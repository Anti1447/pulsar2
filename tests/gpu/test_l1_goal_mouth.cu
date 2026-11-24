// L1 / patch P1-3: the goal mouth. Upstream's SDF had a solid back wall in front of the net (no open-play goals at all).
// Reference crossing ticks are RocketSim v2 / v3 (tools/parity traces, 2026-11-24): ball_goal_center 62, ball_goal_crossbar 62, ball_goal_diagonal 106.
#include "GpuTestUtil.h"
#include <cmath>
using namespace rocketsim_cuda;
using namespace gputest;

struct Shot {
	SimContext ctx{ 1, 1 };
	BallStatePOD b;
	Shot(Vec3 pos, Vec3 vel) {
		CarStatePOD c; c.pos = Vec3(-2500.f, -4500.f, 17.f); b.pos = pos; b.vel = vel;
		ctx.CopyCarStateToDevice(&c, 0, 1); ctx.CopyBallStateToDevice(&b, 0, 1); Sync();
	}
	void Tick() { ctx.Step(1); ctx.CopyBallStateToHost(&b, 0, 1); Sync(); }
	// first tick at which the ball center is beyond the scoring line (5124.25 + 91.25), or -1
	int RunToGoalLine(int maxTicks, float* maxY = nullptr) {
		int cross = -1; float my = 0;
		for (int i = 1; i <= maxTicks; i++) { Tick(); my = std::fmax(my, b.pos.y); if (cross < 0 && b.pos.y > 5215.5f) cross = i; }
		if (maxY) *maxY = my; return cross;
	}
};

TEST(BallIntoTheMouthCrossesTheLineOnTheCpuTick) {
	{ Shot s(Vec3(0.f, 4300.f, 250.f), Vec3(0.f, 1800.f, 0.f)); int c = s.RunToGoalLine(150); CHECK(c > 0); CHECK(std::abs(c - 62) <= 1); }
	{ Shot s(Vec3(0.f, 4300.f, 560.f), Vec3(0.f, 1800.f, 120.f)); int c = s.RunToGoalLine(150); CHECK(c > 0); CHECK(std::abs(c - 62) <= 1); }
	{ Shot s(Vec3(-600.f, 3800.f, 200.f), Vec3(700.f, 1700.f, 0.f)); int c = s.RunToGoalLine(200); CHECK(c > 0); CHECK(std::abs(c - 106) <= 1); }
}

TEST(BallOutsideThePostsOrOverTheBarIsStillBlocked) {
	float my = 0;
	{ Shot s(Vec3(1100.f, 4300.f, 250.f), Vec3(0.f, 1800.f, 0.f)); s.RunToGoalLine(150, &my); CHECK(my < 5130.f); } // wide of the post: wall
	{ Shot s(Vec3(0.f, 4300.f, 900.f), Vec3(0.f, 1800.f, 0.f)); s.RunToGoalLine(150, &my); CHECK(my < 5130.f); }   // above the crossbar: wall
	{ Shot s(Vec3(-1100.f, 4300.f, 250.f), Vec3(0.f, 1800.f, 0.f)); s.RunToGoalLine(150, &my); CHECK(my < 5130.f); }
}

TEST(OwnGoalMouthIsOpenToo) { // symmetric: the blue net at y < 0
	Shot s(Vec3(0.f, -4300.f, 250.f), Vec3(0.f, -1800.f, 0.f));
	float my = 0; int c = 0; for (int i = 1; i <= 150; i++) { s.Tick(); if (!c && s.b.pos.y < -5215.5f) c = i; }
	CHECK(c > 0); CHECK(std::abs(c - 62) <= 1);
}
