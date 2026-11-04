#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"

namespace RLGC {

	// TEAM PRESSURE (2026-07-19, user-directed, RLGym-PPO-guide item; SMOOTHED 2026-11-03, user-directed): a charge of up to -1 per
	// step while NO alive teammate (self included) is pressuring the ball. Pressuring is a smooth score instead of two hard cliffs:
	// each alive car has a PROJECTED distance d_eff = d - tau * min(v_toward, speedCap), d = 3D car-ball distance, v_toward = the car's
	// velocity along the unit vector to the ball (0 when moving away or sideways), so a car closing on the ball counts as already
	// partly there. Its lapse is smoothstep((d_eff - lapseStart) / (lapseFull - lapseStart)): 0 at or below 2000 uu, 1 at or above
	// 3000 uu, 0.5 at 2500 (the old near cliff). The team's lapse is the MINIMUM over its alive cars (one car on the ball covers the
	// team, as before), and the reward is minus that. The window is deliberately wide (shadow defence still counts) so the
	// charge fires only on genuine collective disengagement, never on defensive shape. A DEMOLISHED player's own row is 0 (a lone
	// demolished car used to be charged -1 for the 45 respawn steps = -6.75 weighted); a team with every car down reads 0.
	// Team-level and at most -1 per step in 1v1, 2v2 and 3v3, so one weight serves all modes. Wrap in ZeroSumReward like every event
	// term; mutual non-pressure then cancels (a zero-sum stack cannot charge symmetric passivity - accepted, TimeCost still taxes
	// both sides; real states are asymmetric). This is a RATE, not a potential: it must stay one (a potential telescopes and could
	// not charge a persistent lapse; BallProximityPotential is the potential half).
	// TODO (user, 2026-11-03): tau 2 s, speedCap 1000 uu/s and the 2000-3000 uu window are a baseline; re-evaluate on real results.
	class TeamPressureReward : public Reward {
	public:
		float tau, speedCap, lapseStart, lapseFull;
		TeamPressureReward(float tau = 2.f, float speedCap = 1000.f, float lapseStart = 2000.f, float lapseFull = 3000.f)
			: tau(tau), speedCap(speedCap), lapseStart(lapseStart), lapseFull(lapseFull) {}

		// 0 at or below lapseStart, 1 at or above lapseFull, smoothstep between
		float Lapse(float dEff) const {
			float t = RS_CLAMP((dEff - lapseStart) / (lapseFull - lapseStart), 0.f, 1.f);
			return t * t * (3.f - 2.f * t);
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (player.isDemoed)
				return 0;

			float lapse = 1.f;
			for (auto& p : state.players) {
				if (p.team != player.team || p.isDemoed)
					continue;
				Vec to = state.ball.pos - p.pos;
				float d = to.Length();
				float toward = RS_MAX(0.f, p.vel.Dot(to * (1.f / RS_MAX(d, 1.f))));
				lapse = RS_MIN(lapse, Lapse(d - tau * RS_MIN(toward, speedCap)));
			}
			return -lapse;
		}
	};
}
