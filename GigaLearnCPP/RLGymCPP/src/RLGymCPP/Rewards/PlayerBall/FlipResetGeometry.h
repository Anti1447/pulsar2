#pragma once
#include "../../Gamestates/GameState.h"
#include "../../CommonValues.h"
#include "../../Math.h"
#include "../RewardMath.h"
#include <cmath>

// ============================================================================
// FlipResetGeometry.h - 2026-11-09. Shared pure helpers for FlipResetSetupReward and FlipResetApproachReward.
// Design record: docs/reward_scenarios/FlipResetShaping_2026-11-09.md (user's design Q&A).
// Canonical frame: attack +y for both teams (y negated for ORANGE); walls are symmetric in x, so |x| is used.
// ============================================================================
namespace RLGC {
	namespace FlipResetGeo {
		constexpr float WALL_X = 4096.f, BACKBOARD_Y = 5120.f;
		constexpr float GOAL_MOUTH_X = 993.f;          // |x| below this at the backboard is the goal mouth (893 + margin): a shot, not a wall setup
		constexpr float NEAR_WALL = 1800.f, FULL_NEAR_WALL = 600.f;
		constexpr float MIN_CLOSING = 300.f, FULL_CLOSING = 1200.f;   // ball speed toward the wall (uu/s)
		constexpr float BALL_GROUND_MAX_Z = 300.f;     // "ground control"
		constexpr float CONTROL_DIST = 600.f;          // car-ball centre distance still counted as controlling
		constexpr int   CONTROL_TOUCH_STEPS = 8;       // last touch within ~0.53 s at 15 Hz
		constexpr float BOOST_FULL = 50.f, BOOST_FLOOR = 0.2f;

		inline float TeamSign(Team t) { return t == Team::BLUE ? 1.f : -1.f; }

		constexpr float MAX_BALL_SPEED = 2300.f;       // HARD cap: a faster ball cannot be caught up to (setup pay, match bonus and the launch all die above it)
		constexpr float SIDE_MIN_SPEED = 1000.f;       // ball speed needed going into a side wall
		constexpr float BACKBOARD_MIN_SPEED_FAR = 1000.f, BACKBOARD_MIN_SPEED_NEAR = 600.f;   // lower the closer the ball is to the opponent goal
		constexpr float MATCH_FULL = 200.f, MATCH_ZERO = 600.f;     // |car vel - ball vel| for the speed-match bonus
		constexpr float MATCH_BONUS = 0.5f;                          // setup pay x (1 + 0.5 x matchFrac)

		// 0..1: ball in the attacking half, near a side wall / backboard / corner, moving toward it fast enough. 0 when it does not qualify.
		//  side wall : ball speed >= 1000 and >= 300 toward the wall.
		//  backboard : ball speed >= 1000 far out, falling linearly to 600 at <= 600 uu from the backboard (lower closer to the goal).
		//  any       : ball speed <= 2300.
		inline float WallClosingFrac(Team team, const Vec& ballPos, const Vec& ballVel) {
			float speed = ballVel.Length();
			if (speed > MAX_BALL_SPEED) return 0.f;
			float s = TeamSign(team);
			float cy = s * ballPos.y;
			if (cy < 0.f) return 0.f;
			float ax = fabsf(ballPos.x);
			float vxOut = ballVel.x * (ballPos.x >= 0.f ? 1.f : -1.f);
			float vyOut = s * ballVel.y;
			float best = 0.f;
			auto nearOf = [](float d) { return RS_CLAMP((NEAR_WALL - d) / (NEAR_WALL - FULL_NEAR_WALL), 0.f, 1.f); };
			auto frac = [&](float v, float d) { return RS_CLAMP(v / FULL_CLOSING, 0.f, 1.f) * nearOf(d); };
			float dSide = WALL_X - ax;
			if (dSide < NEAR_WALL && vxOut > MIN_CLOSING && speed >= SIDE_MIN_SPEED) best = RS_MAX(best, frac(vxOut, dSide));
			float dBack = BACKBOARD_Y - cy;
			if (dBack < NEAR_WALL && ax > GOAL_MOUTH_X && vyOut > MIN_CLOSING) {
				float minSpeed = BACKBOARD_MIN_SPEED_FAR + (BACKBOARD_MIN_SPEED_NEAR - BACKBOARD_MIN_SPEED_FAR) * nearOf(dBack);
				if (speed >= minSpeed) best = RS_MAX(best, frac(vyOut, dBack));
			}
			return best;
		}

		// Ground control toward a wall (the stage-1 state). `stepsSinceTouch` is the caller's own count.
		inline bool SetupQualifies(const Player& p, const GameState& s, int stepsSinceTouch) {
			if (p.isDemoed || !p.isOnGround) return false;
			if (s.ball.pos.z > BALL_GROUND_MAX_Z) return false;
			if (stepsSinceTouch > CONTROL_TOUCH_STEPS) return false;
			if ((s.ball.pos - p.pos).Length() > CONTROL_DIST) return false;
			return WallClosingFrac(p.team, s.ball.pos, s.ball.vel) > 0.f;
		}

		// 0..1: the car moves with the ball, vector relative speed <= 200 full, 0 at >= 600; 0 when the ball is faster than 2300.
		inline float SpeedMatchFrac(const Player& p, const Vec& ballVel) {
			if (ballVel.Length() > MAX_BALL_SPEED) return 0.f;
			float rel = (p.vel - ballVel).Length();
			return 1.f - RS_CLAMP((rel - MATCH_FULL) / (MATCH_ZERO - MATCH_FULL), 0.f, 1.f);
		}

		// --- the launch touch: ball rolled up the wall, the car (on the wall) hits it ---
		constexpr float CAR_WALL_DIST = 200.f, BALL_WALL_DIST = 300.f;
		constexpr float CAR_WALL_MIN_Z = 150.f, BALL_WALL_MIN_Z = 250.f;
		inline bool NearWall(Team team, const Vec& pos, float maxDist) {
			float s = TeamSign(team);
			float cy = s * pos.y;
			if (cy < 0.f) return false;
			float ax = fabsf(pos.x);
			if (WALL_X - ax <= maxDist) return true;                                   // side wall (corner included)
			return (BACKBOARD_Y - cy <= maxDist) && ax > GOAL_MOUTH_X;                 // backboard outside the goal mouth
		}
		inline bool LaunchGeometry(const Player& p, const Vec& ballPos) {
			return p.isOnGround && p.pos.z >= CAR_WALL_MIN_Z && ballPos.z >= BALL_WALL_MIN_Z
				&& NearWall(p.team, p.pos, CAR_WALL_DIST) && NearWall(p.team, ballPos, BALL_WALL_DIST);
		}

		// Ball after the launch: state.ballPred if present, else a drag-free ballistic path with no bounces (ticks apart).
		inline Vec PathAt(const GameState& s, int tick) {
			if (s.ballPred && !s.ballPred->empty()) {
				int i = tick < (int)s.ballPred->size() ? tick : (int)s.ballPred->size() - 1;
				return s.ballPred->at(i).pos;
			}
			float t = tick / 120.f;
			return Vec(s.ball.pos.x + s.ball.vel.x * t, s.ball.pos.y + s.ball.vel.y * t, s.ball.pos.z + s.ball.vel.z * t - 325.f * t * t);
		}
		// 1.0 aimed at the opponent goal mouth, 0.5 enters / holds the opponent half, 0 otherwise.
		// A launch from the opponent half must not go back toward our half (net canonical y at the horizon >= now).
		// Faster than 2300 uu/s pays 0 (cannot be caught up to).
		inline float LaunchDirFrac(Team team, const GameState& s) {
			if (s.ball.vel.Length() > MAX_BALL_SPEED) return 0.f;
			float sg = TeamSign(team);
			float cyNow = sg * s.ball.pos.y;
			const int N = (s.ballPred && !s.ballPred->empty()) ? (int)s.ballPred->size() : 241;
			float maxCy = cyNow, endCy = cyNow;
			for (int t = 0; t < N; t += 4) {
				Vec p = PathAt(s, t);
				float cy = sg * p.y;
				if (cy >= BACKBOARD_Y - 93.f) {
					if (fabsf(p.x) < 893.f && p.z < 642.f) return 1.f;
					break;                                                              // hit the backboard / wall first
				}
				if (cy > maxCy) maxCy = cy;
				endCy = cy;
			}
			if (cyNow >= 0.f) return (endCy >= cyNow) ? 0.5f : 0.f;                    // started in the opponent half: must not go back
			return maxCy > 0.f ? 0.5f : 0.f;                                           // started in ours: must cross midfield
		}

		inline float BoostFactor(float boost) { return RS_CLAMP(boost / BOOST_FULL, BOOST_FLOOR, 1.f); }

		// Ballistic intercept: where the ball is when its vz has fallen to `vzTarget` (-250, the middle of the -600..+100 contact window).
		// Ignores bounces (the ball is airborne away from the wall). Falls back to the current position if already below vzTarget.
		inline Vec InterceptPoint(const Vec& pos, const Vec& vel, float vzTarget = -250.f) {
			float t = RS_MAX(0.f, (vel.z - vzTarget) / 650.f);
			return Vec(pos.x + vel.x * t, pos.y + vel.y * t, pos.z + vel.z * t - 325.f * t * t);
		}

		inline float Triangle(float x, float lo, float peak, float hi) {
			if (x <= lo || x >= hi) return 0.f;
			return x < peak ? (x - lo) / (peak - lo) : (hi - x) / (hi - peak);
		}
	}
}
