#pragma once
#include "../../Gamestates/GameState.h"
#include "../../CommonValues.h"
#include "../../Math.h"
#include "../RewardMath.h"
#include <cmath>

// ============================================================================
// DemoGeometry.h - 2026-11-09
//
// Where was the car that got demoed? "Offensive factor" f in [0,1]:
//
//   f = lineProx x ballProx
//     lineProx : two lines from the ball centre, one to each post of the attacked goal (the SHOT LANE). A car inside the triangle they
//                form with the goal line is on the lane (distance 0); outside it, the distance is to the nearer of the two segments.
//                1 within LINE_FULL of the lane, 0 beyond LINE_ZERO. A car far behind the ball or in a far corner scores 0.
//     ballProx : 1 within BALL_FULL of the ball (floor plane), 0 beyond BALL_ZERO.
//
// OffensiveDemoReward pays  demo x (MULT_FLOOR + (1 - MULT_FLOOR) x f)  in team games: a perfectly placed demo pays EXACTLY what the flat DemoReward
// paid (multiplier 1.0), the least useful demo pays MULT_FLOOR (< 1). The telemetry (Telemetry/DemoTelemetry.h) reads the same f, so the reward
// and its measurement cannot drift apart. Tests: tests/test_demo_geometry.cpp.
// ============================================================================

namespace RLGC {
	namespace DemoGeometry {
		constexpr float LINE_FULL = 400.0f, LINE_ZERO = 1600.0f; // uu, victim to the ball->goal segment
		constexpr float BALL_FULL = 1000.0f, BALL_ZERO = 3500.0f; // uu, victim to the ball
		constexpr float MULT_FLOOR = 0.4f;                         // payout share of the least useful demo (< 1: it paid 1.0 before)
		constexpr int ACTIVE_MIN_TEAM_SIZE = 2;                    // the multiplier is a no-op (1.0) when both teams have one player

		// 1 at d <= full, 0 at d >= zero, linear between.
		inline float Ramp(float d, float full, float zero) {
			if (d <= full) return 1.0f;
			if (d >= zero) return 0.0f;
			return (zero - d) / (zero - full);
		}

		// Distance in the floor (xy) plane from p to segment a-b.
		inline float SegmentDistXY(const Vec& p, const Vec& a, const Vec& b) {
			float abx = b.x - a.x, aby = b.y - a.y;
			float len2 = abx * abx + aby * aby;
			float t = 0.0f;
			if (len2 > 1e-6f) t = RS_CLAMP(((p.x - a.x) * abx + (p.y - a.y) * aby) / len2, 0.0f, 1.0f);
			float cx = a.x + t * abx, cy = a.y + t * aby;
			float dx = p.x - cx, dy = p.y - cy;
			return sqrtf(dx * dx + dy * dy);
		}

		// The two posts of the goal the team attacks (floor plane; z is unused).
		struct Posts { Vec left, right; };
		inline Posts AttackedPosts(Team bumperTeam) {
			const float y = (bumperTeam == Team::BLUE) ? CommonValues::GOAL_LINE_Y : -CommonValues::GOAL_LINE_Y;
			const float x = CommonValues::GOAL_WIDTH_FROM_CENTER;
			return { Vec(-x, y, 0), Vec(x, y, 0) };
		}

		// p inside triangle a-b-c in the floor plane (edges inclusive). False for a degenerate (zero-area) triangle, e.g. a ball on the goal line.
		inline bool InTriangleXY(const Vec& p, const Vec& a, const Vec& b, const Vec& c) {
			auto cross = [](const Vec& o, const Vec& u, const Vec& v) { return (u.x - o.x) * (v.y - o.y) - (u.y - o.y) * (v.x - o.x); };
			if (fabsf(cross(a, b, c)) < 1.0f) return false;
			float d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
			bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
			return !(neg && pos);
		}

		// Distance from p to the shot lane: 0 inside the ball-post-post triangle, else to the nearer of the two ball->post segments.
		inline float LaneDistXY(const Vec& p, const Vec& ballPos, Team bumperTeam) {
			Posts g = AttackedPosts(bumperTeam);
			if (InTriangleXY(p, ballPos, g.left, g.right)) return 0.0f;
			return RS_MIN(SegmentDistXY(p, ballPos, g.left), SegmentDistXY(p, ballPos, g.right));
		}

		struct Sample {
			bool valid = false;   // a victim was found
			float f = 0.0f;       // offensive factor
			float mult = 1.0f;    // payout multiplier (1.0 when inactive or unresolved)
			float lineDist = 0.0f, ballDist = 0.0f;
		};

		inline float FactorAt(const Vec& ballPos, const Vec& victimPos, Team bumperTeam, float* lineDistOut = nullptr, float* ballDistOut = nullptr) {
			float lineDist = LaneDistXY(victimPos, ballPos, bumperTeam);
			float ballDx = victimPos.x - ballPos.x, ballDy = victimPos.y - ballPos.y;
			float ballDist = sqrtf(ballDx * ballDx + ballDy * ballDy); // floor plane, like the line distance: a ball overhead still counts as near
			if (lineDistOut) *lineDistOut = lineDist;
			if (ballDistOut) *ballDistOut = ballDist;
			return Ramp(lineDist, LINE_FULL, LINE_ZERO) * Ramp(ballDist, BALL_FULL, BALL_ZERO);
		}

		inline float Multiplier(float f) { return MULT_FLOOR + (1.0f - MULT_FLOOR) * RS_CLAMP(f, 0.0f, 1.0f); }

		// At least one team has ACTIVE_MIN_TEAM_SIZE players.
		inline bool IsTeamGame(const GameState& s) {
			int n[2] = { 0, 0 };
			for (auto& p : s.players) n[(int)p.team == (int)Team::BLUE ? 0 : 1]++;
			return n[0] >= ACTIVE_MIN_TEAM_SIZE || n[1] >= ACTIVE_MIN_TEAM_SIZE;
		}

		// A car's position BEFORE this step (the engine may already have moved a demoed car); falls back to the current position.
		inline Vec PosBefore(const Player& p) { return p.prev ? p.prev->pos : p.pos; }

		// The opponent this bumper demoed this step: the demoed opponent nearest to it (events do not carry the pair, and a step with two
		// demos by different cars is rare). Null when none is flagged.
		inline const Player* FindVictim(const Player& bumper, const GameState& s) {
			const Player* best = nullptr; float bestD = 1e30f;
			Vec bp = PosBefore(bumper);
			for (auto& v : s.players) {
				if (v.team == bumper.team || !v.eventState.demoed) continue;
				float d = (PosBefore(v) - bp).Length();
				if (d < bestD) { bestD = d; best = &v; }
			}
			return best;
		}

		// Everything for one bumper's demo this step. Ball position is taken from the previous step so it shares a time with the victim position.
		inline Sample Evaluate(const Player& bumper, const GameState& s) {
			Sample out;
			if (!bumper.eventState.demo) return out;
			const Player* victim = FindVictim(bumper, s);
			if (!victim) return out;
			const Vec ballPos = s.prev ? s.prev->ball.pos : s.ball.pos;
			out.valid = true;
			out.f = FactorAt(ballPos, PosBefore(*victim), bumper.team, &out.lineDist, &out.ballDist);
			out.mult = IsTeamGame(s) ? Multiplier(out.f) : 1.0f;
			return out;
		}
	}
}
