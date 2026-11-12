#include "TestFramework.h"

#include <RLGymCPP/Framework.h>
#include <RLGymCPP/StateSetters/AirPlayState.h>
#include <cmath>
#include <vector>

// AirPlayState WALL_TO_AIR mode (2026-11-11, user-directed). Real RocketSim arenas, many resets, invariants + shares.
// Report: docs/reward_scenarios/AirPlayWall_2026-11-11.md. Run from the repo root (needs build/collision_meshes).
// A wall-mode reset is recognised by the ball ON THE GROUND (z < 120) rolling horizontally at >= 300 uu/s; the other modes put the ball at
// z >= 350 (carry) or z >= 1100 (flip-reset-ready), so there is no overlap.

namespace {
	using namespace RocketSim;
	using namespace RLGC;

	void InitOnce() {
		static bool inited = false;
		if (inited) return;
		for (const char* p : { "build/collision_meshes", "collision_meshes", "../collision_meshes" }) { try { RocketSim::Init(p); inited = true; break; } catch (...) {} }
	}

	// Follow the ball's floor-plane heading to the wall plane it meets first and classify the contact: 0 side wall, 1 backboard, 2 corner
	// (the ray continues through the sampled contact point, so a corner target ends in the corner region; a side/backboard target keeps its own).
	struct Hit { int kind; float x, y; };
	Hit WallHit(const Vec& p, const Vec& v) {
		Vec d = Vec(v.x, v.y, 0).Normalized();
		float tBest = 1e9f; int axis = -1;
		if (fabsf(d.x) > 1e-6f) { float t = ((d.x > 0 ? 4096.f : -4096.f) - p.x) / d.x; if (t > 0 && t < tBest) { tBest = t; axis = 0; } }
		if (fabsf(d.y) > 1e-6f) { float t = ((d.y > 0 ? 5120.f : -5120.f) - p.y) / d.y; if (t > 0 && t < tBest) { tBest = t; axis = 1; } }
		float hx = p.x + d.x * tBest, hy = p.y + d.y * tBest;
		bool corner = fabsf(hx) > 3200.f && fabsf(hy) > 4000.f;
		return { corner ? 2 : (axis == 0 ? 0 : 1), hx, hy };
	}
}

TEST(AirPlayWall_shares_and_geometry_over_many_resets) {
	InitOnce();
	struct Cfg { int blue, orange; };
	for (Cfg cfg : { Cfg{ 1, 1 }, Cfg{ 2, 2 }, Cfg{ 3, 3 } }) {
		Arena* a = Arena::Create(GameMode::SOCCAR);
		for (int i = 0; i < cfg.blue; i++) a->AddCar(Team::BLUE);
		for (int i = 0; i < cfg.orange; i++) a->AddCar(Team::ORANGE);
		AirPlayState setter; // defaults: wallFrac 0.30, resetFrac 0.5
		const int N = 3000;
		int wall = 0, side = 0, back = 0, corner = 0, midfield = 0, resetReady = 0, carry = 0;
		int supportCars = 0, supportFar = 0;
		for (int n = 0; n < N; n++) {
			setter.ResetArena(a);
			BallState b = a->ball->GetState();
			float hs = sqrtf(b.vel.x * b.vel.x + b.vel.y * b.vel.y);
			if (b.pos.z >= 1100.f) { resetReady++; continue; }
			if (b.pos.z >= 350.f) { carry++; continue; }
			CHECK(b.pos.z < 120.f && hs >= 299.f); // anything else would be an unrecognised mode
			wall++;
			// ball: on the ground, rolling horizontally at 300..2300 uu/s, inside the pitch
			CHECK(fabsf(b.vel.z) < 1e-3f);
			CHECK(hs <= 2301.f);
			CHECK(fabsf(b.pos.x) <= 3901.f && fabsf(b.pos.y) <= 4801.f && fabsf(b.pos.x) + fabsf(b.pos.y) <= 7901.f);
			Hit h = WallHit(b.pos, b.vel);
			if (h.kind == 0) { side++; if (fabsf(h.y) < 1200.f) midfield++; }
			else if (h.kind == 1) { back++; CHECK(fabsf(h.x) >= 1000.f); } // never rolls into the goal mouth
			else corner++;
			// exactly one blue car is the setup car: grounded, collinear behind the ball, 250..600 uu, speed <= 2300, 50..100 boost, same heading
			Vec dir = Vec(b.vel.x, b.vel.y, 0).Normalized();
			int setupCars = 0;
			for (Car* c : a->_cars) {
				CarState s = c->GetState();
				float behind = -((s.pos.x - b.pos.x) * dir.x + (s.pos.y - b.pos.y) * dir.y);
				float lat = fabsf((s.pos.x - b.pos.x) * dir.y - (s.pos.y - b.pos.y) * dir.x);
				if (c->team == Team::BLUE && behind >= 249.f && behind <= 601.f && lat < 1.f) {
					setupCars++;
					CHECK(s.pos.z < 30.f); CHECK(s.vel.Length() <= 2301.f); CHECK(s.boost >= 49.f && s.boost <= 100.f);
					CHECK(s.vel.Dot(dir) > -1.f);
				} else {
					supportCars++; if ((s.pos - b.pos).Length() > 600.f) supportFar++; // every other car, all orange cars too, is placed as support
				}
				CHECK(fabsf(s.pos.x) < 4096.f && fabsf(s.pos.y) < 5120.f);
			}
			CHECK_EQ(setupCars, 1);
		}
		auto share = [&](int k, int of) { return of > 0 ? (float)k / of : 0.f; };
		// shares: wall 0.30 of resets; of those side 0.70 / backboard 0.15 / corner 0.15; the rest keep the old reset/carry split (0.35 each)
		CHECK(fabsf(share(wall, N) - 0.30f) < 0.04f);
		CHECK(fabsf(share(side, wall) - 0.70f) < 0.07f);
		CHECK(fabsf(share(back, wall) - 0.15f) < 0.06f);
		CHECK(fabsf(share(corner, wall) - 0.15f) < 0.06f);
		CHECK(share(back, wall) < share(side, wall) && share(corner, wall) < share(side, wall));
		CHECK(fabsf(share(resetReady, N) - 0.35f) < 0.04f);
		CHECK(fabsf(share(carry, N) - 0.35f) < 0.04f);
		// side-wall contacts concentrate at midfield: triangular over +-3800 puts 53% inside |y| < 1200 (a uniform spread would give 32%)
		CHECK(share(midfield, side) > 0.42f);
		CHECK(share(supportFar, supportCars) > 0.9f);
		delete a;
	}
}

TEST(AirPlayWall_wallFrac_zero_and_one_switch_the_mode_off_and_on) {
	InitOnce();
	Arena* a = Arena::Create(GameMode::SOCCAR); a->AddCar(Team::BLUE); a->AddCar(Team::ORANGE);
	AirPlayState off(900, 0.5f, 1500, 3000, 0.0f), on(900, 0.5f, 1500, 3000, 1.0f);
	for (int n = 0; n < 300; n++) {
		off.ResetArena(a); CHECK(a->ball->GetState().pos.z >= 350.f);   // never a ground roll
		on.ResetArena(a);  CHECK(a->ball->GetState().pos.z < 120.f);    // always a ground roll
	}
	delete a;
}
