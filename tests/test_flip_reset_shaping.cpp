#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/PlayerBall/FlipResetSetupReward.h>
#include <RLGymCPP/Rewards/PlayerBall/FlipResetApproachReward.h>

#include <cmath>
#include <vector>

using namespace RLGC;
using namespace RewardTest;

// FlipResetSetupReward / FlipResetApproachReward (2026-11-09). Design record and hand derivations:
// docs/reward_scenarios/FlipResetShaping_2026-11-09.md. Fabricated states, no engine: this proves the logic, not that the bot learns it.

namespace {
	struct Fr {
		Vec ballPos, ballVel;
		Vec carPos = Vec(2700, 3000, 17), carVel = Vec(0, 0, 0);
		Vec up = Vec(0, 0, 1);
		bool ground = true, touch = false, handbrake = false, oppTouch = false;
		float boost = 60;
		const std::vector<BallState>* path = nullptr;
	};
	Fr Setup(float x = 3000, float y = 3000, Vec vel = Vec(1200, 0, 0)) {
		Fr f; f.ballPos = Vec(x, y, 93); f.ballVel = vel; f.carPos = Vec(x - 300, y, 17); f.touch = true; return f;
	}

	// Runs both rewards over the frames for player 0 (blue); an idle orange opponent is player 1.
	struct Out { std::vector<float> setup, approach; };
	Out Run(const std::vector<Fr>& fr, Team team = Team::BLUE) {
		std::vector<GameState> st(fr.size());
		for (size_t i = 0; i < fr.size(); i++) {
			st[i].deltaTime = STEP_DT;
			Player p = MakePlayer(1, 0, team, fr[i].carPos);
			p.vel = fr[i].carVel; p.boost = fr[i].boost; p.isOnGround = fr[i].ground; p.ballTouchedStep = fr[i].touch;
			p.rotMat.forward = Vec(0, 1, 0); p.rotMat.right = Vec(1, 0, 0); p.rotMat.up = fr[i].up;
			p.prevAction.handbrake = fr[i].handbrake ? 1.f : 0.f;
			Player o = MakePlayer(2, 1, team == Team::BLUE ? Team::ORANGE : Team::BLUE, Vec(0, -4000, 17));
			o.ballTouchedStep = fr[i].oppTouch;
			st[i].players.push_back(p); st[i].players.push_back(o);
			st[i].ball.pos = fr[i].ballPos; st[i].ball.vel = fr[i].ballVel; st[i].ballPred = fr[i].path;
		}
		for (size_t i = 1; i < st.size(); i++) { st[i].prev = &st[i - 1]; for (int k = 0; k < 2; k++) st[i].players[k].prev = &st[i - 1].players[k]; }
		FlipResetSetupReward a; FlipResetApproachReward b;
		a.Reset(st[0]); b.Reset(st[0]);
		Out out;
		for (auto& s : st) { out.setup.push_back(a.GetReward(s.players[0], s, false)); out.approach.push_back(b.GetReward(s.players[0], s, false)); }
		return out;
	}
	float SetupPay(const Fr& f) { return Run({ f, f }).setup[1]; }
}

// ------------------------------- Setup -------------------------------

TEST(FRS_setup_side_wall_pay_and_boost_ramp) {
	// ball (3000,3000) vel +1200 x: dSide 1096, closing 1.0, near (1800-1096)/1200 = 0.5867 -> 0.05 x 0.5867 = 0.02933 at boost >= 50
	CHECK_NEAR(SetupPay(Setup()), 0.02933f, 0.0002f);
	Fr f = Setup(); f.boost = 25; CHECK_NEAR(SetupPay(f), 0.02933f * 0.5f, 0.0002f);
	f.boost = 0;    CHECK_NEAR(SetupPay(f), 0.02933f * 0.2f, 0.0002f);   // floor 0.2: still a gradient to collect boost
	f.boost = 100;  CHECK_NEAR(SetupPay(f), 0.02933f, 0.0002f);
	f.boost = 50;   CHECK_NEAR(SetupPay(f), 0.02933f, 0.0002f);
}

TEST(FRS_setup_backboard_corner_and_goal_mouth) {
	// backboard: ball (1500,4500) vel +1200 y: dBack 620 -> near 0.9833 -> 0.04917
	Fr b = Setup(1500, 4500, Vec(0, 1200, 0)); CHECK_NEAR(SetupPay(b), 0.04917f, 0.0003f);
	// goal mouth: |x| 500 < 993, a shot not a setup
	Fr m = Setup(500, 4500, Vec(0, 1200, 0)); CHECK_NEAR(SetupPay(m), 0.0f, 1e-6f);
	// corner: both walls near, takes the better of the two
	Fr c = Setup(3500, 4500, Vec(900, 900, 0)); CHECK(SetupPay(c) > SetupPay(Setup(3500, 4500, Vec(900, 0, 0))) - 1e-6f);
}

TEST(FRS_setup_gates) {
	Fr f = Setup(); f.ballVel = Vec(0, 0, 0);            CHECK_NEAR(SetupPay(f), 0.f, 1e-6f);   // hovering at the wall pays nothing
	f = Setup(); f.ballVel = Vec(-1200, 0, 0);           CHECK_NEAR(SetupPay(f), 0.f, 1e-6f);   // moving away
	f = Setup(); f.ballVel = Vec(250, 0, 0);             CHECK_NEAR(SetupPay(f), 0.f, 1e-6f);   // under the 300 uu/s closing floor
	f = Setup(3000, -3000);                              CHECK_NEAR(SetupPay(f), 0.f, 1e-6f);   // defensive half
	f = Setup(); f.ballPos.z = 400;                      CHECK_NEAR(SetupPay(f), 0.f, 1e-6f);   // not ground control
	f = Setup(); f.ground = false;                       CHECK_NEAR(SetupPay(f), 0.f, 1e-6f);   // car airborne
	f = Setup(); f.carPos = Vec(2300, 3000, 17);         CHECK_NEAR(SetupPay(f), 0.f, 1e-6f);   // 700 uu away: not controlling
	f = Setup(); f.ballPos = Vec(1500, 3000, 93);        CHECK_NEAR(SetupPay(f), 0.f, 1e-6f);   // 2596 uu from the side wall (> 1800)
}

TEST(FRS_setup_touch_recency_8_steps) {
	// touch, then n steps without a touch: pays through n = 8, not at 9
	for (int n : { 7, 8, 9 }) {
		std::vector<Fr> v = { Setup() };
		for (int i = 0; i < n; i++) { Fr f = Setup(); f.touch = false; v.push_back(f); }
		float last = Run(v).setup.back();
		if (n <= 8) CHECK(last > 0.f); else CHECK_NEAR(last, 0.f, 1e-6f);
	}
}

TEST(FRS_setup_orange_mirror_matches_blue) {
	Fr b = Setup(); b.boost = 60;
	Fr o = b; o.ballPos = Vec(-3000, -3000, 93); o.ballVel = Vec(-1200, 0, 0); o.carPos = Vec(-2700, -3000, 17);
	CHECK_NEAR(Run({ o, o }, Team::ORANGE).setup[1], Run({ b, b }).setup[1], 1e-6f);
}

TEST(FRS_setup_bucket_bounds_the_income) {
	// a perfect setup every step (raw 0.05): the bucket (1.0) plus 0.1/s refill is all that can be paid
	Fr f = Setup(1500, 4520, Vec(0, 1200, 0)); f.carPos = Vec(1500, 4220, 17);
	std::vector<Fr> v(101, f);
	float total = 0; for (float x : Run(v).setup) total += x;
	CHECK(total < 1.0f + 0.1f * 100 * STEP_DT + 0.01f);   // <= 1.667
	CHECK(total > 1.6f);
	// a second 30 s later pays again (bucket refilled)
	std::vector<Fr> w(451, f);
	float t2 = 0; for (float x : Run(w).setup) t2 += x;
	CHECK(t2 < 1.0f + 0.1f * 450 * STEP_DT + 0.01f);      // <= 4.0
}

// ------------------------------- Approach -------------------------------

namespace {
	// setup frame, then the ball goes airborne past apex with the car airborne below it
	std::vector<Fr> Armed(const Fr& air) { std::vector<Fr> v = { Setup(), Setup() }; v.push_back(air); return v; }
	Fr Air(Vec ballPos, Vec ballVel, Vec carPos, Vec carVel, Vec up = Vec(0, 0, 1)) {
		Fr f; f.ballPos = ballPos; f.ballVel = ballVel; f.carPos = carPos; f.carVel = carVel; f.up = up; f.ground = false; f.touch = false; return f;
	}
}

TEST(FRA_not_armed_without_a_setup) {
	Fr a = Air(Vec(3000, 3000, 900), Vec(0, 0, -100), Vec(3000, 2600, 700), Vec(0, 0, -100));
	Fr b = Air(Vec(3000, 3000, 900), Vec(0, 0, -100), Vec(3000, 2650, 720), Vec(0, 0, -100));
	for (float x : Run({ a, b }).approach) CHECK_NEAR(x, 0.f, 1e-6f);
}

TEST(FRA_closing_underside_and_speed_terms) {
	// ball (3000,3000,900) vel z -100: intercept t = 0.2308 -> P z = 859.6. car (3000,2600,700) -> (3000,2650,720).
	// dPrev 430.6, dNow 376.8, progress 53.8/1500 = 0.0359 (roof up, no underside term; same velocity: no speed term)
	Fr prev = Air(Vec(3000, 3000, 900), Vec(0, 0, -100), Vec(3000, 2600, 700), Vec(0, 0, -100));
	Fr now = Air(Vec(3000, 3000, 900), Vec(0, 0, -100), Vec(3000, 2650, 720), Vec(0, 0, -100));
	std::vector<Fr> v = { Setup(), Setup(), prev, now };
	CHECK_NEAR(Run(v).approach[3], 0.0359f, 0.001f);
	// wheels toward the ball (car upside down relative to the ball direction): + 0.02 x 0.457 x 0.677 = 0.00619
	now.up = Vec(0, 0, -1); v = { Setup(), Setup(), prev, now };
	CHECK_NEAR(Run(v).approach[3], 0.0359f + 0.00619f, 0.001f);
	// moving away from the intercept: negative (two-sided)
	Fr away = Air(Vec(3000, 3000, 900), Vec(0, 0, -100), Vec(3000, 2550, 680), Vec(0, 0, -100));
	std::vector<Fr> w = { Setup(), Setup(), prev, away };
	CHECK(Run(w).approach[3] < 0.f);
}

TEST(FRA_speed_match_prefers_car_slightly_faster_in_z) {
	// same position both runs, only the car vz differs: ball vz -100; car vz +100 (rel z 200 = peak) vs -100 (rel 0) vs +500 (rel 600)
	auto at = [](float carVz, float carVx = 0) {
		Fr prev = Air(Vec(3000, 3000, 900), Vec(0, 0, -100), Vec(3000, 2650, 720), Vec(carVx, 0, carVz));
		Fr now = prev;     // identical position: no closing progress
		return Run({ Setup(), Setup(), prev, now }).approach[3];
	};
	float peak = at(100), same = at(-100), fast = at(500), side = at(100, 400);
	// peak: 0.02 x 1 x 1 x prox 0.677 = 0.01354 (+ closing 0)
	CHECK_NEAR(peak, 0.01354f, 0.0006f);
	CHECK_NEAR(same, 0.f, 1e-5f);        // the ball "sticks": no reward for identical velocity
	CHECK_NEAR(fast, 0.f, 1e-5f);        // rel z 600 > 500
	CHECK(side < peak);                  // horizontal mismatch reduces it
}

TEST(FRA_no_pay_before_apex_but_armed) {
	Fr pre = Air(Vec(3000, 3000, 900), Vec(0, 0, 300), Vec(3000, 2650, 720), Vec(0, 0, 100));
	Fr post = Air(Vec(3000, 3000, 900), Vec(0, 0, -100), Vec(3000, 2650, 720), Vec(0, 0, 100));
	auto r = Run({ Setup(), Setup(), pre, pre, post });
	CHECK_NEAR(r.approach[2], 0.f, 1e-6f);
	CHECK_NEAR(r.approach[3], 0.f, 1e-6f);
	CHECK(fabsf(r.approach[4]) > 0.f);   // still armed once past apex
}

namespace {
	// ideal contact: ball (3000,3000,900) prevVz -250; car 100 below and 100 behind (away from the +y goal), wheels toward the ball
	std::vector<Fr> Contact(float preVz, Vec carPos, Vec up, bool hb = false) {
		Fr pre = Air(Vec(3000, 3000, 920), Vec(0, 0, preVz), Vec(3000, 2850, 760), Vec(0, 0, 0));
		Fr hit = Air(Vec(3000, 3000, 900), Vec(0, 0, 200), carPos, Vec(0, 0, 0), up);
		hit.touch = true; hit.handbrake = hb;
		return { Setup(), Setup(), pre, hit };
	}
	const Vec WHEELS_UP = Vec(0, -0.7071f, -0.7071f);
}

TEST(FRA_contact_bonus_hand_values) {
	Vec behind45(3000, 2900, 800);
	// vzFrac 1, elev -45 deg (1), away: car (0,-100) vs goal dir (-.7071,.7071): 0.7071/0.866 = 0.8165 -> 1 x 1 x (0.5 + 0.4082) = 0.9082
	CHECK_NEAR(Run(Contact(-250, behind45, WHEELS_UP)).approach[3], 0.9082f, 0.002f);
	CHECK_NEAR(Run(Contact(-250, behind45, WHEELS_UP, true)).approach[3], 0.9082f * 1.25f, 0.003f);   // handbrake x1.25
	// vz window: -100 -> triangle 200/350 = 0.5714; -700 and +200 -> 0
	CHECK_NEAR(Run(Contact(-100, behind45, WHEELS_UP)).approach[3], 0.9082f * 0.5714f, 0.003f);
	CHECK_NEAR(Run(Contact(-700, behind45, WHEELS_UP)).approach[3], 0.f, 1e-6f);
	CHECK_NEAR(Run(Contact(200, behind45, WHEELS_UP)).approach[3], 0.f, 1e-6f);
	// car only 15 deg below the ball centre: elevFrac 0 -> 0.4 x 0.9082
	Vec flat(3000, 2900 - 273.2f + 100, 900 - 100 * 0.0f - 0.0f);   // placeholder replaced below
	float rr = 100.f; Vec shallow(3000, 3000 - rr * cosf(15.f * 3.14159265f / 180.f), 900 - rr * sinf(15.f * 3.14159265f / 180.f));
	float away15 = 0.7071f;  // same horizontal bearing relative to the goal
	(void)flat; (void)away15;
	float v15 = Run(Contact(-250, shallow, Vec(0, -0.26f, -0.97f))).approach[3];
	CHECK(v15 > 0.f && v15 < 0.9082f * 0.6f);
	// car on the goal side of the ball (between ball and goal): awayFrac 0 -> 0.5
	Vec goalside(3000, 3100, 800);
	CHECK_NEAR(Run(Contact(-250, goalside, Vec(0, 0.7071f, -0.7071f))).approach[3], 0.5f, 0.003f);
}

TEST(FRA_contact_needs_wheels_first_and_ends_the_stage) {
	Vec behind45(3000, 2900, 800);
	CHECK_NEAR(Run(Contact(-250, behind45, Vec(0, 0, 1))).approach[3], 0.f, 1e-6f);   // roof-first touch pays nothing
	// a second, perfect touch after a spent contact pays nothing (one-shot)
	auto v = Contact(-250, behind45, WHEELS_UP);
	Fr again = v.back(); v.push_back(again);
	auto r = Run(v);
	CHECK(r.approach[3] > 0.8f);
	CHECK_NEAR(r.approach[4], 0.f, 1e-6f);
}

TEST(FRA_disarm_conditions) {
	Vec behind45(3000, 2900, 800);
	// opponent touch before our contact: stage over, no bonus
	auto v = Contact(-250, behind45, WHEELS_UP); v[2].oppTouch = true;
	CHECK_NEAR(Run(v).approach[3], 0.f, 1e-6f);
	// ball falls below z 300 first
	auto w = Contact(-250, behind45, WHEELS_UP);
	Fr low = w[2]; low.ballPos.z = 250; w.insert(w.begin() + 3, low);      // armed at frame 2, ball dips below 300 at frame 3
	CHECK_NEAR(Run(w).approach[4], 0.f, 1e-6f);                            // the stage is spent; the latch died with it
	// 3 s (45 steps) after arming
	std::vector<Fr> late = { Setup(), Setup() };
	Fr hover = Air(Vec(3000, 3000, 900), Vec(0, 0, -100), Vec(3000, 2650, 720), Vec(0, 0, -100));
	for (int i = 0; i < 46; i++) late.push_back(hover);
	Fr hit = Contact(-250, behind45, WHEELS_UP).back(); late.push_back(hit);
	CHECK_NEAR(Run(late).approach.back(), 0.f, 1e-6f);
	// the setup latch expires after 2 s: ball only goes up 32 steps after the last qualifying step
	std::vector<Fr> stale = { Setup(), Setup() };
	Fr idle = Setup(); idle.ballVel = Vec(0, 0, 0); idle.touch = false;
	for (int i = 0; i < 32; i++) stale.push_back(idle);
	Fr up = Contact(-250, behind45, WHEELS_UP).back(); stale.push_back(Contact(-250, behind45, WHEELS_UP)[2]); stale.push_back(up);
	CHECK_NEAR(Run(stale).approach.back(), 0.f, 1e-6f);
}

TEST(FRA_per_step_income_is_bounded) {
	// hovering below a post-apex ball at the best pose for 45 steps (the whole armed window): closing 0, only the two proximity terms
	Fr h = Air(Vec(3000, 3000, 900), Vec(0, 0, -100), Vec(3000, 2650, 720), Vec(0, 0, 100), Vec(0, 0, -1));
	std::vector<Fr> v = { Setup(), Setup() };
	for (int i = 0; i < 60; i++) v.push_back(h);
	float total = 0; for (float x : Run(v).approach) total += x;
	// 46 armed steps x (0.00619 + 0.01354) = 0.91, plus the one-off closing progress on the first hover step (clamped 0.2)
	CHECK(total > 0.85f);
	CHECK(total < 1.25f);                // one clean contact is worth up to 1.25 on top; hovering alone cannot exceed it
}

// ------------------------------- Rev 2: speed floors, 2300 cap, speed match, wall launch -------------------------------

TEST(FRS_rev2_ball_speed_floors_and_the_2300_cap) {
	CHECK_NEAR(SetupPay(Setup(3000, 3000, Vec(900, 0, 0))), 0.f, 1e-6f);          // side wall needs >= 1000
	CHECK(SetupPay(Setup(3000, 3000, Vec(1100, 0, 0))) > 0.f);
	CHECK_NEAR(SetupPay(Setup(3000, 3000, Vec(2400, 0, 0))), 0.f, 1e-6f);         // faster than 2300: cannot be caught up to
	CHECK(SetupPay(Setup(3000, 3000, Vec(2200, 0, 0))) > 0.f);
	// backboard floor falls toward the goal: 800 uu/s pays at dBack 620 (floor ~606) but not at dBack 1620 (floor 940)
	CHECK(SetupPay(Setup(1500, 4500, Vec(0, 800, 0))) > 0.f);
	CHECK_NEAR(SetupPay(Setup(1500, 3500, Vec(0, 800, 0))), 0.f, 1e-6f);
	CHECK_NEAR(SetupPay(Setup(1500, 4500, Vec(0, 500, 0))), 0.f, 1e-6f);          // under the 600 near-goal floor
}

TEST(FRS_rev2_speed_match_bonus) {
	float base = 0.02933f;
	Fr m = Setup(); m.carVel = Vec(1200, 0, 0);                                    // rel 0: x1.5
	CHECK_NEAR(SetupPay(m), base * 1.5f, 0.0004f);
	m.carVel = Vec(1000, 0, 0);                                                    // rel 200: still full
	CHECK_NEAR(SetupPay(m), base * 1.5f, 0.0004f);
	m.carVel = Vec(800, 0, 0);                                                     // rel 400: half -> x1.25
	CHECK_NEAR(SetupPay(m), base * 1.25f, 0.0004f);
	m.carVel = Vec(400, 0, 0);                                                     // rel 800: none
	CHECK_NEAR(SetupPay(m), base, 0.0004f);
}

namespace {
	std::vector<BallState> Line(Vec a, Vec b) {
		std::vector<BallState> p;
		for (int t = 0; t < 241; t++) { BallState s; float u = t / 240.f; s.pos = a + (b - a) * u; p.push_back(s); }
		return p;
	}
	Fr WallHit(Vec vel = Vec(-300, 1500, 300)) {
		Fr f; f.carPos = Vec(4060, 3000, 400); f.ballPos = Vec(4000, 3000, 500); f.ballVel = vel; f.up = Vec(-1, 0, 0);
		f.ground = true; f.touch = true; return f;
	}
	float Launch(const Fr& hit) { return Run({ Setup(), Setup(), hit }).setup[2]; }
}

TEST(FRS_rev2_launch_touch_direction_and_gates) {
	// no path: ballistic fallback. vel (-300,1500,300) from cy 3000 keeps going into the opponent half -> 0.5 x boost factor 1
	CHECK_NEAR(Launch(WallHit()), 0.5f, 1e-4f);
	// a path into the goal mouth -> full
	auto goal = Line(Vec(4000, 3000, 500), Vec(100, 5100, 300));
	Fr g = WallHit(); g.path = &goal; CHECK_NEAR(Launch(g), 1.0f, 1e-4f);
	g.boost = 25; CHECK_NEAR(Launch(g), 0.5f, 1e-4f);                              // boost factor
	// a path back toward our half, from a setup in the opponent half -> 0
	auto back = Line(Vec(4000, 3000, 500), Vec(3000, 1000, 300));
	Fr b = WallHit(); b.path = &back; CHECK_NEAR(Launch(b), 0.f, 1e-6f);
	// faster than 2300 -> 0 even though aimed at the goal
	Fr fast = WallHit(Vec(-300, 2400, 300)); fast.path = &goal; CHECK_NEAR(Launch(fast), 0.f, 1e-6f);
	// geometry: car far from the wall, car not on the wall (airborne), ball low
	Fr far = WallHit(); far.carPos = Vec(3000, 3000, 400); CHECK_NEAR(Launch(far), 0.f, 1e-6f);
	Fr air = WallHit(); air.ground = false; CHECK_NEAR(Launch(air), 0.f, 1e-6f);
	Fr low = WallHit(); low.ballPos.z = 150; CHECK_NEAR(Launch(low), 0.f, 1e-6f);
	// no setup behind it (cold start) -> 0; the latch is spent by the launch (a second touch pays nothing)
	CHECK_NEAR(Run({ WallHit(), WallHit() }).setup[1], 0.f, 1e-6f);
	auto twice = Run({ Setup(), Setup(), WallHit(), WallHit() }).setup;
	CHECK_NEAR(twice[2], 0.5f, 1e-4f); CHECK_NEAR(twice[3], 0.f, 1e-6f);
	// latch expiry: 31 idle steps (> 2 s) between the setup and the wall hit
	std::vector<Fr> stale = { Setup(), Setup() };
	Fr idle = Setup(); idle.ballVel = Vec(0, 0, 0); idle.touch = false;
	for (int i = 0; i < 31; i++) stale.push_back(idle);
	stale.push_back(WallHit());
	CHECK_NEAR(Run(stale).setup.back(), 0.f, 1e-6f);
}

TEST(FRS_rev2_launch_direction_rules_directly) {
	// own-half start (cy -500) must cross midfield: crosses -> 0.5, stays -> 0
	GameState s; s.ball.pos = Vec(3900, -500, 400); s.ball.vel = Vec(0, 1500, 0);
	auto cross = Line(Vec(3900, -500, 400), Vec(3000, 1500, 300)); s.ballPred = &cross;
	CHECK_NEAR(FlipResetGeo::LaunchDirFrac(Team::BLUE, s), 0.5f, 1e-6f);
	auto stay = Line(Vec(3900, -500, 400), Vec(3000, -2500, 300)); s.ballPred = &stay;
	CHECK_NEAR(FlipResetGeo::LaunchDirFrac(Team::BLUE, s), 0.f, 1e-6f);
	// orange mirror of a goal-aimed launch pays full too
	GameState o; o.ball.pos = Vec(-4000, -3000, 500); o.ball.vel = Vec(300, -1500, 300);
	auto og = Line(Vec(-4000, -3000, 500), Vec(-100, -5100, 300)); o.ballPred = &og;
	CHECK_NEAR(FlipResetGeo::LaunchDirFrac(Team::ORANGE, o), 1.f, 1e-6f);
}

TEST(FRA_rev2_armed_survives_the_car_still_on_the_wall) {
	// the wall launch arms the approach while the car is still on the wall; it must not die on the grounded frames
	Fr hit = WallHit(); hit.ballPos.z = 700;
	Fr still = hit; still.touch = false;                                           // car still on the wall, no new touch
	Fr fly = Air(Vec(3000, 3000, 900), Vec(0, 0, -100), Vec(3000, 2650, 720), Vec(0, 0, -100));
	fly.carPos = Vec(3000, 2650, 720);
	auto r = Run({ Setup(), Setup(), hit, still, fly, fly }).approach;
	CHECK_NEAR(r[2], 0.f, 1e-6f); CHECK_NEAR(r[3], 0.f, 1e-6f);                    // grounded: waits, no pay, no bonus for the launch touch
	CHECK(r[4] > 0.f);                                                             // airborne and past apex: per-step terms are live
}
