// Engine-agnostic physics scenarios shared by the CPU recorders (RocketSim v2 / v3) and the GPU replayer.
// Docs: docs/gpu_env/G1_PREREG.md (Part A). Pure C++17, no engine types, so the same definitions drive all three engines.
#pragma once
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace parity {
	struct Ctl { float throttle = 0, steer = 0, pitch = 0, yaw = 0, roll = 0; bool jump = false, boost = false, handbrake = false; };
	struct Vec3f { float x = 0, y = 0, z = 0; };
	inline Vec3f Cross(Vec3f a, Vec3f b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	inline Vec3f Norm(Vec3f a) { float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); return { a.x / l, a.y / l, a.z / l }; }

	struct CarInit {
		Vec3f pos, vel, angVel;
		Vec3f forward{ 1, 0, 0 }, up{ 0, 0, 1 };
		float boost = 33.33333f;
		bool onGround = true, hasJumped = false, hasDoubleJumped = false, hasFlipped = false;
		int team = 0;
	};
	struct BallInit { Vec3f pos{ 0, 0, 93.15f }, vel{ 0, 0, -0.01f }, angVel; }; // the 0.01 nudge: a v2 ball at exactly zero velocity never wakes up

	struct Scenario {
		std::string name;
		std::vector<CarInit> cars;
		BallInit ball;
		int ticks = 240;
		std::function<Ctl(int tick, int car)> ctl = [](int, int) { return Ctl{}; };
	};

	// forward direction from yaw (radians, 0 = +x, pi/2 = +y) and pitch (up positive); roll 0
	inline void Face(CarInit& c, float yaw, float pitch = 0) {
		c.forward = { std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch) };
		Vec3f right = Norm(Cross({ 0, 0, 1 }, c.forward));
		c.up = Cross(c.forward, right);
	}
	constexpr float HALF_PI = 1.5707963f;
	inline Vec3f FarBall() { return { 3200, 3200, 93.15f }; }

	inline std::vector<Scenario> BuildScenarios() {
		std::vector<Scenario> out;
		auto add = [&](Scenario s) { out.push_back(std::move(s)); };
		auto baseCar = [](float x, float y, float yaw, float speed) { CarInit c; c.pos = { x, y, 17.f }; Face(c, yaw); c.vel = { speed * std::cos(yaw), speed * std::sin(yaw), 0 }; return c; };

		// ---- driving
		{ Scenario s; s.name = "drive_ground"; s.cars = { baseCar(0, -3500, HALF_PI, 0) }; s.cars[0].boost = 0; s.ball.pos = FarBall(); s.ticks = 240;
		  s.ctl = [](int, int) { Ctl c; c.throttle = 1; return c; }; add(s); }
		{ Scenario s; s.name = "drive_boost"; s.cars = { baseCar(0, -3500, HALF_PI, 0) }; s.cars[0].boost = 100; s.ball.pos = FarBall(); s.ticks = 240;
		  s.ctl = [](int, int) { Ctl c; c.throttle = 1; c.boost = true; return c; }; add(s); }
		for (int dir : { -1, 1 }) {
			Scenario s; s.name = dir < 0 ? "steer_right" : "steer_left"; s.cars = { baseCar(-1500, -3000, 0.f, 1000) }; s.cars[0].boost = 0; s.ball.pos = FarBall(); s.ticks = 240;
			s.ctl = [dir](int, int) { Ctl c; c.throttle = 1; c.steer = (float)(-dir); return c; }; add(s);
		}
		{ Scenario s; s.name = "handbrake_turn"; s.cars = { baseCar(-1500, -3000, 0.f, 1400) }; s.cars[0].boost = 0; s.ball.pos = FarBall(); s.ticks = 180;
		  s.ctl = [](int, int) { Ctl c; c.throttle = 1; c.steer = 1; c.handbrake = true; return c; }; add(s); }

		// ---- jumps and dodges (car starts at rest on the ground, far from the ball)
		{ Scenario s; s.name = "jump_hold_024"; s.cars = { baseCar(0, -3500, HALF_PI, 0) }; s.cars[0].boost = 0; s.ball.pos = FarBall(); s.ticks = 240;
		  s.ctl = [](int t, int) { Ctl c; c.jump = t < 24; return c; }; add(s); }
		{ Scenario s; s.name = "jump_tap_3"; s.cars = { baseCar(0, -3500, HALF_PI, 0) }; s.cars[0].boost = 0; s.ball.pos = FarBall(); s.ticks = 240;
		  s.ctl = [](int t, int) { Ctl c; c.jump = t < 3; return c; }; add(s); }
		{ Scenario s; s.name = "double_jump"; s.cars = { baseCar(0, -3500, HALF_PI, 0) }; s.cars[0].boost = 0; s.ball.pos = FarBall(); s.ticks = 240;
		  s.ctl = [](int t, int) { Ctl c; c.jump = (t < 3) || (t >= 15 && t < 16); return c; }; add(s); }
		for (int p = -1; p <= 1; p++) for (int r = -1; r <= 1; r++) {
			if (!p && !r) continue;
			Scenario s; s.name = "dodge_p" + std::to_string(p) + "_r" + std::to_string(r); s.cars = { baseCar(0, -3500, HALF_PI, 0) }; s.cars[0].boost = 0; s.ball.pos = FarBall(); s.ticks = 300;
			s.ctl = [p, r](int t, int) { Ctl c; c.jump = (t < 3) || (t >= 15 && t < 16); if (t >= 15 && t < 16) { c.pitch = (float)p; c.roll = (float)r; } return c; }; add(s);
		}
		{ Scenario s; s.name = "air_pitch_up"; CarInit c; c.pos = { 0, 0, 400 }; c.boost = 0; c.onGround = false; c.hasJumped = true; s.cars = { c }; s.ball.pos = FarBall(); s.ticks = 60;
		  s.ctl = [](int, int) { Ctl k; k.pitch = 1; return k; }; add(s); }
		{ Scenario s; s.name = "air_roll_yaw_boost"; CarInit c; c.pos = { 0, -2000, 600 }; c.vel = { 0, 800, 200 }; Face(c, HALF_PI, 0.3f); c.boost = 100; c.onGround = false; c.hasJumped = true; s.cars = { c }; s.ball.pos = FarBall(); s.ticks = 120;
		  s.ctl = [](int t, int) { Ctl k; k.boost = true; k.roll = 1; k.yaw = (t > 40) ? -1.f : 0.f; k.pitch = (t > 80) ? 1.f : 0.f; return k; }; add(s); }

		// ---- ball only
		auto ballOnly = [&](const char* name, Vec3f p, Vec3f v, Vec3f w, int ticks) { Scenario s; s.name = name; CarInit c; c.pos = { -2500, -4500, 17 }; s.cars = { c }; s.ball.pos = p; s.ball.vel = v; s.ball.angVel = w; s.ticks = ticks; add(s); };
		ballOnly("ball_drop_400", { 0, 0, 400 }, { 0, 0, -0.01f }, { 0, 0, 0 }, 360);
		ballOnly("ball_roll_1500", { -2000, 0, 93.15f }, { 1500, 0, 0 }, { 0, 0, 0 }, 360);
		ballOnly("ball_roll_1500_spin", { -2000, 0, 93.15f }, { 1500, 0, 0 }, { 0, 4, 0 }, 360);
		ballOnly("ball_side_wall", { 3000, 0, 500 }, { 2000, 0, 0 }, { 0, 0, 0 }, 240);
		ballOnly("ball_back_wall", { 0, 4500, 500 }, { 0, 2000, 300 }, { 0, 0, 0 }, 240);
		ballOnly("ball_ceiling", { 0, 0, 1500 }, { 300, 200, 1500 }, { 0, 0, 0 }, 300);
		ballOnly("ball_corner", { 3000, 4000, 600 }, { 1500, 1500, 0 }, { 0, 0, 0 }, 300);
		ballOnly("ball_angled_floor", { 0, 0, 300 }, { 900, 600, -900 }, { 2, -3, 1 }, 360);

		// ---- car hits ball (engine_probe scenario 4 plus off-center and height variants)
		for (float sp : { 800.f, 1200.f, 1400.f }) for (float off : { 0.f, 60.f, -60.f }) {
			Scenario s; s.name = "carhit_" + std::to_string((int)sp) + "_off" + std::to_string((int)off);
			CarInit c = baseCar(off, -sp * 0.5f - 160, HALF_PI, sp); c.boost = 0; s.cars = { c }; s.ball.pos = { 0, 0, 93.15f }; s.ticks = 240;
			s.ctl = [](int, int) { Ctl k; k.throttle = 1; return k; }; add(s);
		}
		{ Scenario s; s.name = "carry_800"; CarInit c = baseCar(0, -3000, HALF_PI, 800); c.boost = 0; s.cars = { c }; s.ball.pos = { 0, -3000, 17 + 70 + 93.15f }; s.ball.vel = { 0, 800, 0 }; s.ticks = 960;
		  s.ctl = [](int, int) { Ctl k; k.throttle = 0.4f; return k; }; add(s); }
		for (float bs : { 500.f, 1000.f, 2000.f }) {
			Scenario s; s.name = "ball_into_parked_car_" + std::to_string((int)bs); CarInit c = baseCar(0, 0, HALF_PI, 0); s.cars = { c }; s.ball.pos = { 0, 1500, 93.15f }; s.ball.vel = { 0, -bs, 0 }; s.ticks = 480; add(s);
		}

		// ---- bump / demo: car 0 (blue) drives into car 1 (orange), far from the ball
		struct B { const char* tag; float sp; int mode; };
		for (B b : { B{ "slow_headon", 900, 0 }, B{ "fast_headon", 1800, 0 }, B{ "super_headon", 2300, 0 }, B{ "super_rear", 2300, 1 }, B{ "super_side", 2300, 2 } }) {
			Scenario s; s.name = std::string("bump_") + b.tag; CarInit a = baseCar(-1200, -3500, 0.f, b.sp); a.boost = 0; a.team = 0;
			CarInit v = baseCar(-200, -3500, b.mode == 0 ? (float)M_PI : (b.mode == 1 ? 0.f : HALF_PI), b.mode == 0 ? 0.f : 0.f); v.boost = 0; v.team = 1;
			if (b.mode == 1) { v.vel = { 0, 0, 0 }; }
			s.cars = { a, v }; s.ball.pos = FarBall(); s.ticks = 200;
			s.ctl = [](int, int car) { Ctl k; if (car == 0) k.throttle = 1; return k; }; add(s);
		}

		// ---- boost pads: big pad at (-3072,-4096), small pad at (-1792,-4184)
		{ Scenario s; s.name = "pad_big_pickup_respawn"; CarInit c = baseCar(-3500, -4096, 0.f, 600); c.boost = 0; s.cars = { c }; s.ball.pos = FarBall(); s.ticks = 1500;
		  s.ctl = [](int t, int) { Ctl k; k.throttle = (t < 200) ? 0.3f : 0.f; return k; }; add(s); }
		{ Scenario s; s.name = "pad_small_pickup"; CarInit c = baseCar(-2100, -4184, 0.f, 600); c.boost = 0; s.cars = { c }; s.ball.pos = FarBall(); s.ticks = 400;
		  s.ctl = [](int, int) { Ctl k; k.throttle = 0.3f; return k; }; add(s); }
		return out;
	}
}
