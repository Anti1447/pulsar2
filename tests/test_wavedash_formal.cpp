#include "TestFramework.h"

#include <RLGymCPP/Framework.h>
#include <algorithm>
#include <cmath>
#include <vector>

// Pins the engine-measured facts behind docs/WAVEDASH_FORMAL.md. A car falls with its nose pitched `theta` degrees UP (negative = down),
// travelling v0 along +x; a front flip (jump + pitch -1, one tick) is pressed j ticks before the first wheel touches the ground.
// Needs build/collision_meshes (run from the repo root). One geometry, one speed: these are measurements, not laws.

namespace {
	using namespace RocketSim;
	Arena* A = nullptr; Car* C = nullptr;
	struct T { float fz, vx; int wheels; bool ground, flipping, hasFlipped; };

	void Setup() {
		if (A) return;
		for (const char* p : { "build/collision_meshes", "collision_meshes", "../collision_meshes" }) {
			try { RocketSim::Init(p); break; } catch (...) {}
		}
		A = Arena::Create(GameMode::SOCCAR); C = A->AddCar(Team::BLUE);
		BallState bs; bs.pos = Vec(0, 4500, 93); A->ball->SetState(bs);
	}
	CarState Init(float thetaDeg, float v0, float z0) {
		CarState s = C->GetState();
		float th = thetaDeg * 3.14159265f / 180.f;
		s.pos = Vec(-3000, -3500, z0); s.vel = Vec(v0, 0, 0); s.angVel = Vec(0, 0, 0); s.boost = 0;
		s.rotMat = RotMat(Vec(cosf(th), 0, sinf(th)), Vec(0, 1, 0), Vec(-sinf(th), 0, cosf(th)));
		s.isOnGround = false; s.hasJumped = true; s.hasDoubleJumped = false; s.hasFlipped = false; s.isJumping = false;
	s.airTimeSinceJump = 0; s.airTime = 0; s.flipTime = 0; s.jumpTime = 0; s.isFlipping = false; // a stale airTimeSinceJump > 1.25 s silently disables the dodge
		for (int i = 0; i < 4; i++) s.wheelsWithContact[i] = false;
		return s;
	}
	std::vector<T> Run(float theta, float v0, float z0, int flipTick, int ticks = 90) {
		Setup(); C->SetState(Init(theta, v0, z0));
		std::vector<T> out;
		for (int t = 0; t < ticks; t++) {
			CarControls c = {}; c.throttle = 1.f;
			if (flipTick >= 0 && t == flipTick) { c.jump = true; c.pitch = -1.f; }
			C->controls = c; A->Step(1);
			CarState s = C->GetState(); int w = 0; for (int i = 0; i < 4; i++) w += s.wheelsWithContact[i];
			out.push_back({ s.rotMat.forward.z, s.vel.x, w, s.isOnGround, s.isFlipping, s.hasFlipped });
		}
		return out;
	}
	int FirstWheel(const std::vector<T>& r) { for (size_t t = 0; t < r.size(); t++) if (r[t].wheels > 0) return (int)t; return -1; }
	int CancelTicks(const std::vector<T>& r, int F) { for (int t = F + 1; t < (int)r.size(); t++) if (!r[t].flipping && r[t - 1].flipping) return t - F; return -1; }
	int AllGrounded(const std::vector<T>& r) { for (size_t t = 0; t < r.size(); t++) if (r[t].wheels >= 3 && r[t].ground) return (int)t; return -1; }
}

TEST(WavedashForm_pitch_sign_plus_is_nose_up) {
	Setup(); C->SetState(Init(0, 0, 400));
	for (int t = 0; t < 8; t++) { CarControls c = {}; c.pitch = 1.f; C->controls = c; A->Step(1); }
	CHECK(C->GetState().rotMat.forward.z > 0.f);
}

TEST(WavedashForm_nose_up_flip_one_to_two_ticks_before_contact_is_cancelled_and_keeps_speed) {
	auto ctrl = Run(25.f, 1000, 70, -1); int T1 = FirstWheel(ctrl); CHECK(T1 > 5);
	float ctrlVx = ctrl[std::min((int)ctrl.size() - 1, T1 + 30)].vx;
	for (int j : { 1, 2 }) {
		int F = T1 - j; auto r = Run(25.f, 1000, 70, F);
		int W1 = FirstWheel(r);
		CHECK_EQ(r[W1].wheels, 2);                       // trailing (rear) wheels touch first
		int c = CancelTicks(r, F); CHECK(c > 0 && c <= 12);   // flip cancelled within ~10 ticks, far inside the 0.65 s flip window
		int g = AllGrounded(r); CHECK(g > 0 && g - W1 <= 10); // leading wheels follow within a few ticks
		CHECK(!r.back().hasFlipped);                     // flag clears on landing
		CHECK(r[std::min((int)r.size() - 1, W1 + 30)].vx - ctrlVx > 300.f); // measured +422
	}
}

TEST(WavedashForm_press_window_is_about_two_ticks) {
	// Pressed 3+ ticks early (or not registering at all) the flip does not end in a landing-cancel within the next 90 ticks.
	auto ctrl = Run(25.f, 1000, 70, -1); int T1 = FirstWheel(ctrl);
	for (int j : { 3, 4, 6, 7 }) {
		int F = T1 - j; auto r = Run(25.f, 1000, 70, F);
		CHECK_EQ(CancelTicks(r, F), -1);
		CHECK_EQ(AllGrounded(r), -1);
	}
}

TEST(WavedashForm_nose_down_flip_is_never_cancelled) {
	auto ctrl = Run(-25.f, 1000, 70, -1); int T1 = FirstWheel(ctrl); CHECK(T1 > 5);
	for (int j : { 1, 2, 4, 5 }) {
		int F = T1 - j; auto r = Run(-25.f, 1000, 70, F);
		CHECK_EQ(CancelTicks(r, F), -1);                 // flip keeps running, no early cancel
		CHECK(r.back().hasFlipped);                      // still mid-flip/airborne at 90 ticks
	}
}

// ---- calibration facts used by tools/wavedash_sim (level car, high up so the ground never interferes) ----
namespace {
	CarState InitHigh(float vx, float vz, float z0, bool hasJumped) {
		CarState s = C->GetState();
		s.pos = Vec(-3000, -3500, z0); s.vel = Vec(vx, 0, vz); s.angVel = Vec(0, 0, 0); s.boost = 0;
		s.rotMat = RotMat(Vec(1, 0, 0), Vec(0, 1, 0), Vec(0, 0, 1));
		s.isOnGround = false; s.hasJumped = hasJumped; s.hasDoubleJumped = false; s.hasFlipped = false; s.isJumping = false;
		s.airTimeSinceJump = 0; s.airTime = 0; s.flipTime = 0; s.jumpTime = 0; s.isFlipping = false;
		for (int i = 0; i < 4; i++) s.wheelsWithContact[i] = false;
		return s;
	}
	// press a flip at tick `press` (pitch input `pitch`), run `ticks`, return the final state
	CarState FlipRun(float vx, float vz, float z0, bool hasJumped, int press, float pitch, int ticks) {
		Setup(); C->SetState(InitHigh(vx, vz, z0, hasJumped));
		for (int t = 0; t < ticks; t++) { CarControls c = {}; if (t == press) { c.jump = true; c.pitch = pitch; } C->controls = c; A->Step(1); }
		return C->GetState();
	}
}

TEST(WavedashForm_dodge_availability_no_jump_has_no_timer_a_jump_has_1p25s) {
	// A car that never jumped (fell / drove off a surface) can still flip 2.3 s into the fall; after a jump the window closes at 1.25 s.
	CHECK(FlipRun(800, 0, 1900, false, 280, -1.f, 285).isFlipping);   // press at 2.33 s, no jump: registers
	CHECK(FlipRun(800, 0, 1900, true, 100, -1.f, 105).isFlipping);    // 0.83 s after a jump: registers
	CHECK(!FlipRun(800, 0, 1900, true, 160, -1.f, 165).isFlipping);   // 1.33 s after a jump: expired
}

TEST(WavedashForm_front_flip_impulse_is_500_up_to_the_2300_cap) {
	for (float v0 : { 0.f, 500.f, 1000.f, 1500.f }) {
		CarState ctl = FlipRun(v0, 0, 1500, true, 9999, -1.f, 8), fl = FlipRun(v0, 0, 1500, true, 2, -1.f, 8);
		CHECK_NEAR(fl.vel.x - ctl.vel.x, 500.f, 2.f);
	}
	{ CarState ctl = FlipRun(2000, 0, 1500, true, 9999, -1.f, 8), fl = FlipRun(2000, 0, 1500, true, 2, -1.f, 8); CHECK_NEAR(fl.vel.x - ctl.vel.x, 300.f, 3.f); }
	{ CarState ctl = FlipRun(2300, 0, 1500, true, 9999, -1.f, 8), fl = FlipRun(2300, 0, 1500, true, 2, -1.f, 8); CHECK_NEAR(fl.vel.x - ctl.vel.x, 0.f, 3.f); }
}

TEST(WavedashForm_back_flip_decelerates_more_than_a_front_flip_accelerates) {
	for (float v0 : { 0.f, 1000.f, 2000.f }) {
		CarState ctl = FlipRun(v0, 0, 1500, true, 9999, 1.f, 8), fl = FlipRun(v0, 0, 1500, true, 2, 1.f, 8);
		CHECK_NEAR(fl.vel.x - ctl.vel.x, -(533.3f + 0.348f * v0), 6.f);
	}
}

TEST(WavedashForm_dodge_damps_vertical_speed_to_minus_15_while_flipping) {
	// From the 18th tick after the press until the flip ends (tick 79), vz is multiplied by 0.65 each tick (gravity applies after), so it
	// converges to -5.42 / 0.35 = -15.47 uu/s whatever it was before: a flip makes the car float.
	for (float vz0 : { -400.f, -200.f, 0.f, 300.f }) {
		CarState s = FlipRun(1000, vz0, 1700, true, 0, -1.f, 60);
		CHECK_NEAR(s.vel.z, -15.47f, 1.5f);
	}
	// before the damping starts gravity acts normally (17 ticks after the press: unchanged fall rate)
	CarState a = FlipRun(1000, 0, 1700, true, 0, -1.f, 17), b = FlipRun(1000, 0, 1700, true, 9999, -1.f, 17);
	CHECK_NEAR(a.vel.z, b.vel.z, 0.5f);
}
