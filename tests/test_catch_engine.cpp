#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Framework.h>
#include <RLGymCPP/Rewards/CatchBallOnCarReward.h>
#include <RLGymCPP/Telemetry/CarryCatchTelemetry.h>
#include <vector>

// Engine drop grid for CatchBallOnCarReward Rev 5 (docs/reward_scenarios/CatchBallOnCarReward_2026-11-08.md, re-audit).
// A real RocketSim v3 car drives +y; a ball is dropped so it reaches the roof ~T seconds later (car 600/1000/1400 uu/s, ball horizontal
// speed 0..1.2x car, vz 0/-500/-1000, 2 arrival times, fwd/lateral offsets, 3 sampler phases). The first rising-edge touch on the 8-tick
// sampler is fed to the REAL reward and telemetry. Rev 3/4 (world-frame gates) paid 0 of these 864; this guards against going dead again.
// Run from the repo root (needs build/collision_meshes).
namespace {
	using namespace RocketSim;
	struct S { Vec cp, cv, bp, bv; RotMat rm; bool touch; };

	std::vector<S> Run(float v0, float kH, float vz0, float T, float fo, float lo, int phase) {
		static bool inited = false;
		if (!inited) { for (const char* p : { "build/collision_meshes", "collision_meshes", "../collision_meshes" }) { try { RocketSim::Init(p); inited = true; break; } catch (...) {} } }
		Arena* a = Arena::Create(GameMode::SOCCAR); Car* car = a->AddCar(Team::BLUE);
		CarState cs = car->GetState(); cs.pos = Vec(0, -3500, 17); cs.vel = Vec(0, v0, 0); car->SetState(cs);
		Arena* b = Arena::Create(GameMode::SOCCAR); Car* c2 = b->AddCar(Team::BLUE); c2->SetState(cs);
		int Tt = (int)(T * 120) + phase;
		for (int t = 0; t < Tt; t++) { CarControls c = {}; c.throttle = 1; c2->controls = c; b->Step(1); }
		CarState at = c2->GetState(); delete b;
		BallState bs = a->ball->GetState();
		float bvy = at.vel.y * kH;
		bs.pos = Vec(at.pos.x + lo, at.pos.y + fo - bvy * T, 150.f - vz0 * T + 0.5f * 650.f * T * T);
		bs.vel = Vec(0, bvy, vz0); a->ball->SetState(bs);
		for (int t = 0; t < Tt - 24; t++) { CarControls c = {}; c.throttle = 1; car->controls = c; a->Step(1); }
		std::vector<S> st;
		for (int step = 0; step < 30; step++) {
			CarControls c = {}; c.throttle = 1; car->controls = c; a->Step(8);
			CarState k = car->GetState(); BallState bb = a->ball->GetState();
			bool touch = k.ballHitInfo.isValid && k.ballHitInfo.tickCountWhenHit >= (int64_t)a->tickCount - 8;
			st.push_back({ k.pos, k.vel, bb.pos, bb.vel, k.rotMat, touch });
		}
		delete a; return st;
	}
}

TEST(Catch_engine_drop_grid_pays_and_matches_telemetry) {
	using namespace RLGC;
	int n = 0, pays = 0, mismatch = 0;
	for (float v0 : { 600.f, 1000.f, 1400.f }) for (float k : { 0.f, 0.4f, 0.8f, 1.2f }) for (float vz0 : { 0.f, -500.f, -1000.f })
	for (float T : { 0.6f, 0.9f }) for (float fo : { 0.f, 60.f }) for (float lo : { 0.f, 40.f }) for (int ph : { 0, 3, 6 }) {
		auto st = Run(v0, k, vz0, T, fo, lo, ph);
		size_t i = 1; for (; i < st.size(); i++) if (st[i].touch && !st[i - 1].touch) break;
		if (i >= st.size()) continue;
		GameState prev, now; prev.deltaTime = now.deltaTime = RewardTest::STEP_DT;
		auto mk = [&](const S& s, bool touch) {
			Player p = RewardTest::MakePlayer(1, 0, Team::BLUE, s.cp); p.vel = s.cv; p.rotMat = s.rm; p.ballTouchedStep = touch; return p; };
		prev.players.push_back(mk(st[i - 1], st[i - 1].touch)); prev.ball.pos = st[i - 1].bp; prev.ball.vel = st[i - 1].bv;
		now.players.push_back(mk(st[i], true)); now.ball.pos = st[i].bp; now.ball.vel = st[i].bv;
		now.players[0].prev = &prev.players[0]; now.prev = &prev;
		CatchBallOnCarReward r; r.Reset(now);
		bool paid = r.GetReward(now.players[0], now, false) > 0.0f;
		if (paid != CarryCatchTelemetry::PossessionCatch(now.players[0], now)) mismatch++;
		n++; if (paid) pays++;
	}
	CHECK(n > 800);
	CHECK_EQ(mismatch, 0);
	CHECK(pays * 100 >= n * 15);   // measured ~25%; Rev 4 paid 0
}
