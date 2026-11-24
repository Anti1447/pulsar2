// L1 / patch P1-4: simultaneous car-ball resolution. A perfectly symmetric head-on 50/50 must leave the ball (nearly) where it was, as RocketSim does
// (v2 / v3: ball ends ~(12,15) after both cars hit on tick 257 / 316). Upstream resolved cars in slot order, so the last slot always won.
#include "GpuTestUtil.h"
#include <cmath>
using namespace rocketsim_cuda;
using namespace gputest;

static float Len2(float x, float y) { return std::sqrt(x * x + y * y); }

struct Race {
	SimContext ctx{ 1, 2 };
	CarStatePOD cars[2]; BallStatePOD ball;
	Race(bool swapSlots) {
		ctx.SetTeamLayout(0x2u);
		Quat up(0.70710678f, 0, 0, 0.70710678f), down(0.70710678f, 0, 0, -0.70710678f); // yaw +90 / -90
		CarStatePOD a, b; a.pos = Vec3(0.f, -4608.f, 17.f); a.quat = up; b.pos = Vec3(0.f, 4608.f, 17.f); b.quat = down; a.team = 0; b.team = 1; a.boost = b.boost = 33.3f;
		cars[0] = swapSlots ? b : a; cars[1] = swapSlots ? a : b; cars[0].team = 0; cars[1].team = 1; // slot 0 is always the blue slot; swapping moves the geometry
		ctx.CopyCarStateToDevice(cars, 0, 2); ball.pos = Vec3(0.f, 0.f, 93.15f); ball.vel = Vec3(0.f, 0.f, -0.01f); ctx.CopyBallStateToDevice(&ball, 0, 1); Sync();
	}
	void Run(int ticks) {
		CarControls c[2]; for (auto& k : c) { k.throttle = 1; k.boost = 1; }
		for (int i = 0; i < ticks; i++) { ctx.CopyControlsToDevice(c, 0, 2); ctx.Step(1); }
		ctx.CopyBallStateToHost(&ball, 0, 1); ctx.CopyCarStateToHost(cars, 0, 2); Sync();
	}
};

TEST(SymmetricHeadOnRaceLeavesTheBallInPlace) {
	Race r(false); r.Run(420);
	CHECK(Len2(r.ball.pos.x, r.ball.pos.y) < 60.f); // RocketSim v2 / v3: ~19
	CHECK(std::llabs((long long)r.cars[0].ball_hit_tick_count - (long long)r.cars[1].ball_hit_tick_count) <= 2);
}

TEST(SwappingTheSlotsMirrorsTheOutcome) { // slot order must not matter: both arrangements leave the ball centred
	Race a(false), b(true); a.Run(420); b.Run(420);
	CHECK(Len2(a.ball.pos.x, a.ball.pos.y) < 60.f); CHECK(Len2(b.ball.pos.x, b.ball.pos.y) < 60.f);
}
