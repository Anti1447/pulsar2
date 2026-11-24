// L2: the host adapter over RocketSim-CUDA (gpu/adapter/ArenaBatch): layouts, state round trip, isolation, controls, events, pads, goals, ball prediction.
#include "TestFramework.h"
#include "ArenaBatch.h"
#include <cmath>
#include <cuda_runtime.h>
using namespace pulsar_gpu;

static std::vector<CarData> Cars(const ArenaBatch& b, int arena) {
	std::vector<CarData> v(b.NumCars(arena)); for (size_t s = 0; s < v.size(); s++) { v[s].team = (uint8_t)b.TeamOfSlot(arena, (int)s); v[s].pos[0] = -3000.f + 300.f * (float)s; v[s].pos[1] = -4000.f; v[s].pos[2] = 17.f; }
	return v;
}
static BallData FarBall() { BallData b; b.pos[0] = 3000; b.pos[1] = 3000; return b; }

TEST(LayoutsHaveTheRequestedTeamSizes) {
	struct M { int b, o; } modes[] = { {1,1}, {2,2}, {2,1}, {3,3}, {3,2}, {3,1} };
	std::vector<ModeSpec> specs; for (auto m : modes) specs.push_back({ m.b, m.o, 3 });
	ArenaBatch batch(specs);
	CHECK_EQ(batch.NumArenas(), 18);
	for (int i = 0; i < 6; i++) for (int k = 0; k < 3; k++) {
		int a = i * 3 + k, blue = 0, orange = 0;
		for (int s = 0; s < batch.NumCars(a); s++) (batch.TeamOfSlot(a, s) ? orange : blue)++;
		CHECK_EQ(blue, modes[i].b); CHECK_EQ(orange, modes[i].o);
	}
}

TEST(StateRoundTripsThroughTheGpu) {
	ArenaBatch batch({ { 2, 2, 4 } });
	auto cars = Cars(batch, 1); cars[0].vel[0] = 123.f; cars[1].boost = 77.f; cars[2].hasJumped = 1; cars[2].onGround = 0; cars[3].angVel[2] = 1.5f;
	BallData ball = FarBall(); ball.vel[1] = -250.f;
	batch.SetState(1, cars, ball); batch.Sync();
	for (int s = 0; s < 4; s++) { const CarData& c = batch.Car(1, s);
		CHECK_NEAR(c.pos[0], cars[s].pos[0], 1e-3f); CHECK_NEAR(c.pos[1], cars[s].pos[1], 1e-3f); CHECK_EQ((int)c.team, batch.TeamOfSlot(1, s)); }
	CHECK_NEAR(batch.Car(1, 0).vel[0], 123.f, 1e-3f); CHECK_NEAR(batch.Car(1, 1).boost, 77.f, 1e-3f);
	CHECK_EQ((int)batch.Car(1, 2).hasJumped, 1); CHECK_EQ((int)batch.Car(1, 2).onGround, 0); CHECK_NEAR(batch.Car(1, 3).angVel[2], 1.5f, 1e-3f);
	CHECK_NEAR(batch.Ball(1).vel[1], -250.f, 1e-3f); CHECK_NEAR(batch.Ball(1).pos[0], 3000.f, 1e-3f);
}

TEST(SetStateOnOneArenaLeavesOthersUntouched) {
	ArenaBatch batch({ { 1, 1, 5 } });
	for (int a = 0; a < 5; a++) { auto c = Cars(batch, a); c[0].vel[1] = 10.f * (float)(a + 1); batch.SetState(a, c, FarBall()); }
	batch.Sync(); float before[5]; for (int a = 0; a < 5; a++) before[a] = batch.Car(a, 0).vel[1];
	auto c = Cars(batch, 2); c[0].vel[1] = 999.f; batch.SetState(2, c, FarBall()); batch.Sync();
	for (int a = 0; a < 5; a++) CHECK_NEAR(batch.Car(a, 0).vel[1], a == 2 ? 999.f : before[a], 1e-3f);
}

TEST(ControlsMoveTheCarAndStepCountsTicks) {
	ArenaBatch batch({ { 1, 1, 2 } });
	for (int a = 0; a < 2; a++) { auto c = Cars(batch, a); c[0].quat[0] = 0.70710678f; c[0].quat[3] = 0.70710678f; batch.SetState(a, c, FarBall()); }
	std::vector<ControlData> drive(2), idle(2); drive[0].throttle = 1;
	batch.SetControls(0, drive); batch.SetControls(1, idle);
	batch.Step(60); batch.Sync();
	CHECK(batch.Car(0, 0).pos[1] > -4000.f + 100.f); CHECK(std::fabs(batch.Car(1, 0).pos[1] + 4000.f) < 1.f);
	CHECK_EQ((int)batch.Tick(0), 60); CHECK_EQ((int)batch.Tick(1), 60);
}

// A cross-team demo during one Step must show in the flags after Sync, and the NEXT Step must start from cleared flags.
TEST(StepEventsAccumulateThenClear) {
	ArenaBatch batch({ { 1, 1, 1 } });
	auto c = Cars(batch, 0);
	c[0].pos[0] = -900; c[0].pos[1] = -3000; c[0].vel[0] = 2300; c[0].isSupersonic = 1; c[0].quat[0] = 1;
	c[1].pos[0] = -200; c[1].pos[1] = -3000; batch.SetState(0, c, FarBall());
	std::vector<ControlData> ctl(2); ctl[0].throttle = 1; batch.SetControls(0, ctl);
	for (int i = 0; i < 20; i++) batch.Step(4);
	// find the step where the demo happened by re-running
	batch.SetState(0, c, FarBall()); bool seen = false;
	for (int i = 0; i < 20 && !seen; i++) { batch.Step(4); batch.Sync(); if (batch.Car(0, 0).demo) { seen = true; CHECK_EQ((int)batch.Car(0, 1).demoed, 1); CHECK_EQ((int)batch.Car(0, 1).isDemoed, 1); } }
	CHECK(seen);
	batch.Step(4); batch.Sync(); CHECK_EQ((int)batch.Car(0, 0).demo, 0); CHECK_EQ((int)batch.Car(0, 1).demoed, 0);
}

TEST(BigPadPickupTurnsItOffAndCountsDownToRespawn) {
	ArenaBatch batch({ { 1, 1, 1 } });
	auto c = Cars(batch, 0); c[0].pos[0] = -3072; c[0].pos[1] = -4096; c[0].boost = 0; c[1].pos[0] = 0; c[1].pos[1] = 100;
	batch.SetState(0, c, FarBall()); batch.Sync();
	int active0 = 0; for (int i = 0; i < NUM_PADS; i++) active0 += batch.PadActive(0)[i]; CHECK_EQ(active0, NUM_PADS);
	batch.Step(8); batch.Sync();
	int active1 = 0; float maxCd = 0; for (int i = 0; i < NUM_PADS; i++) { active1 += batch.PadActive(0)[i]; maxCd = std::fmax(maxCd, batch.PadCooldown(0)[i]); }
	CHECK_EQ(active1, NUM_PADS - 1); CHECK(maxCd > 9.f && maxCd <= 10.f); CHECK(batch.Car(0, 0).boost > 99.f);
}

TEST(GoalFlagRaisedWhenBallCrossesTheLine) {
	ArenaBatch batch({ { 1, 1, 2 } });
	BallData in = FarBall(); in.pos[0] = 0; in.pos[1] = 4700; in.pos[2] = 250; in.vel[1] = 1800;
	for (int a = 0; a < 2; a++) batch.SetState(a, Cars(batch, a), a == 0 ? in : FarBall());
	batch.Step(60); batch.Sync();
	CHECK(batch.GoalScored(0)); CHECK_EQ(batch.ScoringTeam(0), 0); CHECK(!batch.GoalScored(1));
}

// Prediction slice k (0.25 s ahead each) must equal the ball actually stepped 30 (k+1) ticks later.
TEST(BallPredictionSlicesMatchTheRealFuture) {
	ArenaBatch batch({ { 1, 1, 1 } });
	BallData b = FarBall(); b.pos[0] = -500; b.pos[1] = 0; b.pos[2] = 800; b.vel[0] = 700; b.vel[1] = 400; b.vel[2] = 300;
	batch.SetState(0, Cars(batch, 0), b); batch.Sync();
	float pred[8 * 6]; for (int i = 0; i < 48; i++) pred[i] = batch.BallPredSlices(0)[i];
	for (int k = 0; k < 4; k++) {
		batch.Step(30); batch.Sync();
		const BallData& r = batch.Ball(0);
		CHECK_NEAR(r.pos[0], pred[k * 6 + 0], 0.5f); CHECK_NEAR(r.pos[1], pred[k * 6 + 1], 0.5f); CHECK_NEAR(r.pos[2], pred[k * 6 + 2], 0.5f);
		CHECK_NEAR(r.vel[0], pred[k * 6 + 3], 0.5f); CHECK_NEAR(r.vel[2], pred[k * 6 + 5], 0.5f);
	}
}
