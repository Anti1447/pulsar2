#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

#include <cmath>

using namespace RLGC;
using namespace RewardTest;

// Audit of ConsecutiveAirTouchReward (checklist item 2, weight 40, wrapped ZeroSum(TEAM_SPIRIT 0.3)).
//   streak: +1 on each step the player is airborne AND touches the ball; 0 whenever isOnGround (ground OR wall); unchanged when airborne
//           with no touch (however long); reset (and not charged) on a demo or the step after
//   Phi(n)  = 1 - exp(-max(0, n - 1) / 2)                    r = gamma * Phi(streak now) - Phi(streak before)
// Hand-computed anchors, Phi(n): n=0,1 -> 0 | 2 -> 0.393469 | 3 -> 0.632121 | 4 -> 0.776870 | 5 -> 0.864665 | 6 -> 0.917915 | 10 -> 0.988891 | 20 -> 0.999925
//   marginal credit: 2nd touch +0.3935, 3rd +0.2386, 4th +0.1447, 5th +0.0878
// The older tests in tests/test_pbr_rewards.cpp (ConsecAir_*) cover the qualitative cases; this file adds exact values, the carry
// semantics, the missing time limit, the demo leak and the team wiring. Fixture: fabricated states (no engine). The engine side (what a
// real "touch" is: sustained contact counts on every step) is in tools/reward_lab/packs/consecutive_air_touch.

namespace {
	constexpr float G = 0.9969f; // TRAIN_GAMMA

	struct CT { bool ground = false, touch = false, demoed = false; float ballZ = 1450; };   // 1450 uu = full ball-height credit (1.0 per touch), so the older hand values hold
	CT A(bool touch) { CT c; c.touch = touch; return c; }           // airborne, touch or not
	CT Gr() { CT c; c.ground = true; return c; }                    // on the ground or a wall
	CT Gt() { CT c; c.ground = true; c.touch = true; return c; }    // a touch while supported
	CT Dm() { CT c; c.demoed = true; return c; }
CT Low(bool touch = true) { CT c; c.touch = touch; c.ballZ = 93; return c; }   // airborne, ball on the floor

	std::vector<float> Run(ConsecutiveAirTouchReward& r, const std::vector<CT>& fr) {
		std::vector<GameState> st(fr.size());
		for (size_t i = 0; i < fr.size(); i++) {
			st[i].deltaTime = STEP_DT;
			Player p = MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 500));
			p.isOnGround = fr[i].ground; p.ballTouchedStep = fr[i].touch; p.isDemoed = fr[i].demoed;
			st[i].players.push_back(p);
			st[i].ball.pos = Vec(0, 0, fr[i].ballZ);
		}
		for (size_t i = 1; i < st.size(); i++) { st[i].prev = &st[i - 1]; st[i].players[0].prev = &st[i - 1].players[0]; }
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
	CHECK_NEAR(ConsecutiveAirTouchReward::Phi(6), 0.917915f, 1e-5f);
	CHECK_NEAR(ConsecutiveAirTouchReward::Phi(10), 0.988891f, 1e-5f);
	CHECK_NEAR(ConsecutiveAirTouchReward::Phi(20), 0.999925f, 1e-5f);
	float prev = 0, prevGain = 9;
	for (int n = 2; n <= 12; n++) {                       // strictly below 1, strictly increasing, with shrinking gains
		float p = ConsecutiveAirTouchReward::Phi(n), gain = p - prev;
		CHECK(p < 1.0f && gain > 0.0f && gain < prevGain);
		prev = p; prevGain = gain;
	}
	CHECK(40.f * ConsecutiveAirTouchReward::Phi(20) < 40.0f);   // a weighted chain can never reach 40
}

TEST(ConsecAir2_a_lone_air_touch_pays_zero_and_the_second_pays_gamma_phi2) {
	ConsecutiveAirTouchReward r(G);
	auto v = Run(r, { A(false), A(true), A(true) });
	CHECK_EQ(v[0], 0.0f);
	CHECK_EQ(v[1], 0.0f);                                  // streak 0 -> 1: Phi 0 -> 0
	CHECK_NEAR(v[2], G * 0.393469f, 1e-5f);                // streak 1 -> 2: +0.39225 (x40 = +15.7)
}

TEST(ConsecAir2_a_touch_while_supported_does_not_count_and_landing_refunds_exactly) {
	ConsecutiveAirTouchReward r(1.0f);
	auto a = Run(r, { A(true), A(true), A(true), Gr() });
	CHECK_NEAR(a[3], -0.632121f, 1e-5f);                   // landing refunds Phi(3) exactly
	CHECK_NEAR(Sum(a), 0.0f, 1e-6f);
	ConsecutiveAirTouchReward r2(1.0f);
	auto b = Run(r2, { A(true), A(true), Gt(), A(true) }); // a touch on the ground: the chain is gone, so the next air touch is a lone one
	CHECK_NEAR(b[2], -0.393469f, 1e-5f);
	CHECK_EQ(b[3], 0.0f);
}

TEST(ConsecAir2_holding_a_chain_bleeds_gamma_minus_one_times_phi_per_step) {
	ConsecutiveAirTouchReward r(G);
	auto v = Run(r, { A(true), A(true), A(true), A(false), A(false), A(false) });
	for (int k = 3; k <= 5; k++) CHECK_NEAR(v[k], (G - 1.0f) * 0.632121f, 1e-6f);   // -0.001960 per step, x40 = -0.078, about -1.2 per second at 15 Hz
}

// Carry semantics (engine-measured: pushing a ball in the air reports a touch on EVERY step of the contact, 17 steps in the pack case):
// a carry of N steps is a chain of N, so the potential climbs toward 1 while the carry lasts and is refunded on landing.
TEST(ConsecAir2_a_continuous_contact_counts_every_step_and_pays_phi_of_the_length) {
	ConsecutiveAirTouchReward r(1.0f);
	std::vector<CT> fr; for (int i = 0; i < 10; i++) fr.push_back(A(true));
	auto v = Run(r, fr);
	CHECK_NEAR(Sum(v), 0.988891f, 1e-5f);                  // gamma 1: the sum is Phi(10)
	CHECK_NEAR(v[1], 0.393469f, 1e-5f);
	CHECK_NEAR(v[2], 0.632121f - 0.393469f, 1e-5f);
}

// No time limit (a finding): two touches a hundred steps apart, airborne the whole time, are a "chain"; the second one pays the same as
// back-to-back touches. It is not a farm (Phi(1) = 0 and the chain is refunded on landing) but the chain is not about continuity.
TEST(ConsecAir2_two_touches_a_hundred_steps_apart_still_chain) {
	ConsecutiveAirTouchReward r(G);
	std::vector<CT> fr = { A(true) }; for (int i = 0; i < 100; i++) fr.push_back(A(false)); fr.push_back(A(true));
	auto v = Run(r, fr);
	CHECK_NEAR(v.back(), G * 0.393469f, 1e-5f);            // exactly the back-to-back second-touch payment
}

TEST(ConsecAir2_any_cycle_nets_zero_undiscounted_and_the_discounted_sum_telescopes) {
	std::vector<CT> fr = { A(true), A(true), A(false), A(true), A(true), A(true), A(false), Gr(), A(true), A(true), Gr() };
	ConsecutiveAirTouchReward r1(1.0f);
	CHECK_NEAR(Sum(Run(r1, fr)), 0.0f, 1e-5f);
	ConsecutiveAirTouchReward r2(G);
	auto v = Run(r2, fr);
	double sum = 0, disc = 1; for (float x : v) { sum += disc * x; disc *= G; }
	CHECK_NEAR((float)sum, 0.0f, 1e-5f);                   // starts and ends on the ground: Phi 0 at both ends
}

// A demo drops the chain WITHOUT refunding it (the code calls this "a negligible, standard telescoping leak"): measured here, a chain of 3
// that ends in a demo keeps +Phi(3) = +0.632 (x40 = +25) instead of netting 0.
TEST(ConsecAir2_a_demo_mid_chain_keeps_the_banked_credit) {
	ConsecutiveAirTouchReward r(1.0f);
	auto v = Run(r, { A(true), A(true), A(true), Dm(), Gr() });
	CHECK_EQ(v[3], 0.0f);                                  // the demo step: nothing charged
	CHECK_EQ(v[4], 0.0f);                                  // the chain was reset, so landing refunds nothing
	CHECK_NEAR(Sum(v), 0.632121f, 1e-5f);                  // the whole chain's credit is kept
	// a respawned player starts a fresh chain
	ConsecutiveAirTouchReward r2(1.0f);
	auto w = Run(r2, { A(true), A(true), Dm(), A(true), A(true) });
	CHECK_EQ(w[3], 0.0f);                                  // the step after the demo is also skipped (previous frame demoed)
	CHECK_EQ(w[4], 0.0f);                                  // streak 0 -> 1 again: a lone touch
}

TEST(ConsecAir2_each_player_has_its_own_streak) {
	GameState a, b, c; a.deltaTime = b.deltaTime = c.deltaTime = STEP_DT;
	auto fill = [](GameState& g, bool t0, bool g1) {
		g.ball.pos = Vec(0, 0, 1450);
		g.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 500))); g.players[0].isOnGround = false; g.players[0].ballTouchedStep = t0;
		g.players.push_back(MakePlayer(2, 1, Team::ORANGE, Vec(0, 0, 500))); g.players[1].isOnGround = g1; g.players[1].ballTouchedStep = false;
	};
	fill(a, true, false); fill(b, true, false); fill(c, true, true);
	b.prev = &a; c.prev = &b; for (int i = 0; i < 2; i++) { b.players[i].prev = &a.players[i]; c.players[i].prev = &b.players[i]; }
	ConsecutiveAirTouchReward r(1.0f); r.Reset(a);
	auto v1 = r.GetAllRewards(a, false); auto v2 = r.GetAllRewards(b, false); auto v3 = r.GetAllRewards(c, false);
	CHECK_EQ(v1[0], 0.0f);
	CHECK_NEAR(v2[0], 0.393469f, 1e-5f);                   // blue's second air touch
	CHECK_EQ(v2[1], 0.0f); CHECK_EQ(v3[1], 0.0f);          // orange never had a chain
	CHECK_NEAR(v3[0], 0.632121f - 0.393469f, 1e-5f);       // blue's third touch pays Phi(3) - Phi(2) = 0.2387 while orange lands: not affected
}

// Wiring as in training: ZeroSum(ConsecAir, 0.3), 2v2. Blue car 0 makes its 2nd air touch: r = Phi(2) = 0.393469 (gamma 1).
//   car 0: 0.85 r = 0.33445; teammate 0.15 r = 0.05902; each orange car -0.5 r = -0.19673
TEST(ConsecAir2_zero_sum_2v2_values_at_team_spirit_0p3) {
	GameState a, b; a.deltaTime = b.deltaTime = STEP_DT;
	for (GameState* g : { &a, &b }) {
		g->ball.pos = Vec(0, 0, 1450);
		for (int i = 0; i < 4; i++) {
			Player p = MakePlayer(1 + i, i, i < 2 ? Team::BLUE : Team::ORANGE, Vec(i * 100.f, 0, 500));
			p.isOnGround = false; p.ballTouchedStep = (i == 0);
			g->players.push_back(p);
		}
	}
	b.prev = &a; for (int i = 0; i < 4; i++) b.players[i].prev = &a.players[i];
	Reward* zs = new ZeroSumReward(new ConsecutiveAirTouchReward(1.0f), 0.3f);
	zs->Reset(a); zs->PreStep(a); zs->GetAllRewards(a, false);     // first touch: streak 1, Phi 0
	zs->PreStep(b);
	auto v = zs->GetAllRewards(b, false);                           // second touch
	CHECK_NEAR(v[0], 0.33445f, 1e-4f);
	CHECK_NEAR(v[1], 0.05902f, 1e-4f);
	CHECK_NEAR(v[2], -0.19673f, 1e-4f);
	CHECK_NEAR(v[3], -0.19673f, 1e-4f);
	CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-5f);
	delete zs;
}

// BALL-HEIGHT CREDIT (2026-11-03, AerialTouchReward's scale): each touch adds clamp((ball.z - 150) / 1300, 0, 1) to an EFFECTIVE streak,
// and Phi(e) = 1 - exp(-max(0, e - 1) / 2). Hand values: credit(93) = 0, credit(150) = 0, credit(151) = 1/1300 = 0.000769, credit(800) = 0.5,
// credit(1450) = 1, credit(2000) = 1.   Phi(1.5) = 0.221199, Phi(2) = 0.393469, Phi(2.5) = 0.527633, Phi(3) = 0.632121.
TEST(ConsecAir2_touch_credit_is_AerialTouchs_ball_height_scale) {
	CHECK_EQ(ConsecutiveAirTouchReward::TouchCredit(93), 0.0f);
	CHECK_EQ(ConsecutiveAirTouchReward::TouchCredit(150), 0.0f);
	CHECK_NEAR(ConsecutiveAirTouchReward::TouchCredit(151), 1.0f / 1300.f, 1e-6f);
	CHECK_NEAR(ConsecutiveAirTouchReward::TouchCredit(800), 0.5f, 1e-6f);
	CHECK_EQ(ConsecutiveAirTouchReward::TouchCredit(1450), 1.0f);
	CHECK_EQ(ConsecutiveAirTouchReward::TouchCredit(2000), 1.0f);
	CHECK_EQ(ConsecutiveAirTouchReward::BALL_MIN_Z, AerialTouchReward::BALL_MIN_Z);     // locked to AerialTouch's constants
	CHECK_EQ(ConsecutiveAirTouchReward::BALL_FULL_Z, AerialTouchReward::BALL_FULL_Z);
	float ballFracAerialTouch = RS_CLAMP((800.f - AerialTouchReward::BALL_MIN_Z) / (AerialTouchReward::BALL_FULL_Z - AerialTouchReward::BALL_MIN_Z), 0, 1);
	CHECK_EQ(ConsecutiveAirTouchReward::TouchCredit(800), ballFracAerialTouch);         // literally the same formula
}

TEST(ConsecAir2_a_mid_height_chain_needs_two_touches_before_it_pays_anything) {
	auto mid = [] { CT c = A(true); c.ballZ = 800; return c; };   // credit 0.5 each
	ConsecutiveAirTouchReward r(1.0f);
	auto v = Run(r, { A(false), mid(), mid(), mid(), mid(), mid() });
	CHECK_EQ(v[1], 0.0f);                                  // e = 0.5, Phi 0
	CHECK_EQ(v[2], 0.0f);                                  // e = 1.0, Phi 0
	CHECK_NEAR(v[3], 0.221199f, 1e-5f);                    // e = 1.5
	CHECK_NEAR(v[4], 0.393469f - 0.221199f, 1e-5f);        // e = 2.0
	CHECK_NEAR(v[5], 0.527633f - 0.393469f, 1e-5f);        // e = 2.5
	CHECK_NEAR(Sum(v), 0.527633f, 1e-5f);
}

TEST(ConsecAir2_full_height_touches_behave_exactly_as_before_and_mixed_heights_add_up) {
	auto at = [](float z) { CT c = A(true); c.ballZ = z; return c; };
	ConsecutiveAirTouchReward r(1.0f);
	auto v = Run(r, { A(false), at(1450), at(800), at(1450) });   // e = 1, 1.5, 2.5
	CHECK_EQ(v[1], 0.0f);
	CHECK_NEAR(v[2], 0.221199f, 1e-5f);
	CHECK_NEAR(v[3], 0.527633f - 0.221199f, 1e-5f);
}

// The hop-poke of the audit: the car's push lifts the floor ball through z = 112, 133, 151, 166, 178 on five contact steps. Credits
// 0, 0, 0.00077, 0.01231, 0.02154 add up to 0.035, far below 1, so nothing is paid (a plain 150 uu floor still paid 0.629 for it).
TEST(ConsecAir2_a_hop_poke_that_lifts_a_floor_ball_pays_nothing) {
	ConsecutiveAirTouchReward r(G);
	std::vector<CT> fr = { A(false) };
	for (float z : { 112.f, 133.f, 151.f, 166.f, 178.f }) { CT c = A(true); c.ballZ = z; fr.push_back(c); }
	fr.push_back(Gr());
	auto v = Run(r, fr);
	for (float x : v) CHECK_EQ(x, 0.0f);
}

TEST(ConsecAir2_the_discounted_sum_still_telescopes_with_varied_ball_heights_and_landing_refunds_the_partial_streak) {
	std::vector<CT> fr = { A(false) };
	for (float z : { 900.f, 700.f, 1200.f, 400.f, 1450.f, 1000.f, 600.f }) { CT c = A(true); c.ballZ = z; fr.push_back(c); }
	fr.push_back(A(false)); fr.push_back(Gr());
	ConsecutiveAirTouchReward r(G);
	auto v = Run(r, fr);
	double sum = 0, disc = 1; for (float x : v) { sum += disc * x; disc *= G; }
	CHECK_NEAR((float)sum, 0.0f, 1e-5f);                   // starts and ends on the ground: Phi 0 at both ends
	CHECK(v[v.size() - 1] < -0.3f);                         // the landing refund of the banked chain
	ConsecutiveAirTouchReward r1(1.0f);
	CHECK_NEAR(Sum(Run(r1, fr)), 0.0f, 1e-5f);
}

// A low touch mid-chain adds (almost) nothing and never resets: it is just a touch worth ~0
TEST(ConsecAir2_a_low_touch_mid_chain_adds_nothing_and_does_not_reset_it) {
	ConsecutiveAirTouchReward r(1.0f);
	auto v = Run(r, { A(true), A(true), Low(), A(true) });
	CHECK_NEAR(v[1], 0.393469f, 1e-5f);               // e = 2
	CHECK_EQ(v[2], 0.0f);                              // gamma 1: a floor-level touch adds credit 0
	CHECK_NEAR(v[3], 0.632121f - 0.393469f, 1e-5f);   // the next full-height touch is e = 3: the chain resumed
}
