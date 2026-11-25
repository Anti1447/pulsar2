// Engine-agnostic physics scenarios shared by the CPU recorders (RocketSim v2 / v3) and the GPU replayer.
// Docs: docs/gpu_env/G1_PREREG.md (Part A). Pure C++17, no engine types, so the same definitions drive all three engines.
#pragma once
#include <cmath>
#include <math.h>
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
			Scenario s; s.name = "dodge_p" + std::to_string(p) + "_r" + std::to_string(r); s.cars = { baseCar(0, -800, HALF_PI, 0) }; s.cars[0].boost = 0; s.ball.pos = FarBall(); s.ticks = 300;
			s.ctl = [p, r](int t, int) { Ctl c; c.jump = (t < 3) || (t >= 15 && t < 16); if (t >= 15 && t < 16) { c.pitch = (float)p; c.roll = (float)r; } return c; }; add(s);
		}
		// ---- aerial touches: an airborne car boosting into an airborne ball, centred and off-centre
		for (int off : { -60, 0, 60 }) {
			Scenario s; s.name = "aerialhit_off" + std::to_string(off); CarInit c; c.pos = { (float)off, -700, 500 }; c.vel = { 0, 900, 250 }; Face(c, HALF_PI, 0.15f); c.boost = 100; c.onGround = false; c.hasJumped = true;
			s.cars = { c }; s.ball.pos = { 0, 0, 640 }; s.ball.vel = { 0, 0, 250 }; s.ticks = 180;
			s.ctl = [](int, int) { Ctl k; k.boost = true; return k; }; add(s);
		}
		// ---- flips into a ball on the ground: front flip and diagonal flip
		for (int r : { 0, 1 }) {
			Scenario s; s.name = std::string("fliphit_") + (r ? "diag" : "front"); s.cars = { baseCar(r ? -60.f : 0.f, -700, HALF_PI, 1000) }; s.cars[0].boost = 0; s.ball.pos = { 0, 0, 93.15f }; s.ticks = 240;
			s.ctl = [r](int t, int) { Ctl c; c.jump = (t < 3) || (t >= 30 && t < 31); if (t >= 30 && t < 31) { c.pitch = -1.f; c.roll = (float)r; } return c; }; add(s);
		}
		// ---- a double jump into a hanging ball
		{ Scenario s; s.name = "jumphit_double"; s.cars = { baseCar(0, -750, HALF_PI, 1000) }; s.cars[0].boost = 0; s.ball.pos = { 0, 0, 260 }; s.ball.vel = { 0, 0, 300 }; s.ticks = 240;
		  s.ctl = [](int t, int) { Ctl c; c.jump = (t < 20) || (t >= 30 && t < 31); return c; }; add(s); }
		{ Scenario s; s.name = "air_pitch_up"; CarInit c; c.pos = { 0, 0, 400 }; c.boost = 0; c.onGround = false; c.hasJumped = true; s.cars = { c }; s.ball.pos = FarBall(); s.ticks = 60;
		  s.ctl = [](int, int) { Ctl k; k.pitch = 1; return k; }; add(s); }
		{ Scenario s; s.name = "air_roll_yaw_boost"; CarInit c; c.pos = { 0, -2000, 600 }; c.vel = { 0, 800, 200 }; Face(c, HALF_PI, 0.3f); c.boost = 100; c.onGround = false; c.hasJumped = true; s.cars = { c }; s.ball.pos = FarBall(); s.ticks = 120;
		  s.ctl = [](int t, int) { Ctl k; k.boost = true; k.roll = 1; k.yaw = (t > 40) ? -1.f : 0.f; k.pitch = (t > 80) ? 1.f : 0.f; return k; }; add(s); }

		// ---- symmetric kickoff race: both cars floor it from the standard diagonal 1v1 spawns; who touches first, and when?
		{ Scenario s; s.name = "kickoff_race_diag"; CarInit b = baseCar(-2048, -2560, 0.8961f, 0); b.team = 0; b.boost = 33.33333f; CarInit o = baseCar(2048, 2560, -2.2455f, 0); o.team = 1; o.boost = 33.33333f;
		  s.cars = { b, o }; s.ball.pos = { 0, 0, 93.15f }; s.ticks = 420;
		  s.ctl = [](int, int) { Ctl k; k.throttle = 1; k.boost = true; return k; }; add(s); }
		{ Scenario s; s.name = "kickoff_race_straight"; CarInit b = baseCar(0, -4608, HALF_PI, 0); b.team = 0; CarInit o = baseCar(0, 4608, -HALF_PI, 0); o.team = 1;
		  s.cars = { b, o }; s.ball.pos = { 0, 0, 93.15f }; s.ticks = 420;
		  s.ctl = [](int, int) { Ctl k; k.throttle = 1; k.boost = true; return k; }; add(s); }

		{ Scenario s; s.name = "kickoff_race_mixed"; CarInit b = baseCar(-2048, -2560, 0.8961f, 0); b.team = 0; CarInit o = baseCar(0, 4608, -HALF_PI, 0); o.team = 1;
		  s.cars = { b, o }; s.ball.pos = { 0, 0, 93.15f }; s.ticks = 420;
		  s.ctl = [](int, int) { Ctl k; k.throttle = 1; k.boost = true; return k; }; add(s); }
		// ---- a ball squeezed between two cars at different speeds (blue 1500 uu/s, orange 500 uu/s, arriving together): no summing of the two pushes
		{ Scenario s; s.name = "squeeze_unequal"; CarInit b = baseCar(0, -1700, HALF_PI, 1500); b.team = 0; b.boost = 0; CarInit o = baseCar(0, 600, -HALF_PI, 500); o.team = 1; o.boost = 0;
		  s.cars = { b, o }; s.ball.pos = { 0, 0, 93.15f }; s.ticks = 240; add(s); }
		{ Scenario s; s.name = "squeeze_equal"; CarInit b = baseCar(0, -1000, HALF_PI, 1000); b.team = 0; b.boost = 0; CarInit o = baseCar(0, 1000, -HALF_PI, 1000); o.team = 1; o.boost = 0;
		  s.cars = { b, o }; s.ball.pos = { 0, 0, 93.15f }; s.ticks = 240; add(s); }

		// ---- car vs walls, ceiling and corner (arena geometry: analytic SDF on the GPU vs collision meshes on the CPU)
		for (float sp : { 900.f, 1500.f }) {
			Scenario s; s.name = "car_side_wall_" + std::to_string((int)sp); CarInit c = baseCar(2500, 0, 0.f, sp); c.boost = 100; s.cars = { c }; s.ball.pos = { -3000, 3000, 93.15f }; s.ticks = 360;
			s.ctl = [](int, int) { Ctl k; k.throttle = 1; k.boost = true; return k; }; add(s);
		}
		{ Scenario s; s.name = "car_back_wall_ramp"; CarInit c = baseCar(0, -3000, -HALF_PI, 1200); c.boost = 100; s.cars = { c }; s.ball.pos = FarBall(); s.ticks = 300;
		  s.ctl = [](int, int) { Ctl k; k.throttle = 1; k.boost = true; return k; }; add(s); }
		{ Scenario s; s.name = "car_corner_ramp"; CarInit c = baseCar(2500, 3000, 0.7854f, 1400); c.boost = 100; s.cars = { c }; s.ball.pos = { -3000, -3000, 93.15f }; s.ticks = 360;
		  s.ctl = [](int, int) { Ctl k; k.throttle = 1; k.boost = true; return k; }; add(s); }
		{ Scenario s; s.name = "car_ceiling_hit"; CarInit c; c.pos = { 0, 0, 1300 }; c.vel = { 0, 200, 1400 }; Face(c, HALF_PI, 1.2f); c.boost = 100; c.onGround = false; c.hasJumped = true; s.cars = { c }; s.ball.pos = FarBall(); s.ticks = 240;
		  s.ctl = [](int, int) { Ctl k; k.boost = true; return k; }; add(s); }

		// ---- ball only
		auto ballOnly = [&](const char* name, Vec3f p, Vec3f v, Vec3f w, int ticks) { Scenario s; s.name = name; CarInit c; c.pos = { -2500, -4500, 17 }; s.cars = { c }; s.ball.pos = p; s.ball.vel = v; s.ball.angVel = w; s.ticks = ticks; add(s); };
		ballOnly("ball_drop_400", { 0, 0, 400 }, { 0, 0, -0.01f }, { 0, 0, 0 }, 360);
		ballOnly("ball_roll_1500", { -2000, 0, 93.15f }, { 1500, 0, 0 }, { 0, 0, 0 }, 360);
		ballOnly("ball_roll_1500_spin", { -2000, 0, 93.15f }, { 1500, 0, 0 }, { 0, 4, 0 }, 360);
		ballOnly("ball_side_wall", { 3000, 0, 500 }, { 2000, 0, 0 }, { 0, 0, 0 }, 240);
		ballOnly("ball_back_wall", { 0, 4500, 500 }, { 0, 2000, 300 }, { 0, 0, 0 }, 240);
		ballOnly("ball_ceiling", { 0, 0, 1500 }, { 300, 200, 1500 }, { 0, 0, 0 }, 300);
		ballOnly("ball_corner", { 3000, 4000, 600 }, { 1500, 1500, 0 }, { 0, 0, 0 }, 300);
		ballOnly("ball_goal_center", { 0, 4300, 250 }, { 0, 1800, 0 }, { 0, 0, 0 }, 300);
		ballOnly("ball_goal_post", { 950, 4300, 250 }, { 0, 1800, 0 }, { 0, 0, 0 }, 300);
		ballOnly("ball_goal_crossbar", { 0, 4300, 560 }, { 0, 1800, 120 }, { 0, 0, 0 }, 300);
		ballOnly("ball_goal_diagonal", { -600, 3800, 200 }, { 700, 1700, 0 }, { 0, 0, 0 }, 300);
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

		// ---- wall driving: starts on the side wall (up = -x), boosting along it, turning, jumping and dodging off it
		{
			auto wallCar = [](float y, float z, Vec3f fwd, float speed, float x = 4079.f) { CarInit c; c.pos = { x, y, z }; c.forward = fwd; c.up = { -1, 0, 0 }; c.vel = { 0, fwd.y * speed, fwd.z * speed }; c.boost = 100; return c; };
			auto wall = [&](const char* name, CarInit c, int ticks, std::function<Ctl(int, int)> f) { Scenario s; s.name = name; s.cars = { c }; s.ball.pos = { -3000, 3000, 93.15f }; s.ticks = ticks; s.ctl = f; add(s); };
			wall("wall_drive_level", wallCar(-2000, 600, { 0, 1, 0 }, 1400), 180, [](int, int) { Ctl k; k.throttle = 1; return k; });
			wall("wall_drive_boost", wallCar(-3000, 500, { 0, 1, 0 }, 1200), 180, [](int, int) { Ctl k; k.throttle = 1; k.boost = true; return k; });
			wall("wall_drive_up", wallCar(0, 400, { 0, 0, 1 }, 1100), 180, [](int, int) { Ctl k; k.throttle = 1; k.boost = true; return k; });
			wall("wall_drive_down", wallCar(0, 1500, { 0, 0, -1 }, 900), 240, [](int, int) { Ctl k; k.throttle = 1; return k; });
			wall("wall_turn", wallCar(-1500, 700, { 0, 1, 0 }, 1200), 180, [](int, int) { Ctl k; k.throttle = 1; k.steer = 0.6f; return k; });
			wall("wall_slide", wallCar(-1500, 900, { 0, 1, 0 }, 1500), 180, [](int t, int) { Ctl k; k.throttle = 1; k.steer = -1; k.handbrake = t < 90; return k; });
			wall("wall_coast_fall", wallCar(0, 1200, { 0, 1, 0 }, 500), 240, [](int, int) { return Ctl{}; });
			wall("wall_jump", wallCar(-1000, 700, { 0, 1, 0 }, 1200), 240, [](int t, int) { Ctl k; k.throttle = 1; k.jump = t >= 30 && t < 50; return k; });
			wall("wall_dodge", wallCar(-1000, 700, { 0, 1, 0 }, 1200), 240, [](int t, int) { Ctl k; k.throttle = 1; k.jump = (t >= 30 && t < 33) || (t >= 45 && t < 46); if (t >= 45 && t < 46) k.pitch = -1; return k; });
			for (int deg : { 30, 60, 90 }) { // drive from the floor up the side wall at different angles
				Scenario s; s.name = "wall_entry_" + std::to_string(deg); float yaw = HALF_PI * (1.0f - deg / 90.0f); CarInit c = baseCar(3000, -1500, yaw, 1300); c.boost = 100; s.cars = { c }; s.ball.pos = { -3000, 3000, 93.15f }; s.ticks = 240;
				s.ctl = [](int, int) { Ctl k; k.throttle = 1; k.boost = true; return k; }; add(s);
			}
		}
		// ---- car vs car: car 0 into car 1, opponents (opp) and teammates (mate), head-on / rear / side / angled, below and at supersonic
		for (int mate : { 0, 1 }) {
			struct K { const char* tag; float av, bv, byaw, bx, by; };
			for (K k : { K{ "slow_headon", 700, 700, (float)M_PI, 0, 0 }, K{ "med_rear", 1500, 500, 0, 0, 0 }, K{ "side", 1400, 0, HALF_PI, 0, 0 }, K{ "side_moving", 1400, 900, HALF_PI, 0, -260 }, K{ "angled", 1600, 800, 2.356f, 0, -200 }, K{ "offset_headon", 1100, 1100, (float)M_PI, 0, 60 }, K{ "super_headon", 2300, 1000, (float)M_PI, 0, 0 }, K{ "super_side", 2300, 0, HALF_PI, 0, 0 } }) {
				Scenario s; s.name = std::string(mate ? "mate_" : "opp_") + k.tag; CarInit a = baseCar(-900, -3500, 0.f, k.av); a.boost = k.av >= 2200 ? 100.f : 0.f; a.team = 0;
				CarInit b = baseCar(k.bx, -3500 + k.by, k.byaw, k.bv); b.boost = 0; b.team = mate ? 0 : 1; s.cars = { a, b }; s.ball.pos = FarBall(); s.ticks = 180;
				bool sup = k.av >= 2200;
				s.ctl = [sup](int, int car) { Ctl c; c.throttle = 1; if (car == 0 && sup) c.boost = true; return c; }; add(s);
			}
			{ Scenario s; s.name = std::string(mate ? "mate_" : "opp_") + "aerial"; CarInit a; a.pos = { 0, -800, 600 }; a.vel = { 0, 900, 100 }; Face(a, HALF_PI, 0.1f); a.onGround = false; a.hasJumped = true; a.boost = 0;
			  CarInit b; b.pos = { 40, 0, 620 }; b.vel = { 0, 0, 100 }; Face(b, 0.f); b.onGround = false; b.hasJumped = true; b.boost = 0; b.team = mate ? 0 : 1; s.cars = { a, b }; s.ball.pos = FarBall(); s.ticks = 150; add(s); }
		}
		{ Scenario s; s.name = "mate_three_pile"; CarInit a = baseCar(-900, -3500, 0.f, 1500); a.boost = 0; CarInit b = baseCar(0, -3500, HALF_PI, 0); b.boost = 0; CarInit c = baseCar(900, -3450, (float)M_PI, 1500); c.boost = 0; c.team = 1;
		  s.cars = { a, b, c }; s.ball.pos = FarBall(); s.ticks = 180; s.ctl = [](int, int) { Ctl k; k.throttle = 1; return k; }; add(s); }

		// ---- seeded random battery (held out: nothing is tuned to these). Controls change every 20 ticks.
		{
			auto rnd = [](unsigned& st, float lo, float hi) { st = st * 1664525u + 1013904223u; return lo + (hi - lo) * ((st >> 8) & 0xFFFFFF) / 16777215.0f; };
			auto randCtl = [](unsigned seed, int t, int car) { unsigned st = seed * 7919u + (unsigned)(t / 20) * 104729u + (unsigned)car * 31u; auto r = [&st]() { st = st * 1664525u + 1013904223u; return ((st >> 8) & 0xFFFFFF) / 16777215.0f; };
				r(); Ctl k; k.throttle = r() < 0.8f ? 1.f : r() * 2 - 1; k.steer = r() < 0.5f ? 0.f : r() * 2 - 1; k.pitch = r() * 2 - 1; k.yaw = k.steer; k.roll = r() < 0.7f ? 0.f : r() * 2 - 1; k.boost = r() < 0.4f; k.jump = r() < 0.2f; k.handbrake = r() < 0.1f; return k; };
			for (unsigned n = 0; n < 20; n++) { // random driving, jumping and flipping near the middle of the field
				unsigned st = 1000 + n; Scenario s; s.name = "rand_drive_" + std::to_string(n); CarInit c = baseCar(rnd(st, -2000, 2000), rnd(st, -2500, 2500), rnd(st, -3.14f, 3.14f), rnd(st, 0, 1800)); c.boost = 100; s.cars = { c }; s.ball.pos = FarBall(); s.ticks = 240;
				s.ctl = [randCtl, n](int t, int car) { return randCtl(1000 + n, t, car); }; add(s);
			}
			for (unsigned n = 0; n < 20; n++) { // random approach to the ball (ground and air)
				unsigned st = 2000 + n; Scenario s; s.name = "rand_hit_" + std::to_string(n); float yaw = rnd(st, -3.14f, 3.14f), d = rnd(st, 500, 900), sp = rnd(st, 700, 1800), off = rnd(st, -80, 80);
				bool air = (n % 2) == 1; float bz = air ? rnd(st, 500, 900) : 93.15f;
				CarInit c = baseCar(-d * std::cos(yaw) - off * std::sin(yaw), -d * std::sin(yaw) + off * std::cos(yaw), yaw, sp); c.boost = 0;
				if (air) { c.pos.z = bz + rnd(st, -90, 50); c.onGround = false; c.hasJumped = true; c.vel.z = rnd(st, 100, 300); }
				s.cars = { c }; s.ball.pos = { 0, 0, bz }; if (air) s.ball.vel = { 0, 0, rnd(st, 100, 300) }; s.ticks = 180;
				s.ctl = [](int, int) { Ctl k; k.throttle = 1; return k; }; add(s);
			}
			for (unsigned n = 0; n < 20; n++) { // random ball shots
				unsigned st = 3000 + n; Scenario s; s.name = "rand_ball_" + std::to_string(n); CarInit c; c.pos = { -2500, -4500, 17 }; s.cars = { c };
				s.ball.pos = { rnd(st, -3000, 3000), rnd(st, -4000, 4000), rnd(st, 150, 1500) }; s.ball.vel = { rnd(st, -2500, 2500), rnd(st, -2500, 2500), rnd(st, -1000, 1500) }; s.ball.angVel = { rnd(st, -3, 3), rnd(st, -3, 3), rnd(st, -3, 3) }; s.ticks = 360; add(s);
			}
			for (unsigned n = 0; n < 20; n++) { // random car-car meetings, alternating opponents and teammates
				unsigned st = 4000 + n; Scenario s; s.name = "rand_bump_" + std::to_string(n); float y1 = rnd(st, -3.14f, 3.14f), y2 = y1 + rnd(st, 1.0f, 5.2f), s1 = rnd(st, 600, 2000), s2 = rnd(st, 0, 1500), t = 0.5f, off = rnd(st, -60, 60);
				CarInit a = baseCar(-s1 * t * std::cos(y1), -2000 - s1 * t * std::sin(y1), y1, s1); a.boost = 0; CarInit b = baseCar(-s2 * t * std::cos(y2) + off, -2000 - s2 * t * std::sin(y2), y2, s2); b.boost = 0; b.team = n % 2 ? 0 : 1;
				s.cars = { a, b }; s.ball.pos = FarBall(); s.ticks = 150; s.ctl = [](int, int) { Ctl k; k.throttle = 1; return k; }; add(s);
			}
			for (unsigned n = 0; n < 20; n++) { // random wall and ramp runs
				unsigned st = 5000 + n; Scenario s; s.name = "rand_wall_" + std::to_string(n); float sign = (n % 2) ? 1.f : -1.f; CarInit c = baseCar(sign * rnd(st, 2200, 3200), rnd(st, -3000, 3000), (sign > 0 ? 0.f : (float)M_PI) + rnd(st, -1.0f, 1.0f), rnd(st, 800, 1900)); c.boost = 100;
				s.cars = { c }; s.ball.pos = FarBall(); s.ball.pos.x = -sign * 1000; s.ticks = 240;
				s.ctl = [n](int t, int) { Ctl k; k.throttle = 1; k.boost = (t / 40 + n) % 2 == 0; k.steer = (t >= 100 && t < 140) ? ((n % 3) - 1) * 0.5f : 0.f; k.jump = (n % 4 == 0) && t >= 150 && t < 160; return k; }; add(s);
			}
		}

		// ---- boost pads: big pad at (-3072,-4096), small pad at (-1792,-4184)
		{ Scenario s; s.name = "pad_big_pickup_respawn"; CarInit c = baseCar(-3500, -4096, 0.f, 600); c.boost = 0; s.cars = { c }; s.ball.pos = FarBall(); s.ticks = 1500;
		  s.ctl = [](int t, int) { Ctl k; k.throttle = (t < 200) ? 0.3f : 0.f; return k; }; add(s); }
		{ Scenario s; s.name = "pad_small_pickup"; CarInit c = baseCar(-2100, -4184, 0.f, 600); c.boost = 0; s.cars = { c }; s.ball.pos = FarBall(); s.ticks = 400;
		  s.ctl = [](int, int) { Ctl k; k.throttle = 0.3f; return k; }; add(s); }
		return out;
	}
}
