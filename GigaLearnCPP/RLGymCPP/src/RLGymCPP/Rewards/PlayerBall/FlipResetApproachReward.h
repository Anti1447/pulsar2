#pragma once
#include "../Reward.h"
#include "../RewardTiming.h"
#include "FlipResetGeometry.h"

namespace RLGC {

	// FlipResetApproachReward (2026-11-09, user-directed; design Q&A in docs/reward_scenarios/FlipResetShaping_2026-11-09.md).
	// Stage 2: after a wall/backboard/corner setup (FlipResetGeo::SetupQualifies within the last 2 s) the ball goes airborne (z > 400) and
	// ARMS this player's approach. While armed, the car airborne and the ball PAST APEX (vz <= +100):
	//  closing   : progress toward the ballistic intercept point (ball at vz -250), (dPrev - dNow)/1500 per step, clamp +-0.2 (two-sided, so hovering/oscillating nets 0)
	//  underside : 0.02 x clamp(-up . toBall) x proximity(800 -> 200 uu)           x handbrake mult
	//  speed     : 0.02 x horizontal match (|rel xy| 0..600) x vertical triangle (car faster in z by 0..500, peak 200) x proximity x handbrake mult
	//  contact   : ONE-SHOT at this player's first touch while armed, if wheels-first (-up.toBall >= 0.3). Raw
	//              1.0 x vzFrac (pre-touch ball vz, triangle -600..+100 peaking at -250)
	//                  x (0.4 + 0.6 x elevFrac)  car 30..60 deg BELOW the ball centre (asin of car-from-ball z), 15 deg taper outside
	//                  x (0.5 + 0.5 x awayFrac)  car on the side AWAY from the opponent goal (within 30 deg full, 0 at 90 deg)
	//                  x handbrake mult (1.25 if handbrake held)
	// DISARM: 3 s after arming, ball below z 300, any opponent touch, this player's touch (paid or not), landing, or a goal.
	// Individual one-sided shaping; wrap in ZeroSum. The reset itself is paid by FlipResetReward (120).
	class FlipResetApproachReward : public Reward {
	public:
		constexpr static float LATCH_SEC = 2.0f, ARM_MAX_SEC = 3.0f;
		constexpr static float ARM_BALL_Z = 400.f, DISARM_BALL_Z = 300.f;
		constexpr static float APEX_VZ_MAX = 100.f;
		constexpr static float CLOSE_SCALE = 1500.f, CLOSE_CLAMP = 0.2f;
		constexpr static float TERM_STEP_MAX = 0.02f;
		constexpr static float WHEELS_TO_BALL_MIN = 0.3f;
		constexpr static float HANDBRAKE_MULT = 1.25f;
		struct PS { int sinceTouch = 1000; float latch = 0.f; bool armed = false; float age = 0.f; bool flew = false; };
		std::vector<PS> ps;

		virtual void Reset(const GameState& initialState) override { ps.assign(initialState.players.size(), PS()); }

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if ((size_t)player.index >= ps.size()) ps.resize(player.index + 1);
			PS& s = ps[player.index];
			const float dt = StepDt(state);
			if (player.ballTouchedStep) s.sinceTouch = 0; else if (s.sinceTouch < 1000) s.sinceTouch++;

			// latch: remember the last ground-control-toward-wall step for LATCH_SEC
			if (FlipResetGeo::SetupQualifies(player, state, s.sinceTouch)) s.latch = LATCH_SEC;
			else s.latch = RS_MAX(0.f, s.latch - dt);

			bool oppTouch = false;
			for (const Player& o : state.players) if (o.team != player.team && o.ballTouchedStep) oppTouch = true;
			if (oppTouch) s.latch = 0.f;   // the ball is no longer under our control: the setup memory dies with an opponent touch

			if (s.armed) {
				s.age += dt;
				if (s.age > ARM_MAX_SEC || state.ball.pos.z < DISARM_BALL_Z || oppTouch || state.goalScored || player.isDemoed)
				{ s.armed = false; s.latch = 0.f; }   // a spent stage needs a FRESH ground setup to re-arm
			} else if (s.latch > 0.f && state.ball.pos.z > ARM_BALL_Z && !oppTouch) {
				s.armed = true; s.age = 0.f; s.flew = false;
			}
			if (!s.armed || !state.prev || !player.prev)
				return 0.f;

			// The car must be airborne to reset. A car still on the wall right after the wall launch waits (no pay, no touch handling);
			// landing AFTER having flown ends the stage.
			if (player.isOnGround) { if (s.flew) { s.armed = false; s.latch = 0.f; } return 0.f; }
			s.flew = true;

			Vec toBall = state.ball.pos - player.pos;
			float d = toBall.Length();
			if (d < 1e-3f) return 0.f;
			Vec dir = toBall / d;
			float under = (-player.rotMat.up).Dot(dir);
			const bool hb = player.prevAction.handbrake > 0.5f;
			const float hbMult = hb ? HANDBRAKE_MULT : 1.f;

			// CONTACT: first touch while armed ends the stage
			if (player.ballTouchedStep) {
				s.armed = false; s.latch = 0.f;
				if (under < WHEELS_TO_BALL_MIN) return 0.f;
				float preVz = state.prev->ball.vel.z;
				float vzFrac = FlipResetGeo::Triangle(preVz, -600.f, -250.f, 100.f);
				if (vzFrac <= 0.f) return 0.f;
				Vec carFromBall = player.pos - state.ball.pos;
				float cl = carFromBall.Length();
				if (cl < 1e-3f) return 0.f;
				float elevDeg = asinf(RS_CLAMP(carFromBall.z / cl, -1.f, 1.f)) * (180.f / 3.14159265f);   // negative = car below the ball
				float elevFrac;
				if (elevDeg >= -60.f && elevDeg <= -30.f) elevFrac = 1.f;
				else if (elevDeg > -30.f) elevFrac = RS_CLAMP(1.f - (elevDeg + 30.f) / 15.f, 0.f, 1.f);
				else elevFrac = RS_CLAMP(1.f - (-60.f - elevDeg) / 15.f, 0.f, 1.f);
				Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
				Vec g = oppGoal - state.ball.pos; g.z = 0.f; float gl = g.Length();
				Vec h = carFromBall; h.z = 0.f; float hl = h.Length();
				float awayFrac = 0.f;
				if (gl > 1e-3f && hl > 1e-3f) {
					float away = -(h / hl).Dot(g / gl);           // 1 = car directly opposite the goal
					awayFrac = RS_CLAMP(away / 0.866f, 0.f, 1.f);  // 30 deg = full, 90 deg = 0
				}
				return vzFrac * (0.4f + 0.6f * elevFrac) * (0.5f + 0.5f * awayFrac) * hbMult;
			}

			// PER-STEP (past apex only)
			if (state.ball.vel.z > APEX_VZ_MAX)
				return 0.f;
			float r = 0.f;
			Vec P = FlipResetGeo::InterceptPoint(state.ball.pos, state.ball.vel);
			if (P.z >= 250.f) {
				float dPrev = (P - player.prev->pos).Length(), dNow = (P - player.pos).Length();
				r += RS_CLAMP((dPrev - dNow) / CLOSE_SCALE, -CLOSE_CLAMP, CLOSE_CLAMP);
			}
			float prox = RS_CLAMP((800.f - d) / 600.f, 0.f, 1.f);
			if (prox > 0.f) {
				r += TERM_STEP_MAX * RS_CLAMP(under, 0.f, 1.f) * prox * hbMult;
				Vec rel = player.vel - state.ball.vel;
				float relH = sqrtf(rel.x * rel.x + rel.y * rel.y);
				float hMatch = 1.f - RS_CLAMP(relH / 600.f, 0.f, 1.f);
				float vMatch = FlipResetGeo::Triangle(rel.z, 0.f, 200.f, 500.f);
				r += TERM_STEP_MAX * hMatch * vMatch * prox * hbMult;
			}
			return r;
		}
	};
}
