#pragma once
#include "../Gamestates/GameState.h"
#include "../CommonValues.h"
#include "../Math.h"
#include "../Rewards/RewardMath.h"

// ============================================================================
// CarryCatchTelemetry.h - 2026-11-08
//
// State-only predicates for "is this car carrying the ball" and "did this car just make a possession catch".
// They read only the GameState (and its prev links), never a reward, so they report the same whether
// DribbleMasteryReward / CatchBallOnCarReward are in the stack or not. That is the point: these are the measurements the
// audit (docs/reward_scenarios/*_2026-11-08.md) said were missing before either reward is re-enabled.
//
// The carry gates are COPIES of DribbleMasteryReward's (DribbleRewards.h); tests/test_reward_audit_dribblemastery.cpp
// asserts the two agree on a grid of states, so a change to one that forgets the other fails a test.
// Wired into StepCallback in src/ExampleMain.cpp (Carry/* and Catch/* panels).
// ============================================================================

namespace RLGC {
	namespace CarryCatchTelemetry {
		// --- carry gates (== DribbleMasteryReward) ---
		constexpr float CARRY_MIN_UP = 100.0f, CARRY_MAX_UP = 230.0f;
		constexpr float CARRY_MIN_FWD = -70.0f, CARRY_MAX_FWD = 150.0f;
		constexpr float CARRY_MAX_LAT = 75.0f;
		constexpr float CARRY_MAX_REL_SPEED = 500.0f;
		constexpr float CARRY_MIN_SPEED = 800.0f;
		constexpr float CARRY_GROUND_MAX_Z = 600.0f;

		// --- catch definition (independent of the reward's pay geometry) ---
		constexpr float CATCH_MIN_INCOMING_REL_SPEED = 400.0f; // car-relative (Rev 5)
		constexpr float CATCH_MIN_SLOWDOWN_FRAC = 0.35f;       // of the relative incoming speed
		constexpr float CATCH_END_MAX_REL_SPEED = 500.0f; // ball must leave the touch moving with the car
		constexpr float CATCH_END_MAX_EXCESS_SPEED = 250.0f; // ... and not faster than the car by more than this

		// Ball position in the car's own frame.
		struct CarFrame { float lf, lr, lu; };
		inline CarFrame BallInCarFrame(const Player& p, const Vec& ballPos) {
			Vec rel = ballPos - p.pos;
			return { rel.Dot(p.rotMat.forward), rel.Dot(p.rotMat.right), rel.Dot(p.rotMat.up) };
		}

		// Ball on the roof/hood of a grounded, moving car, moving with it.
		inline bool InCarry(const Player& p, const Vec& ballPos, const Vec& ballVel) {
			if (!p.isOnGround || ballPos.z > CARRY_GROUND_MAX_Z) return false;
			CarFrame f = BallInCarFrame(p, ballPos);
			if (f.lu < CARRY_MIN_UP || f.lu > CARRY_MAX_UP) return false;
			if (f.lf < CARRY_MIN_FWD || f.lf > CARRY_MAX_FWD) return false;
			if (fabsf(f.lr) > CARRY_MAX_LAT) return false;
			if (p.vel.Length() < CARRY_MIN_SPEED) return false;
			if ((ballVel - p.vel).Length() > CARRY_MAX_REL_SPEED) return false;
			return true;
		}

		inline bool InCarry(const Player& p, const GameState& s) { return InCarry(p, s.ball.pos, s.ball.vel); }

		// Was the same car carrying one step ago? False when there is no previous state (first step of an episode).
		inline bool InCarryPrev(const Player& p, const GameState& s) {
			if (!s.prev || !p.prev) return false;
			return InCarry(*p.prev, s.prev->ball.pos, s.prev->ball.vel);
		}

		// Dot of the ball's velocity direction with the direction to the opponent goal back (car forward if the ball is still).
		inline float CarryTowardOpp(const Player& p, const GameState& s) {
			Vec opponentGoal = (p.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
			Vec toOpp = opponentGoal - s.ball.pos;
			float len = toOpp.Length();
			if (len <= 1e-5f) return 0.0f;
			Vec dir = toOpp / len;
			float ballSpeed = s.ball.vel.Length();
			return (ballSpeed > 1e-3f) ? s.ball.vel.Normalized().Dot(dir) : p.rotMat.forward.Dot(dir);
		}

		// Ball y in the team-canonical frame (positive = toward the opponent goal).
		inline float CanonicalBallY(const Player& p, const GameState& s) {
			return (p.team == Team::BLUE) ? s.ball.pos.y : -s.ball.pos.y;
		}

		// A first-contact touch that took a real share of the ball's speed RELATIVE to the car (Rev 5). Geometry-free: counts blocks,
		// clears and traps alike. (== the speed/edge gates of CatchBallOnCarReward Rev 3.)
		inline bool SlowingTouch(const Player& p, const GameState& s) {
			if (!p.ballTouchedStep || !s.prev) return false;
			if (p.prev && p.prev->ballTouchedStep) return false; // rising edge
			Vec prevCarVel = p.prev ? p.prev->vel : p.vel;
			float prevRel = (s.prev->ball.vel - prevCarVel).Length();
			float curRel = (s.ball.vel - p.vel).Length();
			if (prevRel < CATCH_MIN_INCOMING_REL_SPEED) return false;
			if ((prevRel - curRel) / prevRel < CATCH_MIN_SLOWDOWN_FRAC) return false;
			return true;
		}

		// A possession-building catch: a slowing touch after which the ball sits on the car and moves with it
		// (same car-frame band as the carry, without the car-speed or ground gates). CatchBallOnCarReward Rev 4 pays exactly
		// when this is true (tests/test_reward_audit_catchballoncar.cpp asserts the equivalence on a grid).
		inline bool PossessionCatch(const Player& p, const GameState& s) {
			if (!SlowingTouch(p, s)) return false;
			CarFrame f = BallInCarFrame(p, s.ball.pos);
			if (f.lu < CARRY_MIN_UP || f.lu > CARRY_MAX_UP) return false;
			if (f.lf < CARRY_MIN_FWD || f.lf > CARRY_MAX_FWD) return false;
			if (fabsf(f.lr) > CARRY_MAX_LAT) return false;
			if ((s.ball.vel - p.vel).Length() > CATCH_END_MAX_REL_SPEED) return false;
			return s.ball.vel.Length() <= p.vel.Length() + CATCH_END_MAX_EXCESS_SPEED;
		}
	}
}
