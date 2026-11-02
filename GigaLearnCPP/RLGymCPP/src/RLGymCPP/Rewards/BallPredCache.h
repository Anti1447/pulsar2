#pragma once
#include "../Gamestates/GameState.h"
#include "../CommonValues.h"
#include <cmath>
#include <vector>

// ============================================================================
// BallPredCache.h
// Drop-in replacement for RLGymBot_v3's BallPred::Cache that reads the engine's per-tick ball
// prediction from GameState::ballPred (the same BallPredictor the obs builder uses) instead of
// running its own analytic simulation.
//
// Same shape as the v3 cache so ported rewards change very little:
//   Update(state) in PreStep(), `valid`, `slices[i]`, AtTime(seconds), HitSurfaceBy, ...
// Differences from v3's BallPred (read before porting a reward):
//   * Horizon is whatever the builder predicts (2.0s at ballPredSlices = 8), not 4s. `numSlices`
//     replaces the NUM_SLICES constant; AtTime() clamps to the last slice, so AtTime(3.0f) on a
//     2.0s horizon silently returns the 2.0s slice.
//   * Slices are 15 Hz like v3 (slice i is the ball (i+1)*8 ticks ahead, t = (i+1)/15).
//   * The surface flags are DERIVED from the per-tick path: a bounce is a tick-to-tick velocity
//     change that gravity does not explain (> BOUNCE_DV), classified by where it happened. They are
//     sticky ("happened at or before this slice"), as in v3. Events between two sampled ticks are
//     reported on the next slice.
//   * hitBackboard is set the first time the ball centre reaches |y| >= BACK_WALL_Y - BALL_RADIUS
//     (or bounces off the back wall). Unlike v3's analytic model, the engine ball really flies
//     into the goal, but the slice where the flag first sets is the same "goal-plane arrival"
//     point v3's goal-mouth checks judge x/z at.
//   * `valid` is false when the state has no prediction (obs builder without ballPredSlices, or a
//     state without an arena). AtTime() then returns a stationary-ball fallback slice rather than
//     crashing; callers should still check `valid`.
// ============================================================================

namespace RLGC {

class BallPredCache {
public:
	static constexpr int   TICKS_PER_SLICE = 8;
	static constexpr float SLICE_DT = TICKS_PER_SLICE * CommonValues::TICK_TIME; // 1/15 s
	// Velocity change per tick (uu/s) that gravity/drag cannot explain => a collision. Gravity is
	// 5.4 uu/s per tick and drag under ~2, so this sits far above both and far below a real bounce.
	static constexpr float BOUNCE_DV = 100.0f;
	static constexpr float SURFACE_PAD = 40.0f; // uu from a surface (ball centre) that still counts as touching it

	struct Slice {
		Vec   pos, vel;
		float t = 0.0f;       // seconds from now
		bool  hitGround = false, hitWall = false, hitCeiling = false, hitBackboard = false; // sticky
	};

	std::vector<Slice> slices;
	int  numSlices = 0;
	bool valid = false;

	void Update(const GameState& state) {
		fallback = Slice();
		fallback.pos = state.ball.pos;
		fallback.vel = Vec(0, 0, 0);

		const std::vector<BallState>* pred = state.ballPred;
		numSlices = 0;
		valid = false;
		if (!pred || (int)pred->size() < TICKS_PER_SLICE + 1)
			return;

		const int n = ((int)pred->size() - 1) / TICKS_PER_SLICE;
		slices.resize(n);
		numSlices = n;
		valid = true;

		constexpr float R = CommonValues::BALL_RADIUS;
		const float gdv = CommonValues::GRAVITY_Z * CommonValues::TICK_TIME;
		bool ground = false, wall = false, ceil = false, back = false;
		if (std::fabs((*pred)[0].pos.y) >= CommonValues::BACK_WALL_Y - R)
			back = true;

		for (int k = 1; k <= n * TICKS_PER_SLICE; k++) {
			const BallState& a = (*pred)[k - 1];
			const BallState& b = (*pred)[k];
			Vec dv = b.vel - a.vel;
			dv.z -= gdv;
			if (dv.Length() > BOUNCE_DV) {
				const Vec& p = b.pos;
				if (p.z < R + SURFACE_PAD)                                    ground = true;
				else if (p.z > CommonValues::CEILING_Z - R - SURFACE_PAD)     ceil = true;
				else if (std::fabs(p.y) > CommonValues::BACK_WALL_Y - R - SURFACE_PAD) back = true;
				else                                                          wall = true; // side wall / corner ramp
			}
			if (std::fabs(b.pos.y) >= CommonValues::BACK_WALL_Y - R)
				back = true;

			if (k % TICKS_PER_SLICE == 0) {
				Slice& s = slices[k / TICKS_PER_SLICE - 1];
				s.pos = b.pos; s.vel = b.vel;
				s.t = k * CommonValues::TICK_TIME;
				s.hitGround = ground; s.hitWall = wall; s.hitCeiling = ceil; s.hitBackboard = back;
			}
		}
	}

	// Seconds covered by the prediction (0 when invalid).
	float Horizon() const { return numSlices * SLICE_DT; }

	// Slice nearest `seconds` ahead, clamped to [first, last]. Fallback slice when invalid.
	const Slice& AtTime(float seconds) const {
		if (!valid) return fallback;
		int idx = (int)std::lround(seconds / SLICE_DT) - 1;
		if (idx < 0) idx = 0;
		if (idx >= numSlices) idx = numSlices - 1;
		return slices[idx];
	}

	bool HitGroundBy(float seconds) const { return AtTime(seconds).hitGround; }
	bool HitSurfaceBy(float seconds) const {
		const Slice& s = AtTime(seconds);
		return s.hitWall || s.hitCeiling || s.hitBackboard;
	}

	// Time of the first slice with hitGround set, or -1.
	float FirstGroundHitTime() const {
		for (int i = 0; i < numSlices; i++)
			if (slices[i].hitGround) return slices[i].t;
		return -1.0f;
	}

	// Distinct ground-bounce onsets in the first `seconds`. The flag is sticky, so a bounce is counted
	// only on a slice where it is newly set; only the FIRST ground contact is distinguishable this way
	// (v3's cache had the same limitation: PassingSetup counted rising edges of a sticky flag).
	int GroundBouncesBy(float seconds) const {
		int count = 0;
		for (int i = 0; i < numSlices; i++) {
			if (slices[i].t > seconds) break;
			if (slices[i].hitGround && (i == 0 || !slices[i - 1].hitGround)) count++;
		}
		return count;
	}

private:
	Slice fallback;
};

}
