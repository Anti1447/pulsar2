#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CatchBallOnCarReward.h>
#include <RLGymCPP/Telemetry/CarryCatchTelemetry.h>

#include <cmath>

using namespace RLGC;
using namespace RewardTest;

// Audit of CatchBallOnCarReward, 2026-11-08. Scenario ladder and hand derivations:
// docs/reward_scenarios/CatchBallOnCarReward_2026-11-08.md (C1..C8 + the night re-audit).
// This file tests Rev 5 = Rev 4 (car-frame geometry, no clear/block payout) with the gates moved to the CAR-RELATIVE frame:
// incoming relative speed >= 400, relative slowdown >= 35%, no incoming-distance gate, exit relative speed <= 500.
// (Rev 3/4 measured these in the world frame and paid 0 of 864 engine drops; tests/test_catch_engine.cpp is the engine grid.)
// Fixture: fabricated states, no engine. Blue car at (0,0,17) facing +y on flat ground, moving at carVel on BOTH steps; the ball is
// described in the car frame (lf ahead, relZ above the car origin). `relIn` is the car-relative speed of the ball one step earlier
// (ball closing on the car, along -y relative to it). WHAT THIS DOES NOT SHOW: whether the engine reports ballTouchedStep on every
// step of a ball resting on the roof.

namespace {
	struct Hit {
		float relIn = 400;                 // |ballVel_prev - carVel_prev|, ball coming toward the car along -y (car frame)
		Vec prevBallPos = Vec(0, 600, 120);// where the ball was one step earlier
		float lf = 60, relZ = 103;         // ball position relative to the car origin now
		Vec ballVel = Vec(0, 600, 0);
		Vec carVel = Vec(0, 400, 0);       // default: relative exit speed 200, slowdown 0.5 -> an UNCAPPED pay of 7.5
		bool touch = true;
		bool withPrev = true;
		bool prevTouch = false;            // the player's touch flag on the previous step
		bool ballPrevOverride = false; Vec ballPrevVel;   // explicit previous ball velocity (carries)
	};

	void Build(GameState& prev, GameState& now, const Hit& h) {
		prev.deltaTime = now.deltaTime = STEP_DT;
		prev.ball.pos = h.prevBallPos;
		prev.ball.vel = h.ballPrevOverride ? h.ballPrevVel : h.carVel - Vec(0, h.relIn, 0);
		Player pp = MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 17));
		pp.vel = h.carVel; pp.ballTouchedStep = h.prevTouch;
		prev.players.push_back(pp);
		Player p = MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 17));
		p.rotMat.forward = Vec(0, 1, 0); p.rotMat.right = Vec(1, 0, 0); p.rotMat.up = Vec(0, 0, 1);
		p.vel = h.carVel; p.ballTouchedStep = h.touch;
		p.prev = &prev.players[0];
		now.players.push_back(p);
		now.players[0].prev = &prev.players[0];
		now.ball.pos = Vec(0, h.lf, 17 + h.relZ); now.ball.vel = h.ballVel;
		now.prev = h.withPrev ? &prev : nullptr;
	}

	float Pay(const Hit& h) {
		GameState prev, now;
		Build(prev, now, h);
		CatchBallOnCarReward r;
		r.Reset(now);
		return r.GetReward(now.players[0], now, false);
	}
	bool Slowing(const Hit& h) { GameState prev, now; Build(prev, now, h); return CarryCatchTelemetry::SlowingTouch(now.players[0], now); }
	bool Possession(const Hit& h) { GameState prev, now; Build(prev, now, h); return CarryCatchTelemetry::PossessionCatch(now.players[0], now); }

	// Ball leaves the touch at car velocity + `exitRel` along +y (relative speed |exitRel|); incoming relative speed `relIn`.
	Hit Catchish(float relIn, float exitRel, float carSpeed = 400) {
		Hit h; h.relIn = relIn; h.carVel = Vec(0, carSpeed, 0); h.ballVel = Vec(0, carSpeed + exitRel, 0); h.lf = 60; h.relZ = 124; return h;
	}

	// A carry step: car and ball move together at `a` -> `b` uu/s, ball on the hood; previous relative speed 0.
	Hit Carry(float a, float b, float lf = 70, float relZ = 124) {
		Hit h; h.lf = lf; h.relZ = relZ; h.ballVel = Vec(0, b, 0); h.carVel = Vec(0, a, 0);
		h.ballPrevOverride = true; h.ballPrevVel = Vec(0, a, 0);
		h.prevBallPos = Vec(0, lf, 17 + relZ); // the ball was already on the car
		return h;
	}

	// A persistent reward over a scripted sequence (cooldown tests). Each frame: ball pos/vel, touch flag. Car fixed at (0,0,17), vel (0,400,0).
	struct Fr { Vec pos; Vec vel; bool touch; };
	std::vector<float> Seq(const std::vector<Fr>& frames) {
		std::vector<GameState> st(frames.size());
		for (size_t i = 0; i < frames.size(); i++) {
			st[i].deltaTime = STEP_DT;
			Player p = MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 17));
			p.rotMat.forward = Vec(0, 1, 0); p.rotMat.right = Vec(1, 0, 0); p.rotMat.up = Vec(0, 0, 1);
			p.vel = Vec(0, 400, 0); p.ballTouchedStep = frames[i].touch;
			st[i].players.push_back(p);
			st[i].ball.pos = frames[i].pos; st[i].ball.vel = frames[i].vel;
		}
		for (size_t i = 1; i < st.size(); i++) { st[i].prev = &st[i - 1]; st[i].players[0].prev = &st[i - 1].players[0]; }
		CatchBallOnCarReward r;
		r.Reset(st[0]);
		std::vector<float> out;
		for (auto& s : st) out.push_back(r.GetReward(s.players[0], s, false));
		return out;
	}
	// ball incoming (relative speed 1400), then a clean hood catch (relative exit 36 -> slowdown 0.974 -> capped at 8)
	const Fr INCOMING = { Vec(0, 600, 120), Vec(0, -1000, 0), false };
	const Fr CATCH    = { Vec(0, 60, 120),  Vec(0, 420, 30),  true  };
	const Fr RESTING  = { Vec(0, 60, 120),  Vec(0, 400, 0),   false };       // ball on the car, no new touch flag
	const Fr FAR_IDLE = { Vec(0, 900, 120), Vec(0, 0, 0),     false };       // ball >= 300 uu from the car
}

TEST(Catch_C1_clean_hood_catch) {
	// relIn 1400, exit rel 36 -> slowdown 0.974; base 4.87 + carry 4 + 1 = 9.87 -> capped at 8
	Hit h = Catchish(1400, 36); h.ballVel = Vec(0, 420, 30);
	CHECK_NEAR(Pay(h), 8.0f, 1e-5f);
	// uncapped reference: relIn 400, exit rel 200 -> slowdown 0.5, quality 1: 2.5 + 4 + 1 = 7.5
	CHECK_NEAR(Pay(Hit()), 7.5f, 0.01f);
}

TEST(Catch_rev5_incoming_relative_speed_floor) {
	// floor 400 uu/s of RELATIVE speed (0.9x / 1.1x); the exit is fixed at rel 100 so the slowdown gate is always passed
	CHECK_NEAR(Pay(Catchish(360, 100)), 0.0f, 1e-6f);
	CHECK(Pay(Catchish(440, 100)) > 0.0f);
	// the ball's WORLD speed no longer matters: ball and car both at 2300 uu/s, ball 600 uu/s faster than the car one step earlier
	Hit fast = Catchish(600, 50, 2300);
	CHECK(Pay(fast) > 0.0f);
	// ...and a fast ball that is NOT closing on the car (rel 0) pays nothing
	Hit chase = Catchish(0, 0, 1500); CHECK_NEAR(Pay(chase), 0.0f, 1e-6f);
}

TEST(Catch_rev5_a_ball_moving_with_the_car_that_gets_sped_up_still_counts) {
	// the audit's gate-2 finding: car 1400, ball 700 (rel 700 closing as the car drives under it); the car accelerates the ball to car + 100.
	// World-frame slowdown was NEGATIVE (700 -> 1500); relative slowdown is (700-100)/700 = 0.857.
	Hit h = Catchish(700, 100, 1400);
	h.ballPrevOverride = true; h.ballPrevVel = Vec(0, 700, 0);
	CHECK(Pay(h) > 0.0f);
	CHECK(Slowing(h)); CHECK(Possession(h));
}

TEST(Catch_fix4_C3_roof_centre_now_pays_and_the_cliff_is_gone) {
	// Rev 2/3: lf 30 -> 0, lf 59.5 -> 6.92 (world-frame frontAlign cliff at lf = 54). Rev 4+: car-frame band, smooth.
	Hit h = Catchish(400, 200); h.relZ = 124;              // slowdown 0.5, relSpeed 200
	// quality 1 on the roof/hood (lf -10..110): 5 x 0.5 x 1.0 + 4 + 1 = 7.5
	for (float lf : { 30.0f, 48.7f, 53.0f, 55.0f, 59.5f, 100.0f }) { h.lf = lf; CHECK_NEAR(Pay(h), 7.5f, 0.01f); }
	Hit lo = h; lo.lf = 53.0f; Hit hi = h; hi.lf = 55.0f;
	CHECK(fabsf(Pay(hi) - Pay(lo)) < 0.05f);
	// rear edge: lf -60 -> fwdQuality (−60 + 70) / 60 = 0.1667; pay 5 x 0.5 x 0.6667 + 4 x 0.5833 + 1 = 5.0
	h.lf = -60; CHECK_NEAR(Pay(h), 5.0f, 0.02f);
	// front edge: lf 150 -> fwdQuality 0; pay 5 x 0.5 x 0.6 + 4 x 0.5 + 1 = 4.5
	h.lf = 150; CHECK_NEAR(Pay(h), 4.5f, 0.02f);
	// band edges at 0.9x / 1.1x
	h.lf = -77; CHECK_NEAR(Pay(h), 0.0f, 1e-6f);
	h.lf = 165; CHECK_NEAR(Pay(h), 0.0f, 1e-6f);
	h.lf = 60;  h.relZ = 90;  CHECK_NEAR(Pay(h), 0.0f, 1e-6f);   // lu 90 < 100
	h.relZ = 253; CHECK_NEAR(Pay(h), 0.0f, 1e-6f);               // lu 253 > 230
}

TEST(Catch_fix4_lateral_offset_is_gated_and_scales_the_pay) {
	// ball 40 uu to the side: centering 0.4667; base 5 x 0.5 x (0.6 + 0.1867) = 1.9667; carry 4 x 0.7333 = 2.9333; top 1 -> 5.9
	GameState prev, now; Hit h = Catchish(400, 200); h.lf = 60;
	Build(prev, now, h);
	now.ball.pos.x = 40.0f;
	CatchBallOnCarReward r; r.Reset(now);
	CHECK_NEAR(r.GetReward(now.players[0], now, false), 5.9f, 0.02f);
	GameState prev2, now2; Build(prev2, now2, h);
	now2.ball.pos.x = 82.5f;                                              // 1.1 x the 75 uu gate
	CatchBallOnCarReward r2; r2.Reset(now2);
	CHECK_NEAR(r2.GetReward(now2.players[0], now2, false), 0.0f, 1e-6f);
}

TEST(Catch_fix5_C4_a_clear_pays_nothing) {
	// Rev 2/3: 4.75 (a nose clear). Rev 4+: lu 50 < 100, not on the car.
	Hit h = Catchish(2500, 200, 1200); h.lf = 100; h.relZ = 50;
	CHECK_NEAR(Pay(h), 0.0f, 1e-6f);
	// a hit that ends over the hood but sends the ball away fast: relative 700 > 500 and faster than the car by 700
	Hit k = Catchish(3500, 700, 1200); k.lf = 100;
	CHECK(Slowing(k));                                    // 3500 -> 700 relative is a slowing touch...
	CHECK_NEAR(Pay(k), 0.0f, 1e-6f);                      // ...but not a catch
}

TEST(Catch_rev5_exit_speed_boundaries) {
	// car 1200 uu/s; ball exits at car + d. Gates: relSpeed <= 500 and ball speed <= car speed + 250.
	auto pay = [](float d) { Hit h = Catchish(3000, d, 1200); h.lf = 70; return Pay(h); };
	CHECK(pay(0) > 0.0f);
	CHECK(pay(225.0f) > 0.0f);                            // 0.9 x 250 over the car
	CHECK_NEAR(pay(275.0f), 0.0f, 1e-6f);                 // 1.1 x 250 over the car (the excess gate binds before rel 500)
	CHECK(pay(-450.0f) > 0.0f);                           // slower than the car by 450 (rel 450 <= 500)
	CHECK_NEAR(pay(-550.0f), 0.0f, 1e-6f);                // 1.1 x 500 relative speed
	// carry-bonus tiers: rel < 250 -> 4, 250..500 -> 2 (position quality 1)
	Hit a = Catchish(600, 200); a.lf = 70;                // slowdown 0.6667
	CHECK_NEAR(Pay(a), 8.0f, 1e-5f);                      // 3.33 + 4 + 1 = 8.33 -> cap 8
	Hit b = Catchish(800, 300, 1200); b.ballVel = Vec(0, 900, 0); b.lf = 70;                // rel 300, slowdown 0.625
	CHECK_NEAR(Pay(b), 5.0f * 0.625f + 2.0f + 1.0f, 0.01f);              // 6.125
	Hit c = Catchish(800, 450, 1200); c.ballVel = Vec(0, 750, 0); c.lf = 70;                // rel 450 (a bounce the old 350 gate rejected), slowdown 0.4375
	CHECK_NEAR(Pay(c), 5.0f * 0.4375f + 2.0f + 1.0f, 0.01f);            // 5.1875
}

TEST(Catch_fix1_C5_a_carry_that_dips_pays_nothing) {
	CHECK_NEAR(Pay(Carry(1500, 1498)), 0.0f, 1e-6f);      // Rev 2: 5.337 per step. Now: previous relative speed 0 < 400
}

TEST(Catch_fix1_C5b_carry_spam_is_gone) {
	float total = 0;
	for (int k = 0; k < 20; k++) {
		Hit h = Carry(1500 - 2.0f * k, 1500 - 2.0f * (k + 1));
		h.prevTouch = (k > 0);
		total += Pay(h);
	}
	CHECK_NEAR(total, 0.0f, 1e-6f);
	// even a hard brake of the car under a ball already resting on it pays nothing: relative speed was 0, it was not incoming
	CHECK_NEAR(Pay(Carry(1600, 1100)), 0.0f, 1e-6f);
}

TEST(Catch_fix1_rising_edge) {
	Hit h;                                  // a clean catch...
	CHECK(Pay(h) > 7.0f);
	h.prevTouch = true;                     // ...is nothing if the player was already touching the ball one step earlier
	CHECK_NEAR(Pay(h), 0.0f, 1e-6f);
}

TEST(Catch_fix1_C6_boundary_is_now_the_real_gate) {
	CHECK_NEAR(Pay(Carry(1500, 1510)), 0.0f, 1e-6f);
	CHECK_NEAR(Pay(Carry(1500, 1499.99f)), 0.0f, 1e-6f);
}

TEST(Catch_rev5_relative_slowdown_boundary) {
	// relIn 700, car 400: slowdown gate 35% -> exit rel 455 exactly. 0.9x / 1.1x of the 35% (rel exit 479.5 / 430.5, both under the 500 exit gate)
	// car 1200, ball ends SLOWER than the car (a drop back onto it) so the car-speed + 250 cap is not the binding gate
	auto pay = [](float frac) { Hit h = Catchish(700, 0, 1200); h.ballVel = Vec(0, 1200 - 700 * (1.0f - frac), 0); return Pay(h); };
	CHECK_NEAR(pay(0.315f), 0.0f, 1e-6f);
	CHECK(pay(0.385f) > 0.0f);
}

TEST(Catch_rev5_incoming_distance_no_longer_matters) {
	// Rev 3/4 required the ball >= 250 uu from the car on the previous step (needs ~1800 uu/s closing at 15 Hz). Now a gentle catch
	// whose ball was only 170 uu away (or even at 0) pays: relative incoming speed + rising edge define "new contact".
	auto pay = [](float d) { Hit h; h.prevBallPos = Vec(0, 0, 17 + d); return Pay(h); };
	CHECK_NEAR(pay(170.0f), 7.5f, 0.01f);
	CHECK_NEAR(pay(250.0f), 7.5f, 0.01f);
	CHECK_NEAR(pay(0.0f), 7.5f, 0.01f);
}

TEST(Catch_fix2_cooldown_blocks_the_second_catch_for_23_steps) {
	std::vector<Fr> f = { INCOMING, CATCH };
	for (int i = 0; i < 6; i++) f.push_back(RESTING);
	f.push_back(INCOMING); f.push_back(CATCH);            // frames 8, 9
	auto v = Seq(f);
	CHECK_NEAR(v[1], 8.0f, 0.01f);
	CHECK_NEAR(v[9], 0.0f, 1e-6f);
}

TEST(Catch_fix2_cooldown_expires_exactly_after_23_blocked_steps_and_rearms_on_distance) {
	// Payment at frame 1. Frame j after it is blocked while 1.5 - (j-1)*dt > 0, i.e. for j = 1..23; free from j = 24.
	for (int gap : { 22, 23, 24 }) {
		std::vector<Fr> f = { INCOMING, CATCH };
		for (int i = 0; i < gap - 2; i++) f.push_back(i == 0 ? FAR_IDLE : RESTING);   // frame 2 is far from the car: re-arms
		f.push_back(INCOMING);
		f.push_back(CATCH);
		auto v = Seq(f);
		int j = (int)v.size() - 1 - 1;
		float p = v.back();
		if (j <= 23) CHECK_NEAR(p, 0.0f, 1e-6f); else CHECK(p > 7.0f);
	}
}

TEST(Catch_fix2_rearm_requires_the_ball_to_leave_300_uu) {
	// Cooldown long expired (40 steps), but the ball never got >= 300 uu from the car since the paid catch; the next 'incoming' frame is
	// 298 uu out (fails the 300 re-arm). Blocked; with a far excursion first it pays.
	std::vector<Fr> f = { INCOMING, CATCH };
	for (int i = 0; i < 40; i++) f.push_back(RESTING);
	f.push_back({ Vec(0, 280, 120), Vec(0, -1000, 0), false });
	f.push_back(CATCH);
	CHECK_NEAR(Seq(f).back(), 0.0f, 1e-6f);
	std::vector<Fr> g = { INCOMING, CATCH };
	for (int i = 0; i < 40; i++) g.push_back(i == 5 ? FAR_IDLE : RESTING);
	g.push_back({ Vec(0, 280, 120), Vec(0, -1000, 0), false });
	g.push_back(CATCH);
	CHECK(Seq(g).back() > 7.0f);
}

TEST(Catch_fix2_a_gated_out_touch_does_not_burn_the_cooldown) {
	std::vector<Fr> f = { { Vec(0, 60, 120), Vec(0, 400, 0), true }, { Vec(0, 60, 120), Vec(0, 399, 0), true }, INCOMING, CATCH };
	auto v = Seq(f);
	CHECK_NEAR(v[1], 0.0f, 1e-6f);
	CHECK_NEAR(v.back(), 8.0f, 0.01f);
}

TEST(Catch_Reset_clears_the_cooldown) {
	std::vector<GameState> st(4);
	std::vector<Fr> f = { INCOMING, CATCH, INCOMING, CATCH };
	for (size_t i = 0; i < f.size(); i++) {
		st[i].deltaTime = STEP_DT;
		Player p = MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 17));
		p.rotMat.forward = Vec(0, 1, 0); p.rotMat.right = Vec(1, 0, 0); p.rotMat.up = Vec(0, 0, 1);
		p.vel = Vec(0, 400, 0); p.ballTouchedStep = f[i].touch;
		st[i].players.push_back(p); st[i].ball.pos = f[i].pos; st[i].ball.vel = f[i].vel;
	}
	for (size_t i = 1; i < 4; i++) { st[i].prev = &st[i - 1]; st[i].players[0].prev = &st[i - 1].players[0]; }
	CatchBallOnCarReward r;
	r.Reset(st[0]);
	r.GetReward(st[0].players[0], st[0], false);
	CHECK(r.GetReward(st[1].players[0], st[1], false) > 7.0f);
	r.Reset(st[2]);
	r.GetReward(st[2].players[0], st[2], false);
	CHECK(r.GetReward(st[3].players[0], st[3], false) > 7.0f);
}

TEST(Catch_C7_a_slow_carry_pays_nothing_now) {
	CHECK_NEAR(Pay(Carry(620, 610)), 0.0f, 1e-6f);
	CHECK_NEAR(Pay(Carry(590, 580)), 0.0f, 1e-6f);
}

TEST(Catch_C8_gates_that_return_zero) {
	Hit noTouch; noTouch.touch = false;
	CHECK_NEAR(Pay(noTouch), 0.0f, 1e-6f);
	Hit noPrev; noPrev.withPrev = false;
	CHECK_NEAR(Pay(noPrev), 0.0f, 1e-6f);
	Hit behind; behind.lf = -100;
	CHECK_NEAR(Pay(behind), 0.0f, 1e-6f);
}

TEST(Catch_pay_is_capped_at_8) {
	Hit h = Catchish(3000, 0, 100); h.lf = 70;            // slowdown 1.0: 5 + 4 + 1 = 10 -> 8
	CHECK_NEAR(Pay(h), 8.0f, 1e-5f);
}

// ---------------------------------------------------------------------------------------------------------------------
// Telemetry predicates (Telemetry/CarryCatchTelemetry.h): independent of the reward's pay geometry.
// ---------------------------------------------------------------------------------------------------------------------

TEST(Catch_telemetry_slowing_touch_counts_clears_and_traps_alike) {
	CHECK(Slowing(Hit()));
	Hit clear = Catchish(2500, 200, 1200); clear.lf = 100; clear.relZ = 50;
	CHECK(Slowing(clear));                                // a clear is a slowing touch...
	CHECK(!Possession(clear));                            // ...but the ball is not on the car (height 50 < 100)
	CHECK(Possession(Hit()));                             // the hood catch is both
}

TEST(Catch_telemetry_possession_needs_the_ball_on_the_car) {
	Hit roof = Catchish(400, 200); roof.lf = 30;
	CHECK(Possession(roof));
	CHECK_NEAR(Pay(roof), 7.5f, 0.01f);
	Hit bounce = Catchish(1000, 600);                     // slows 40% but leaves the car at 600 uu/s relative: a rebound, not a catch
	CHECK(Slowing(bounce)); CHECK(!Possession(bounce));
}

TEST(Catch_telemetry_possession_catch_is_exactly_what_the_reward_pays) {
	// reward > 0  <=>  PossessionCatch, on a grid over incoming speed, exit, car speed, distance, edge, geometry (fresh reward per point).
	int pays = 0, total = 0;
	for (float relIn : { 300.0f, 450.0f, 900.0f, 2500.0f })
	for (float exitRel : { 20.0f, 200.0f, 480.0f, 700.0f })
	for (float carV : { 100.0f, 400.0f, 1300.0f })
	for (float d : { 100.0f, 300.0f, 600.0f })
	for (bool pt : { false, true })
	for (float lf : { -80.0f, 30.0f, 140.0f, 160.0f })
	for (float relZ : { 80.0f, 124.0f, 250.0f }) {
		Hit h = Catchish(relIn, exitRel, carV);
		h.ballVel = Vec(0, carV + exitRel, 30);
		h.prevBallPos = Vec(0, 0, 17 + d); h.prevTouch = pt; h.lf = lf; h.relZ = relZ;
		bool pays_ = Pay(h) > 0.0f;
		CHECK_EQ(pays_, Possession(h));
		total++; if (pays_) pays++;
	}
	CHECK(pays > 30);
	CHECK(pays < total - 30);
}
