#include "TestFramework.h"

#include <RLGymCPP/Framework.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

// Wavedash oracle v2 at tickSkip 8 / actionDelay 0 in the REAL engine (docs/reward_scenarios/wavedash_ts8_experiments_2026-11-02.md).
// A car falls forward with the dodge available; a scripted executor issues a forward flip k decisions before the predicted touchdown
// (decisions = 8 ticks, actions held); optionally a nose tilt (pitch +-1) for the first `tilt` decisions. The reward rules are replayed on
// the 15 Hz end-of-step snapshots. Needs build/collision_meshes (run from the repo root).
// Fixture lessons (both cost a wrong result once): the orientation matrix must be right-handed (right = (-fy, fx, 0) in this engine) and
// airTimeSinceJump must be reset per episode (a stale value > 1.25 s silently disables the dodge).
// Scripted executor, not an optimal oracle; the speed-gain label is satisfied by throttle alone, so only robust facts are pinned.

namespace {
using namespace RocketSim;
struct Snap { bool ground, flipping; Vec vel; float hspeed; };
struct Ep { std::vector<Snap> S; std::vector<int> act; }; // act[k] = jump input held during the step that produced S[k]

static Arena* g_arena = nullptr; static Car* g_car = nullptr; static int g_tilt = 0; static float g_tiltPitch = -1.f;

static Ep RunEp(std::mt19937& rng, int offsetK /* -1 = control (never flip) */, int steps = 26) {
	std::uniform_real_distribution<float> U(0.f, 1.f);
	CarState s = g_car->GetState();
	float z0 = 60.f + 60.f * U(rng);
	s.pos = Vec(-1500.f + 3000.f * U(rng), -2500.f * U(rng), z0);
	float hd = 6.2831853f * U(rng);
	Vec f(cosf(hd), sinf(hd), 0.f);
	s.rotMat = RotMat(f, Vec(-sinf(hd), cosf(hd), 0.f), Vec(0, 0, 1));
	s.vel = f * (600.f + 500.f * U(rng)); s.angVel = Vec(0, 0, 0); s.boost = 30;
	s.isOnGround = false; s.hasJumped = true; s.hasDoubleJumped = false; s.hasFlipped = false; s.isJumping = false;
	s.airTimeSinceJump = 0; s.airTime = 0; s.flipTime = 0; s.jumpTime = 0; s.isFlipping = false; // a stale airTimeSinceJump > 1.25 s silently disables the dodge
	g_car->SetState(s);
	BallState bs; bs.pos = Vec(0, 4500, 93); bs.vel = Vec(0, 0, 0); g_arena->ball->SetState(bs);

	auto snap = [&]() { CarState c = g_car->GetState(); return Snap{ c.isOnGround, c.isFlipping, c.vel, sqrtf(c.vel.x * c.vel.x + c.vel.y * c.vel.y) }; };
	Ep ep; ep.S.push_back(snap()); ep.act.push_back(0);
	bool flipped = false;
	for (int k = 0; k < steps; k++) {
		CarState c = g_car->GetState();
		bool doFlip = false;
		if (offsetK >= 0 && !flipped && !c.isOnGround) {
			float disc = c.vel.z * c.vel.z + 4 * 325.f * (c.pos.z - 17.f);
			float tLand = (c.vel.z + sqrtf(std::max(disc, 0.f))) / 650.f;
			float decLeft = tLand * 15.f;               // decisions are 8 ticks = 1/15 s
			doFlip = decLeft <= offsetK + 0.5f;
		}
		CarControls ctl = {}; ctl.throttle = 1.f;
		if (k < g_tilt) ctl.pitch = g_tiltPitch;   // nose tilt applied from the first decision, for g_tilt decisions
		if (doFlip) { ctl.jump = true; ctl.pitch = -1.f; flipped = true; }
		g_car->controls = ctl;
		g_arena->Step(8);
		ep.S.push_back(snap()); ep.act.push_back(doFlip ? 1 : 0);
	}
	return ep;
}

static int Landing(const Ep& e) { for (size_t k = 1; k < e.S.size(); k++) if (e.S[k].ground && !e.S[k - 1].ground) return (int)k; return -1; }

static bool Label(const Ep& e) {     // repo census: hspeed[i+3] > hspeed[i-1] + 100 across the landing at i
	int i = Landing(e); if (i < 1) return false;
	int j = std::min(i + 3, (int)e.S.size() - 1);
	return e.S[j].hspeed > e.S[i - 1].hspeed + 100.f;
}
static float Basic(const Ep& e) { float s = 0; for (size_t k = 1; k < e.S.size(); k++) if (e.S[k].ground && e.S[k - 1].flipping && !e.S[k - 1].ground) s += 1.f; return s; }
static float Rolv(const Ep& e) {
	float s = 0, stored = 0;
	for (size_t k = 1; k < e.S.size(); k++) {
		const Snap& c = e.S[k]; const Snap& p = e.S[k - 1];
		bool wd = c.ground && !p.ground && (c.flipping || p.flipping);
		if (c.flipping && !p.flipping) stored = (c.vel - p.vel).Length();
		if (wd) { s += stored / 2300.f; stored = 0; } else if (!c.flipping) stored = 0;
	}
	return s;
}
static float R4(const Ep& e, bool needJump) {
	int i = Landing(e); if (i < 1 || i + 3 >= (int)e.S.size()) return 0.f;
	if (needJump) { bool near = false; for (int k = std::max(1, i - 4); k <= i + 3; k++) near |= e.act[k] != 0; if (!near) return 0.f; }
	float gain = e.S[i + 3].hspeed - e.S[i - 1].hspeed;
	return gain > 100.f ? std::min(1.f, (gain - 100.f) / 400.f) : 0.f;
}


struct Cell { double label = 0, basic = 0, rolv = 0, r4 = 0, r4n = 0, meanGain = 0; };

Cell RunCell(int off, int tilt, float tiltPitch, int n) {
	static bool inited = false;
	if (!inited) {
		for (const char* p : { "build/collision_meshes", "collision_meshes", "../collision_meshes" }) {
			try { RocketSim::Init(p); inited = true; break; } catch (...) {}
		}
		g_arena = Arena::Create(GameMode::SOCCAR); g_car = g_arena->AddCar(Team::BLUE);
	}
	g_tilt = tilt; g_tiltPitch = tiltPitch;
	std::mt19937 rng(20261102 + off + 17 * tilt);
	int s = 0, b = 0, r = 0, f4 = 0, f4n = 0, landed = 0; double gain = 0;
	for (int i = 0; i < n; i++) {
		Ep e = RunEp(rng, off);
		int l = Landing(e);
		if (l >= 1) { landed++; int j = std::min(l + 3, (int)e.S.size() - 1); gain += e.S[j].hspeed - e.S[l - 1].hspeed; }
		s += Label(e); b += Basic(e) > 0; r += Rolv(e) > 0; f4 += R4(e, true) > 0; f4n += R4(e, false) > 0;
	}
	return { 100.0 * s / n, 100.0 * b / n, 100.0 * r / n, 100.0 * f4 / n, 100.0 * f4n / n, landed ? gain / landed : 0.0 };
}
}

TEST(WavedashTs8_level_approach_is_invisible_to_the_stock_and_rolv_detectors) {
	// Level car, flip k decisions before touchdown, k = 0..3: neither detector ever fires (observed 0 / 2000 in every arm).
	for (int k : { 0, 1, 2, 3 }) {
		Cell c = RunCell(k, 0, -1.f, 300);
		CHECK_EQ(c.basic, 0.0);
		CHECK_EQ(c.rolv, 0.0);
	}
}

TEST(WavedashTs8_speed_gain_label_is_satisfied_by_throttle_alone) {
	// The NO-FLIP control already "succeeds" the census-style label (> +100 uu/s across the landing) in > 85% of episodes: after landing
	// the throttle adds ~145 uu/s within 3 steps. A rule without a jump-input condition therefore pays throttle landings; R4 pays none.
	Cell ctrl = RunCell(-1, 0, -1.f, 300);
	CHECK(ctrl.label > 85.0);
	CHECK(ctrl.r4n > 85.0);
	CHECK_EQ(ctrl.r4, 0.0);
	CHECK(ctrl.meanGain > 100.0 && ctrl.meanGain < 200.0);   // observed +145
}

TEST(WavedashTs8_flip_attributable_gain_is_about_three_hundred_not_one_hundred) {
	// Best nose-up cell (1 decision of tilt, flip 1 decision before touchdown): mean gain ~490 vs ~150 in the same-tilt control.
	Cell flip = RunCell(1, 1, 1.f, 300), ctrl = RunCell(-1, 1, 1.f, 300);
	CHECK(flip.meanGain - ctrl.meanGain > 250.0);
}

TEST(WavedashTs8_nose_up_approach_makes_the_detectors_fire) {
	// After a 3-decision nose-up tilt and a flip 1 decision before touchdown, Basic and Rolv fire in most episodes
	// (observed 74% / 82%); Rolv's extra cur.flipping term never catches fewer than Basic.
	Cell c = RunCell(1, 3, 1.f, 300);
	CHECK(c.basic > 30.0);
	CHECK(c.rolv >= c.basic - 1e-9);
	CHECK(c.rolv > 30.0);
}
