#include "TestFramework.h"

#include <RLGymCPP/Framework.h>
#include <cmath>
#include <vector>

// Engine-grounded check of the "wavedash" detectors (docs/reward_scenarios/RolvWavedashReward_2026-11-01.md).
// Drives a REAL RocketSim v3 car: one jump, then a level front flip pressed at tick F, and replays the two
// detector rules on an 8-tick sampler (tickSkip 8) at all 8 sampling phases:
//   Basic = stock WavedashReward / v3 WavedashBasicReward: cur.ground && prev.flipping && !prev.ground
//   Rolv  = RolvWavedashReward: cur.ground && !prev.ground && (cur.flipping || prev.flipping)
// Needs build/collision_meshes (run from the repo root). Level car, no ball, one spawn: it measures detection
// TIMING, not whether a flip is a "good" wavedash.

namespace {
	using namespace RocketSim;
	struct Tick { bool ground, flipping; Vec vel; };

	std::vector<Tick> RunCar(int flipTick, int total = 330) {
		static bool inited = false;
		if (!inited) {
			for (const char* path : { "build/collision_meshes", "collision_meshes", "../collision_meshes" }) {
				try { RocketSim::Init(path); inited = true; break; } catch (...) {}
			}
		}
		Arena* arena = Arena::Create(GameMode::SOCCAR);
		Car* car = arena->AddCar(Team::BLUE);
		CarState s = car->GetState();
		s.pos = Vec(2500, -3500, 17); s.vel = Vec(0, 0, 0); s.angVel = Vec(0, 0, 0);
		car->SetState(s);
		std::vector<Tick> out;
		for (int t = 0; t < total; t++) {
			CarControls c = {};
			if (t == 2) c.jump = true;
			if (flipTick >= 0 && t == flipTick) { c.jump = true; c.pitch = -1.f; }
			car->controls = c;
			arena->Step(1);
			CarState cs = car->GetState();
			out.push_back({ cs.isOnGround, cs.isFlipping, cs.vel });
		}
		delete arena;
		return out;
	}

	struct Hits { int basic = 0, rolv = 0; float meanDv = 0; bool flipActiveAtLanding = false; int flipToLand = -1; };

	Hits Replay(int F, int land0) {
		(void)land0;
		auto r = RunCar(F);
		int air = -1, fs = -1, land = -1;
		for (int t = 0; t < (int)r.size(); t++) { if (air < 0 && !r[t].ground) air = t; if (fs < 0 && r[t].flipping) fs = t; }
		for (int t = fs; t >= 0 && t < (int)r.size(); t++) if (r[t].ground && t > air + 3) { land = t; break; }
		Hits h;
		if (land < 0 || fs < 0) return h;
		h.flipActiveAtLanding = r[land].flipping; h.flipToLand = land - fs;
		float dvSum = 0;
		for (int ph = 0; ph < 8; ph++) {
			float stored = 0; int prevT = -1;
			for (int t = ph; t < (int)r.size(); t += 8) {
				if (prevT >= 0) {
					const Tick& cur = r[t]; const Tick& prv = r[prevT];
					bool landed = cur.ground && !prv.ground;
					bool wd = landed && (cur.flipping || prv.flipping);
					if (cur.flipping && !prv.flipping) stored = (cur.vel - prv.vel).Length();
					if (t > air && cur.ground && prv.flipping && !prv.ground) h.basic++;
					if (t > air && wd) { h.rolv++; dvSum += stored; stored = 0; }
					else if (!cur.flipping) stored = 0;
					if (t > air && landed) break;
				}
				prevT = t;
			}
		}
		h.meanDv = h.rolv ? dvSum / h.rolv : 0.f;
		return h;
	}

	int NoFlipLandingTick() {
		auto r = RunCar(-1); int air = -1;
		for (int t = 0; t < (int)r.size(); t++) { if (air < 0 && !r[t].ground) air = t; if (air >= 0 && r[t].ground && t > air + 3) return t; }
		return -1;
	}
}

TEST(Wavedash_engine_ordinary_flip_hops_are_not_detected) {
	// A flip pressed 4 or more ticks before the no-flip landing tick launches the car up (the dodge lifts it) and
	// the flip is over long before it lands: neither detector fires at any sampler phase.
	int land0 = NoFlipLandingTick(); CHECK(land0 > 0);
	for (int F : { land0 - 60, land0 - 30, land0 - 12, land0 - 6, land0 - 4 }) {
		Hits h = Replay(F, land0);
		CHECK(!h.flipActiveAtLanding);
		CHECK_EQ(h.basic, 0);
		CHECK_EQ(h.rolv, 0);
	}
}

TEST(Wavedash_engine_last_moment_flip_is_detected_only_some_phases) {
	// A flip pressed 2 ticks before landing is still active at touchdown. At tickSkip 8 only some sampler phases
	// see it, and Rolv's extra `cur.flipping` term catches at least as many as the stock rule.
	int land0 = NoFlipLandingTick();
	Hits h = Replay(land0 - 2, land0);
	CHECK(h.flipActiveAtLanding);
	CHECK(h.basic >= 1 && h.basic < 8);        // observed 4/8
	CHECK(h.rolv >= h.basic && h.rolv < 8);    // observed 5/8
	CHECK(h.meanDv > 400.0f && h.meanDv < 600.0f); // observed ~497: the dodge impulse, near-constant across flips
	Hits late = Replay(land0, land0);          // flip on the final airborne tick: seen in only ~1 phase
	CHECK(late.rolv <= h.rolv);
}
