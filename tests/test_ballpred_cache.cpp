#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/BallPredCache.h>

using namespace RLGC;
using namespace RewardTest;

// BallPredCache derives v3-style sticky surface flags from the engine's per-tick ball path
// (GameState::ballPred). Hand-computed expectations, drag-free path with restitution 0.6:
//   lob z0=500, vz=0     -> floor at sqrt(2*(500-92.75)/650) = 1.119 s
//   wall x0=3000, vx=1500 -> x = 4096-92.75 at 0.669 s
//   shot y0=3000, vy=2500 -> y = 5120-92.75 at 0.811 s (x stays 200)

static BallPredCache Run(const std::vector<BallState>& path, GameState& gs) {
	gs.ball.pos = path[0].pos; gs.ball.vel = path[0].vel;
	gs.ballPred = &path;
	BallPredCache c; c.Update(gs); return c;
}

TEST(BallPredCache_lob_lands_at_1p12s) {
	GameState gs; auto p = BallPath(Vec(0, 0, 500), Vec(0, 800, 0)); auto c = Run(p, gs);
	CHECK(c.valid);
	CHECK_EQ(c.numSlices, 30); // 241 ticks = 2.0 s at 15 Hz
	CHECK(!c.HitGroundBy(1.0f));
	float t = c.FirstGroundHitTime();
	CHECK(t > 1.1f && t < 1.2f);
	CHECK_EQ(c.GroundBouncesBy(2.0f), 1);
	CHECK(!c.HitSurfaceBy(2.0f));
}

TEST(BallPredCache_side_wall) {
	GameState gs; auto p = BallPath(Vec(3000, 0, 600), Vec(1500, 0, 0)); auto c = Run(p, gs);
	float tw = -1; for (auto& s : c.slices) if (s.hitWall) { tw = s.t; break; }
	CHECK(tw > 0.66f && tw < 0.8f);
	CHECK(!c.slices[2].hitWall); // 0.2 s
}

TEST(BallPredCache_rolling_ball_sets_no_flag) {
	// Counterfactual to the bounce tests: same speed class, no collision anywhere in 2 s.
	GameState gs; auto p = BallPath(Vec(0, 0, 92.75f), Vec(0, 1000, 0)); auto c = Run(p, gs);
	for (auto& s : c.slices)
		CHECK(!(s.hitGround || s.hitWall || s.hitCeiling || s.hitBackboard));
}

TEST(BallPredCache_goal_bound_shot_arrival_slice) {
	GameState gs; auto p = BallPath(Vec(200, 3000, 600), Vec(0, 2500, 0)); auto c = Run(p, gs);
	int i0 = -1; for (int i = 0; i < c.numSlices; i++) if (c.slices[i].hitBackboard) { i0 = i; break; }
	CHECK(i0 >= 0);
	CHECK(c.slices[i0].t > 0.8f && c.slices[i0].t < 0.9f);
	CHECK_NEAR(c.slices[i0].pos.x, 200.0f, 5.0f);
	CHECK(!c.slices[i0].hitGround && !c.slices[i0].hitWall);
}

TEST(BallPredCache_ceiling) {
	GameState gs; auto p = BallPath(Vec(0, 0, 1800), Vec(0, 500, 1500)); auto c = Run(p, gs);
	CHECK(c.HitSurfaceBy(1.0f) && c.AtTime(1.0f).hitCeiling);
}

TEST(BallPredCache_missing_or_short_prediction_is_invalid) {
	GameState gs; gs.ball.pos = Vec(1, 2, 300);
	gs.ballPred = nullptr;
	BallPredCache c; c.Update(gs);
	CHECK(!c.valid && c.numSlices == 0);
	CHECK_NEAR(c.AtTime(1.5f).pos.z, 300.0f, 1e-6f); // stationary-ball fallback, no flags
	CHECK(!c.HitGroundBy(1.5f));
	std::vector<BallState> shortPath(5); gs.ballPred = &shortPath; c.Update(gs);
	CHECK(!c.valid);
}

TEST(BallPredCache_AtTime_clamps_to_horizon) {
	GameState gs; auto p = BallPath(Vec(0, 0, 500), Vec(0, 0, 0)); auto c = Run(p, gs);
	CHECK(&c.AtTime(3.0f) == &c.slices.back()); // v3 had 4 s; here AtTime(3.0) silently returns the 2.0 s slice
	CHECK(&c.AtTime(0.0f) == &c.slices.front());
}
