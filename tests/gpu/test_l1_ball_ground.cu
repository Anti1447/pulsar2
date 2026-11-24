// L1 / patches P1-1 (ball angular speed cap) and P1-2 (speculative ball-ground contact).
// Reference numbers are RocketSim v2's (build_v2/EngineProbe and tools/parity v2 traces, 2026-11-24); v3 0.3.5 agrees within 0.5%.
#include "GpuTestUtil.h"
#include <cmath>
using namespace rocketsim_cuda;
using namespace gputest;

static float Len(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

struct Ball1 {
	SimContext ctx{ 1, 1 };
	BallStatePOD b;
	Ball1(Vec3 pos, Vec3 vel, Vec3 w) {
		CarStatePOD c; c.pos = Vec3(-2500.f, -4500.f, 17.f); // parked far away
		b.pos = pos; b.vel = vel; b.ang_vel = w;
		ctx.CopyCarStateToDevice(&c, 0, 1); ctx.CopyBallStateToDevice(&b, 0, 1); Sync();
	}
	void Tick() { ctx.Step(1); ctx.CopyBallStateToHost(&b, 0, 1); Sync(); }
};

// P1-1: a ball rolling at 1500 uu/s must keep losing speed to ground friction because its spin is capped at 6 rad/s.
TEST(RollingBallSpeedMatchesV2) {
	Ball1 w(Vec3(-2000.f, 0.f, 93.15f), Vec3(1500.f, 0.f, -0.01f), Vec3(0.f, 0.f, 0.f));
	const float v2[3] = { 1230.9f, 969.9f, 716.7f };
	for (int s = 0; s < 3; s++) {
		for (int i = 0; i < 120; i++) w.Tick();
		const float speed = Len(w.b.vel);
		CHECK(std::fabs(speed - v2[s]) / v2[s] < 0.015f);
	}
}

TEST(BallAngularSpeedNeverExceedsCap) {
	Ball1 w(Vec3(0.f, 0.f, 300.f), Vec3(300.f, 100.f, -200.f), Vec3(15.f, -12.f, 9.f)); // |w| ~ 21 rad/s
	for (int i = 0; i < 400; i++) { w.Tick(); CHECK(Len(w.b.ang_vel) <= 6.0f + 1e-3f); }
}

// P1-2: first bounce of a ball dropped from z = 400 lands on the same tick as RocketSim (118, +-1) and peaks within 1.5% of v2 (199.7).
TEST(DropBounceMatchesV2) {
	Ball1 w(Vec3(0.f, 0.f, 400.f), Vec3(0.f, 0.f, -0.01f), Vec3(0.f, 0.f, 0.f));
	int impactTick = -1; float peak = 0; float prevVz = 0;
	for (int i = 1; i <= 260; i++) {
		w.Tick();
		if (impactTick < 0 && w.b.vel.z > 0) impactTick = i;
		if (impactTick > 0 && prevVz > 0 && w.b.vel.z <= 0 && peak == 0) peak = w.b.pos.z;
		prevVz = w.b.vel.z;
	}
	CHECK(std::abs(impactTick - 118) <= 1);
	CHECK(std::fabs(peak - 199.7f) / 199.7f < 0.015f);
}
