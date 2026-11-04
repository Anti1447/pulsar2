#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

#include <cmath>

using namespace RLGC;
using namespace RewardTest;

// Audit of ConsecutiveAirTouchReward (checklist item 2, weight 40, wrapped ZeroSum(TEAM_SPIRIT 0.3)), as changed on 2026-11-03.
//   state          : an EFFECTIVE streak e per player: 0 whenever isOnGround (ground OR wall); + credit on each airborne touch step;
//                    unchanged airborne without a touch; reset on a demo AND the banked potential charged back on the step it starts (2026-11-04); RESET when an opponent touches the ball on a
//                    step this player does not (turnover)
//   credit         = ballHeight * softness * carHeight
//     ballHeight   = clamp((ball.z - 150) / 1300, 0, 1)                (AerialTouchReward's ballFrac)
//     softness     = 1 - clamp(|car vel - ball vel| / 500, 0, 1)^2    (MultiAirTouchReward's softness)
//     carHeight    = clamp((car.z - 17) / 500, 0, 1)                   (AirInterceptPotentialReward's car scale)
//   potential      = Phi(e) * g(d),  Phi(e) = 1 - exp(-max(0, e - 1) / 2),  g(d) = clamp((1100 - d) / 950, 0, 1),  d = car-ball distance
//   reward         = gamma * potential(now) - potential(previous step)
// Hand anchors: Phi(1.5) = 0.221199, Phi(2) = 0.393469, Phi(2.5) = 0.527633, Phi(3) = 0.632121, Phi(5) = 0.864665, Phi(10) = 0.988891;
//   g(150) = 1, g(625) = 0.5, g(1100) = 0.   Default fixture: the car sits 60 uu below the ball (contact range, g = 1), the ball is at
//   1450 uu (ball credit 1) and the car at 1390 uu (car credit 1), both at rest (softness 1): credit exactly 1, as before.
// Fixture: fabricated states (no engine). The engine side is in tools/reward_lab/packs/consecutive_air_touch and multi_air_touch.

namespace {
	constexpr float G = 0.9969f; // TRAIN_GAMMA

	struct CT {
		bool ground = false, touch = false, demoed = false, oppTouch = false, mateTouch = false;
		float ballZ = 1450;
		float carZ = -1;          // < 0: 60 uu below the ball
		float dx = 0;             // horizontal car-ball offset
		Vec carVel = Vec(0, 0, 0), ballVel = Vec(0, 0, 0);
	};
	CT A(bool touch) { CT c; c.touch = touch; return c; }           // airborne, touch or not
	CT Gr() { CT c; c.ground = true; return c; }                    // on the ground or a wall
	CT Gt() { CT c; c.ground = true; c.touch = true; return c; }    // a touch while supported
	CT Dm() { CT c; c.demoed = true; return c; }
	CT Low(bool touch = true) { CT c; c.touch = touch; c.ballZ = 93; return c; }   // airborne, ball on the floor
	CT At(float ballZ) { CT c = A(true); c.ballZ = ballZ; return c; }

	// player 0 (blue) is the one rewarded; an orange player 1 and a blue player 2 are added when a frame needs an opponent / teammate touch
	void Fill(GameState& g, const CT& f) {
		g.deltaTime = STEP_DT;
		float carZ = f.carZ >= 0 ? f.carZ : f.ballZ - 60.f;
		Player p = MakePlayer(1, 0, Team::BLUE, Vec(f.dx, 0, carZ));
		p.isOnGround = f.ground; p.ballTouchedStep = f.touch; p.isDemoed = f.demoed; p.vel = f.carVel;
		g.players.push_back(p);
		Player o = MakePlayer(2, 1, Team::ORANGE, Vec(3000, 0, 500)); o.isOnGround = false; o.ballTouchedStep = f.oppTouch;
		g.players.push_back(o);
		Player m = MakePlayer(3, 2, Team::BLUE, Vec(-3000, 0, 500)); m.isOnGround = false; m.ballTouchedStep = f.mateTouch;
		g.players.push_back(m);
		g.ball.pos = Vec(0, 0, f.ballZ); g.ball.vel = f.ballVel;
		g.lastTouchCarID = 1;    // deliberately the reward's own player: the reward must not read it
	}

	std::vector<float> Run(ConsecutiveAirTouchReward& r, const std::vector<CT>& fr) {
		std::vector<GameState> st(fr.size());
		for (size_t i = 0; i < fr.size(); i++) Fill(st[i], fr[i]);
		for (size_t i = 1; i < st.size(); i++) for (size_t k = 0; k < st[i].players.size(); k++) st[i].players[k].prev = &st[i - 1].players[k];
		for (size_t i = 1; i < st.size(); i++) st[i].prev = &st[i - 1];
		r.Reset(st[0]);
		std::vector<float> out;
		for (auto& s : st) out.push_back(r.GetReward(s.players[0], s, false));
		return out;
	}
	float Sum(const std::vector<float>& v) { float s = 0; for (float x : v) s += x; return s; }
}

TEST(ConsecAir2_phi_anchors_and_diminishing_marginal_credit) {
	CHECK_EQ(ConsecutiveAirTouchReward::Phi(0), 0.0f);
	CHECK_EQ(ConsecutiveAirTouchReward::Phi(1), 0.0f);
	CHECK_NEAR(ConsecutiveAirTouchReward::Phi(2), 0.393469f, 1e-5f);
	CHECK_NEAR(ConsecutiveAirTouchReward::Phi(3), 0.632121f, 1e-5f);
	CHECK_NEAR(ConsecutiveAirTouchReward::Phi(4), 0.776870f, 1e-5f);
	CHECK_NEAR(ConsecutiveAirTouchReward::Phi(5), 0.864665f, 1e-5f);
	CHECK_NEAR(ConsecutiveAirTouchReward::Phi(10), 0.988891f, 1e-5f);
	CHECK_NEAR(ConsecutiveAirTouchReward::Phi(20), 0.999925f, 1e-5f);
	float prev = 0, prevGain = 9;
	for (int n = 2; n <= 12; n++) {
		float p = ConsecutiveAirTouchReward::Phi((float)n), gain = p - prev;
		CHECK(p < 1.0f && gain > 0.0f && gain < prevGain);
		prev = p; prevGain = gain;
	}
	CHECK(40.f * ConsecutiveAirTouchReward::Phi(20) < 40.0f);
}

TEST(ConsecAir2_the_touch_credit_factors_at_hand_values) {
	using R = ConsecutiveAirTouchReward;
	// ball height (AerialTouch's scale)
	CHECK_EQ(R::TouchCredit(93), 0.0f); CHECK_EQ(R::TouchCredit(150), 0.0f);
	CHECK_NEAR(R::TouchCredit(151), 1.0f / 1300.f, 1e-6f); CHECK_NEAR(R::TouchCredit(800), 0.5f, 1e-6f);
	CHECK_EQ(R::TouchCredit(1450), 1.0f); CHECK_EQ(R::TouchCredit(2000), 1.0f);
	CHECK_EQ(R::BALL_MIN_Z, AerialTouchReward::BALL_MIN_Z); CHECK_EQ(R::BALL_FULL_Z, AerialTouchReward::BALL_FULL_Z);
	// softness (MultiAirTouch's): |dv| 0 -> 1, 250 -> 0.75, 500 and above -> 0
	CHECK_EQ(R::Softness(0), 1.0f); CHECK_NEAR(R::Softness(250), 0.75f, 1e-6f); CHECK_EQ(R::Softness(500), 0.0f); CHECK_EQ(R::Softness(2000), 0.0f);
	// car height (AirIntercept's scale): z 17 -> 0, 267 -> 0.5, 517 -> 1
	CHECK_EQ(R::CarHeightCredit(17), 0.0f); CHECK_EQ(R::CarHeightCredit(5), 0.0f);
	CHECK_NEAR(R::CarHeightCredit(267), 0.5f, 1e-6f); CHECK_EQ(R::CarHeightCredit(517), 1.0f); CHECK_EQ(R::CarHeightCredit(1500), 1.0f);
	CHECK_EQ(R::CAR_MIN_Z, AirInterceptPotentialReward::CAR_MIN_Z); CHECK_EQ(R::CAR_FULL_Z, AirInterceptPotentialReward::CAR_FULL_Z);
	// control distance gate: 1 up to 150, 0.5 at 625, 0 from 1100
	CHECK_EQ(R::ControlGate(0), 1.0f); CHECK_EQ(R::ControlGate(150), 1.0f);
	CHECK_NEAR(R::ControlGate(625), 0.5f, 1e-6f); CHECK_EQ(R::ControlGate(1100), 0.0f); CHECK_EQ(R::ControlGate(5000), 0.0f);
}

TEST(ConsecAir2_a_lone_air_touch_pays_zero_and_the_second_pays_gamma_phi2) {
	ConsecutiveAirTouchReward r(G);
	auto v = Run(r, { A(false), A(true), A(true) });
	CHECK_EQ(v[0], 0.0f);
	CHECK_EQ(v[1], 0.0f);
	CHECK_NEAR(v[2], G * 0.393469f, 1e-5f);
}

TEST(ConsecAir2_a_touch_while_supported_does_not_count_and_landing_refunds_exactly) {
	ConsecutiveAirTouchReward r(1.0f);
	auto a = Run(r, { A(true), A(true), A(true), Gr() });
	CHECK_NEAR(a[3], -0.632121f, 1e-5f);
	CHECK_NEAR(Sum(a), 0.0f, 1e-6f);
	ConsecutiveAirTouchReward r2(1.0f);
	auto b = Run(r2, { A(true), A(true), Gt(), A(true) });
	CHECK_NEAR(b[2], -0.393469f, 1e-5f);
	CHECK_EQ(b[3], 0.0f);
}

TEST(ConsecAir2_holding_a_chain_bleeds_gamma_minus_one_times_phi_per_step) {
	ConsecutiveAirTouchReward r(G);
	auto v = Run(r, { A(true), A(true), A(true), A(false), A(false), A(false) });
	for (int k = 3; k <= 5; k++) CHECK_NEAR(v[k], (G - 1.0f) * 0.632121f, 1e-6f);
}

TEST(ConsecAir2_a_continuous_contact_counts_every_step_and_pays_phi_of_the_length) {
	ConsecutiveAirTouchReward r(1.0f);
	std::vector<CT> fr; for (int i = 0; i < 10; i++) fr.push_back(A(true));
	auto v = Run(r, fr);
	CHECK_NEAR(Sum(v), 0.988891f, 1e-5f);
	CHECK_NEAR(v[1], 0.393469f, 1e-5f);
	CHECK_NEAR(v[2], 0.632121f - 0.393469f, 1e-5f);
}

// With the control-distance gate the potential only holds while the ball is near the car, so two touches far apart are no longer a free
// chain: after 100 steps the ball has drifted a control distance away and the banked credit has long been charged back.
TEST(ConsecAir2_touches_a_hundred_steps_apart_do_not_chain_once_the_ball_has_left) {
	ConsecutiveAirTouchReward r(1.0f);
	std::vector<CT> fr = { A(true), A(true) };                        // a chain of 2 at contact range: e = 2, potential 0.393469
	CT away = A(false); away.dx = 1500;                               // the ball is 1500 uu away: gate 0
	for (int i = 0; i < 100; i++) fr.push_back(away);
	CT back = A(true); back.dx = 0; fr.push_back(back);               // touched again, back at contact range
	auto v = Run(r, fr);
	CHECK_NEAR(v[2], -0.393469f, 1e-4f);                               // the step the ball left: the potential is charged back at once
	for (int k = 3; k < 102; k++) CHECK_EQ(v[k], 0.0f);                // nothing while it is gone (potential 0 and 0)
	CHECK_NEAR(v[102], 0.632121f, 1e-4f);                              // streak 3 (the chain's e is kept) comes back at full value: Phi(3) * 1
	CHECK_NEAR(Sum(v), 0.632121f, 1e-4f);                              // gamma 1: the sum is the potential now
}

TEST(ConsecAir2_the_control_gate_scales_the_potential_and_a_drifting_ball_pays_a_negative_step) {
	ConsecutiveAirTouchReward r(1.0f);
	CT mid = A(false); mid.dx = 611;           // d = sqrt(611^2 + 60^2) = 614 -> g = (1100 - 614) / 950 = 0.5116
	auto v = Run(r, { A(true), A(true), mid });
	float g = (1100.f - sqrtf(611.f * 611.f + 60.f * 60.f)) / 950.f;
	CHECK_NEAR(v[2], 0.393469f * g - 0.393469f, 1e-4f);                // r = Phi(2) * (g(now) - g(before))
	CHECK(v[2] < -0.18f);
	// closing back in is paid, symmetrically (the potential is a function of state)
	ConsecutiveAirTouchReward r2(1.0f);
	CT back = A(false);
	auto w = Run(r2, { A(true), A(true), mid, back });
	CHECK_NEAR(w[3], 0.393469f * (1.0f - g), 1e-4f);
	CHECK_NEAR(Sum(w), 0.393469f, 1e-4f);
}

TEST(ConsecAir2_any_cycle_nets_zero_undiscounted_and_the_discounted_sum_telescopes) {
	CT drift = A(false); drift.dx = 400;
	std::vector<CT> fr = { A(true), A(true), A(false), drift, A(true), A(true), drift, A(false), Gr(), A(true), A(true), Gr() };
	ConsecutiveAirTouchReward r1(1.0f);
	CHECK_NEAR(Sum(Run(r1, fr)), 0.0f, 1e-4f);
	ConsecutiveAirTouchReward r2(G);
	auto v = Run(r2, fr);
	double sum = 0, disc = 1; for (float x : v) { sum += disc * x; disc *= G; }
	CHECK_NEAR((float)sum, 0.0f, 1e-4f);
}

// A demolition refunds the banked chain on the step it starts (2026-11-04; before, a chain of 3 ended by a demo kept +Phi(3) = +0.632)
TEST(ConsecAir2_a_demo_mid_chain_refunds_the_banked_credit) {
	ConsecutiveAirTouchReward r(1.0f);
	auto v = Run(r, { A(true), A(true), A(true), Dm(), Gr() });
	CHECK_NEAR(v[3], -0.632121f, 1e-5f);     // the demolition step: -Phi(3)
	CHECK_EQ(v[4], 0.0f);                    // already 0
	CHECK_NEAR(Sum(v), 0.0f, 1e-5f);         // the whole chain nets 0, as after a landing
	ConsecutiveAirTouchReward r2(1.0f);
	auto w = Run(r2, { A(true), A(true), Dm(), A(true), A(true) });
	CHECK_NEAR(w[2], -0.393469f, 1e-5f);     // chain of 2: -Phi(2)
	CHECK_EQ(w[3], 0.0f);                    // the respawn step
	CHECK_EQ(w[4], 0.0f);                    // a lone touch after the respawn: a fresh chain of 1 pays 0
}

TEST(ConsecAir2_demo_refund_is_charged_once_and_scaled_by_the_control_gate) {
	ConsecutiveAirTouchReward r(1.0f);
	auto v = Run(r, { A(true), A(true), A(true), Dm(), Dm(), Dm(), Gr() });
	CHECK_NEAR(v[3], -0.632121f, 1e-5f);
	CHECK_EQ(v[4], 0.0f); CHECK_EQ(v[5], 0.0f); CHECK_EQ(v[6], 0.0f);   // no repeat while it stays demolished
	// the refund is Phi(e) x g(previous distance): a chain whose ball sits 625 uu away on the last live step (g = 0.5) refunds half
	CT far = A(false); far.dx = 625.f * 0.99999f;
	ConsecutiveAirTouchReward r2(1.0f);
	auto w = Run(r2, { A(true), A(true), A(true), far, Dm() });
	CHECK_NEAR(w[3] + w[4], -0.632121f, 1e-4f);   // the gate fall on the far step plus the refund still net the banked chain to 0
	// a demo with no chain banked pays nothing
	ConsecutiveAirTouchReward r3(1.0f);
	auto z = Run(r3, { A(false), Dm(), Gr() });
	CHECK_EQ(z[1], 0.0f);
}

TEST(ConsecAir2_each_player_has_its_own_streak) {
	GameState a, b, c;
	auto fill = [](GameState& g, bool t0, bool g1) {
		g.deltaTime = STEP_DT; g.ball.pos = Vec(0, 0, 1450);
		g.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 1390))); g.players[0].isOnGround = false; g.players[0].ballTouchedStep = t0;
		g.players.push_back(MakePlayer(2, 1, Team::BLUE, Vec(0, 100, 1390))); g.players[1].isOnGround = g1; g.players[1].ballTouchedStep = false;
	};
	fill(a, true, false); fill(b, true, false); fill(c, true, true);
	b.prev = &a; c.prev = &b; for (int i = 0; i < 2; i++) { b.players[i].prev = &a.players[i]; c.players[i].prev = &b.players[i]; }
	ConsecutiveAirTouchReward r(1.0f); r.Reset(a);
	auto v1 = r.GetAllRewards(a, false); auto v2 = r.GetAllRewards(b, false); auto v3 = r.GetAllRewards(c, false);
	CHECK_EQ(v1[0], 0.0f);
	CHECK_NEAR(v2[0], 0.393469f, 1e-5f);
	CHECK_EQ(v2[1], 0.0f); CHECK_EQ(v3[1], 0.0f);
	CHECK_NEAR(v3[0], 0.632121f - 0.393469f, 1e-5f);
}

// ---- credit factors in the engine-style chain --------------------------------------------------------------------------------------
TEST(ConsecAir2_ball_height_credit_scales_the_chain_and_a_mid_height_chain_needs_three_touches) {
	ConsecutiveAirTouchReward r(1.0f);
	auto v = Run(r, { A(false), At(800), At(800), At(800), At(800), At(800) });   // credit 0.5 each (the car follows the ball, full car credit)
	CHECK_EQ(v[1], 0.0f); CHECK_EQ(v[2], 0.0f);
	CHECK_NEAR(v[3], 0.221199f, 1e-5f);
	CHECK_NEAR(v[4], 0.393469f - 0.221199f, 1e-5f);
	CHECK_NEAR(Sum(v), 0.527633f, 1e-5f);
}

// A hop-poke into a floor ball: the ball rises through z 112, 133, 151, 166, 178 on five contact steps with the car below it: nothing is paid
TEST(ConsecAir2_a_hop_poke_that_lifts_a_floor_ball_pays_nothing) {
	ConsecutiveAirTouchReward r(G);
	std::vector<CT> fr = { A(false) };
	for (float z : { 112.f, 133.f, 151.f, 166.f, 178.f }) fr.push_back(At(z));
	fr.push_back(Gr());
	for (float x : Run(r, fr)) CHECK_EQ(x, 0.0f);
}

// 1. SOFTNESS: the same chain with a hard touch (the ball leaves 400 uu/s faster than the car) earns (1 - 0.64) of a soft one
TEST(ConsecAir2_a_hard_touch_adds_less_credit_than_a_soft_one) {
	auto e2 = [](float dv) {
		ConsecutiveAirTouchReward r(1.0f);
		CT t = A(true); t.carVel = Vec(0, 0, 0); t.ballVel = Vec(dv, 0, 0);
		auto v = Run(r, { A(true), t, t, t });                                  // credit 1 (soft), then three touches with the given |dv|
		return Sum(v);
	};
	float soft = e2(0), mid = e2(250), hard = e2(500);
	CHECK_NEAR(soft, 0.776870f, 1e-4f);                                         // e = 4 -> Phi(4)
	// |dv| 250 -> softness 0.75 per touch: e = 1 + 3 * 0.75 = 3.25 -> Phi(3.25)
	CHECK_NEAR(mid, 1.0f - expf(-(3.25f - 1.0f) / 2.0f), 1e-4f);
	// |dv| 500 -> softness 0: the three hard touches add nothing, e stays 1 -> Phi = 0
	CHECK_NEAR(hard, 0.0f, 1e-6f);
	CHECK(soft > mid && mid > hard);
}

// 2. CAR HEIGHT and 4. HORIZONTAL SPEED, and how the factors compose. The credit of one touch is the product of the four factors, checked here
// directly on FullTouchCredit (the distance gate belongs to the potential, so a low car far under a high ball is tested through the factors).
//   car z 267 (0.5) x ball z 800 (0.5) x softness |dv| = 250 (0.75) x horizontal ball speed 2650 (0.75) = 0.140625
TEST(ConsecAir2_the_four_credit_factors_multiply) {
	using R = ConsecutiveAirTouchReward;
	GameState gs; gs.deltaTime = STEP_DT;
	Player p = MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 267)); p.vel = Vec(2400, 0, 0); p.isOnGround = false; p.ballTouchedStep = true;
	gs.players.push_back(p);
	gs.ball.pos = Vec(0, 0, 800); gs.ball.vel = Vec(2650, 0, 0);
	CHECK_NEAR(R::FullTouchCredit(gs.players[0], gs), 0.5f * 0.5f * 0.75f * 0.75f, 1e-6f);
	// each factor alone moves it: a car at the floor adds nothing, a car at 517 doubles it
	gs.players[0].pos = Vec(0, 0, 17);  CHECK_EQ(R::FullTouchCredit(gs.players[0], gs), 0.0f);
	gs.players[0].pos = Vec(0, 0, 517); CHECK_NEAR(R::FullTouchCredit(gs.players[0], gs), 1.0f * 0.5f * 0.75f * 0.75f, 1e-6f);
}

// 4. HORIZONTAL SPEED (MultiAirTouch's rule): 1 up to 2300 uu/s, 0.5 from 3000, linear between, waived within 3000 uu of the opponent goal back
TEST(ConsecAir2_a_ball_flying_off_horizontally_is_damped_unless_near_the_opponent_goal) {
	using R = ConsecutiveAirTouchReward;
	CHECK_EQ(R::HorizontalSpeedMul(Team::BLUE, Vec(0, 0, 1000), Vec(2300, 0, 0)), 1.0f);
	CHECK_NEAR(R::HorizontalSpeedMul(Team::BLUE, Vec(0, 0, 1000), Vec(2650, 0, 0)), 0.75f, 1e-6f);
	CHECK_NEAR(R::HorizontalSpeedMul(Team::BLUE, Vec(0, 0, 1000), Vec(3000, 0, 0)), 0.5f, 1e-6f);
	CHECK_NEAR(R::HorizontalSpeedMul(Team::BLUE, Vec(0, 0, 1000), Vec(0, 4000, 5000)), 0.5f, 1e-6f);   // vertical speed does not count, horizontal 4000 -> 0.5
	CHECK_EQ(R::HorizontalSpeedMul(Team::BLUE, Vec(0, 4500, 1000), Vec(3000, 0, 0)), 1.0f);            // within 3000 of the orange goal back (0, 6000): waived
	CHECK_NEAR(R::HorizontalSpeedMul(Team::ORANGE, Vec(0, 4500, 1000), Vec(3000, 0, 0)), 0.5f, 1e-6f); // for orange the opponent goal is the other end
	// in a chain: a touch that sends the ball off at 3000 uu/s adds half the credit
	ConsecutiveAirTouchReward r(1.0f);
	CT fast = A(true); fast.ballVel = Vec(3000, 0, 0); fast.carVel = Vec(3000, 0, 0);   // dv 0 (soft), horizontal 3000 -> credit 0.5
	auto v = Run(r, { A(true), fast, fast, fast });
	CHECK_NEAR(Sum(v), 1.0f - expf(-(2.5f - 1.0f) / 2.0f), 1e-4f);                      // e = 1 + 0.5 * 3 = 2.5
}

// 3. OPPONENT TURNOVER: the chain ends when an opponent touches the ball on a step this player does not
TEST(ConsecAir2_an_opponent_touch_ends_the_chain_and_charges_it_back) {
	ConsecutiveAirTouchReward r(1.0f);
	CT opp = A(false); opp.oppTouch = true;
	auto v = Run(r, { A(true), A(true), A(true), opp, A(true), A(true) });
	CHECK_NEAR(v[2], 0.632121f - 0.393469f, 1e-5f);        // e = 3
	CHECK_NEAR(v[3], -0.632121f, 1e-5f);                   // the turnover: the whole banked potential is charged back (like a landing)
	CHECK_EQ(v[4], 0.0f);                                  // a fresh chain: e = 1
	CHECK_NEAR(v[5], 0.393469f, 1e-5f);                    // and e = 2
}

TEST(ConsecAir2_a_teammates_touch_or_a_contested_step_does_not_end_the_chain) {
	CT mate = A(false); mate.mateTouch = true;
	ConsecutiveAirTouchReward r(1.0f);
	auto v = Run(r, { A(true), A(true), mate, A(true) });
	CHECK_EQ(v[2], 0.0f);                                  // a teammate touching changes nothing
	CHECK_NEAR(v[3], 0.632121f - 0.393469f, 1e-5f);        // the chain continued: e = 3
	CT contested = A(true); contested.oppTouch = true;     // both touch on the same step: a fight, the chain is kept (and extended)
	ConsecutiveAirTouchReward r2(1.0f);
	auto w = Run(r2, { A(true), A(true), contested });
	CHECK_NEAR(w[2], 0.632121f - 0.393469f, 1e-5f);
}

TEST(ConsecAir2_the_turnover_reads_the_touch_flags_not_the_engine_last_touch_id) {
	// lastTouchCarID is the reward's own player in every frame (Fill), and still an opponent touch ends the chain: see the test above.
	// Conversely, if the id said "opponent" but no opponent touched this step, nothing happens:
	ConsecutiveAirTouchReward r(1.0f);
	std::vector<GameState> st(3);
	for (int i = 0; i < 3; i++) { Fill(st[i], A(true)); st[i].lastTouchCarID = 2; }   // car 2 is the orange player
	for (int i = 1; i < 3; i++) { st[i].prev = &st[i - 1]; for (size_t k = 0; k < st[i].players.size(); k++) st[i].players[k].prev = &st[i - 1].players[k]; }
	r.Reset(st[0]);
	std::vector<float> v; for (auto& s : st) v.push_back(r.GetReward(s.players[0], s, false));
	CHECK_NEAR(v[1], 0.393469f, 1e-5f);                    // the chain survived the 'opponent' last-touch id: e = 2, then 3
	CHECK_NEAR(v[2], 0.632121f - 0.393469f, 1e-5f);
}

TEST(ConsecAir2_an_opponent_touch_with_no_chain_is_free) {
	CT opp = A(false); opp.oppTouch = true;
	ConsecutiveAirTouchReward r(G);
	for (float x : Run(r, { A(false), opp, opp, A(false) })) CHECK_EQ(x, 0.0f);
}

// Wiring as in training: ZeroSum(ConsecAir, 0.3), 2v2. Blue car 0 makes its 2nd air touch: r = Phi(2) = 0.393469 (gamma 1).
//   car 0: 0.85 r = 0.33445; teammate 0.15 r = 0.05902; each orange car -0.5 r = -0.19673
TEST(ConsecAir2_zero_sum_2v2_values_at_team_spirit_0p3) {
	GameState a, b; a.deltaTime = b.deltaTime = STEP_DT;
	for (GameState* g : { &a, &b }) {
		g->ball.pos = Vec(0, 0, 1450);
		for (int i = 0; i < 4; i++) {
			Player p = MakePlayer(1 + i, i, i < 2 ? Team::BLUE : Team::ORANGE, i == 0 ? Vec(0, 0, 1390) : Vec(3000.f + i * 100.f, 0, 500));
			p.isOnGround = false; p.ballTouchedStep = (i == 0);
			g->players.push_back(p);
		}
	}
	b.prev = &a; for (int i = 0; i < 4; i++) b.players[i].prev = &a.players[i];
	Reward* zs = new ZeroSumReward(new ConsecutiveAirTouchReward(1.0f), 0.3f);
	zs->Reset(a); zs->PreStep(a); zs->GetAllRewards(a, false);
	zs->PreStep(b);
	auto v = zs->GetAllRewards(b, false);
	CHECK_NEAR(v[0], 0.33445f, 1e-4f);
	CHECK_NEAR(v[1], 0.05902f, 1e-4f);
	CHECK_NEAR(v[2], -0.19673f, 1e-4f);
	CHECK_NEAR(v[3], -0.19673f, 1e-4f);
	CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-5f);
	delete zs;
}
