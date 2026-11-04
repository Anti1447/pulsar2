#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"

namespace RLGC {

	// Aerial STRIKE reward - replaces TouchHeightReward as the aerial gradient.
	// Why TouchHeight failed (Aerial Touch Ratio stuck at 0.00125 after 17B+ steps):
	//   (a) avg(ballZ, carZ) paid ground strikes and hop-pokes - no DIFFERENTIAL
	//       between the current 193uu-touch behavior and the target skill;
	//   (b) it was gated, and the HER-trained gate structurally discounts states
	//       the policy has never achieved; positive-part-only gating also made
	//       the zero-sum pair net-negative on every aerial touch.
	// This version is an AND (min, not avg): full credit requires SUSTAINED
	// FLIGHT and a GENUINELY HIGH BALL simultaneously. isOnGround is true on
	// walls, so wall-pinned touches pay exactly 0; the ball-height ramp starts
	// at 150uu (rest ball = 93, current mean touch = 193 -> pays ~0). The
	// impulse factor kills carry annuities; the refire cooldown caps juggle
	// self-rally at ~1.25 payouts/s (1 / the 0.8s cooldown, tickSkip-invariant).
	// Wrap in ZeroSumReward(_, 0). Do NOT gate.
	// KICKOFF RACE (2026-07-20, user-directed: Pulsar wins most play but loses NET on
	// conceded kickoff goals - it loses the kickoff). A kickoff is a symmetric sprint
	// to the ball; on a CONTESTED kickoff, getting the first touch fast is the game.
	// Rewards that first touch, TIME-DECAYED (fast ~1, dawdled ~0), zero-sum wrapped
	// so the winner gains and the loser mirrors it.
	//
	// THE CONTESTED GATE (the user's real safeguard): a "delay kickoff" is the
	// OPPONENT declining the 50/50 to bait us into committing so they can counter.
	// An UNCONDITIONAL first-touch reward would farm us there - the bot would rush in
	// for the touch reward and hand them the counter. So the touch pays ONLY when an
	// opponent is actually contesting the ball (near it or closing on it) at the
	// moment of contact. Against a hang-back, taking the touch pays 0, so the reward
	// never drives us into the trap; the real objective (Goal/B2G/possession) governs
	// how we play a delay instead. Detection: at the touch, is any opponent within
	// CONTEST_DIST of the ball, or closing on it above CONTEST_SPEED. Symmetric - it
	// pays whichever side wins a genuine contest and mirrors the loss.
	//
	// Kickoff is detected ONLY at Reset (ball spawned at field center at rest;
	// KickoffState / FuzzedKickoffState both leave the ball at (0,0,rest)), so a ball
	// passing through center mid-play can never false-fire it. Fires once per kickoff.
	//
	// 2026-11-02 audit fixes (REWARD_CHANGE_LOG): (1) WINDOW_STEPS 30 -> 45. A straight boost rush from the real
	// kickoff spots first touches the ball at step 31 (diagonal) to 40 (back), so the old 2.0 s window paid nothing on
	// any real kickoff; 3.0 s covers them (a touch on step k pays 1 - k/45). (2) Every player who touches on the
	// FIRST-TOUCH step is paid (it used to be only the lowest player index, so a dead-even 50/50 always favoured blue;
	// kickoffs are exactly point-symmetric, so ties are the normal case).
	class KickoffRaceReward : public Reward {
	public:
		constexpr static int WINDOW_STEPS = 45;      // 3s at 15Hz (tickSkip 8); a touch on step k pays 1 - k/45
		constexpr static float CONTEST_DIST = 1500;  // opponent within this of the ball = contesting
		constexpr static float CONTEST_SPEED = 500;  // ...or closing on it faster than this
		bool active = false, resolved = false;
		int steps = 0;
		int resolveStep = -1; // the step of the first touch: touches on that same step are all paid

		static bool IsKickoffBall(const GameState& s) {
			return fabsf(s.ball.pos.x) < 200 && fabsf(s.ball.pos.y) < 200
				&& s.ball.vel.Length() < 50;
		}

		// Is an opponent of the toucher actually going for the ball? (Not a delay.)
		bool Contested(const Player& toucher, const GameState& s) const {
			for (auto& p : s.players) {
				if (p.team == toucher.team || p.isDemoed)
					continue;
				Vec to = s.ball.pos - p.pos;
				float d = to.Length();
				if (d < CONTEST_DIST)
					return true;
				if (d > 1e-3f && p.vel.Dot(to * (1.f / d)) > CONTEST_SPEED)
					return true;
			}
			return false;
		}

		virtual void Reset(const GameState& initialState) override {
			active = IsKickoffBall(initialState);
			resolved = false;
			steps = 0;
			resolveStep = -1;
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!active)
				return 0;
			if (player.index == 0) // one bump per step (index 0 exists in every arena)
				steps++;
			if (resolved && steps != resolveStep)
				return 0; // decided on an earlier step
			if (steps > WINDOW_STEPS) { // window elapsed unresolved -> no race reward
				resolved = true;
				resolveStep = steps;
				return 0;
			}
			if (!player.ballTouchedStep)
				return 0;
			if (!resolved) { // first touch of the kickoff, either team
				resolved = true;
				resolveStep = steps;
			}
			// (a touch on the SAME step by a later player index is also paid: a 50/50 is a tie, not a win for the lower index)
			if (!Contested(player, state))
				return 0; // uncontested (delay kickoff) -> no drive to commit into a counter
			return RS_CLAMP(1.f - (float)steps / WINDOW_STEPS, 0.f, 1.f);
		}

		virtual std::string GetName() override { return "KickoffRace"; }
	};
}
