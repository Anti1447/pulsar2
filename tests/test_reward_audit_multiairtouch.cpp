#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/MultiAirTouchReward.h>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

#include <cmath>

using namespace RLGC;
using namespace RewardTest;

// Audit of MultiAirTouchReward (RLGymBot_v3 port; a COMMENTED-OUT candidate in BuildRewards: ZeroSum(0.3), v3 weight 50).
// An EVENT reward, not a potential: every counted air touch pays a tier by chain length plus quality bonuses; nothing is ever refunded.
//   counted touch : player.ballTouchedStep AND ball.z > 150 AND car.z > 300 AND >= 0.5 s since the last counted touch
//                   (the clock starts at 0 on Reset, so a touch in the first 0.5 s of an episode is never counted)
//   chain         : resets after 1.0 s with no counted touch, when the BALL falls to 150 uu or below, or when the last toucher
//                   (state.lastTouchCarID) is an opponent; a teammate's touch does NOT reset it
//   base tier     : 0.1 (1st), 0.4 (2nd), 0.7 (3rd and later)
//   bonuses (each scaled by boostMultiplier = clamp(boost at the first touch / 50, 0, 1)): consistency 0.09 from the 2nd touch, height 0.2
//     once any touch was above 1500, contact angle 0.25 * exp(-(angle - desired)^2 / 0.32), softness 0.15 (0.075 on the 2nd) on touches 1-2,
//     z-lift 0.25 * clamp(((dz + dvz) / 2) / 500, 0, 1)
//   horizontal speed: reward * (1 - 0.5 * clamp((horizSpeed - 2300) / 700, 0, 1)) unless the ball is within 3000 uu of the opponent goal
// Boost 0 is used wherever the bonuses should vanish, so the base tiers are checked exactly. Fixture: fabricated states (no engine). The
// engine side and the comparison with ConsecutiveAirTouch are in tools/reward_lab/packs/multi_air_touch.

namespace {
	constexpr float DT = 8.0f / 120.0f;

	struct MA { Vec carPos = Vec(0, 0, 1000); bool touch = false; float boost = 0; Vec ballPos = Vec(0, 0, 1100); Vec ballVel = Vec(0, 0, 0); int lastTouch = -1; };

	GameState MakeState(const MA& m, GameState* prev = nullptr) {
		GameState gs; gs.deltaTime = DT;
		Player p = MakePlayer(1, 0, Team::BLUE, m.carPos);
		p.ballTouchedStep = m.touch; p.boost = m.boost; p.isOnGround = false;
		p.rotMat = RotMat::GetIdentity();
		gs.players.push_back(p);
		gs.ball.pos = m.ballPos; gs.ball.vel = m.ballVel; gs.lastTouchCarID = m.lastTouch;
		(void)prev;
		return gs;
	}

	// One blue player; returns the per-step rewards. frames[i] is the state after step i+1.
	std::vector<float> Run(MultiAirTouchReward& r, const std::vector<MA>& frames) {
		std::vector<GameState> st; st.reserve(frames.size());
		for (auto& f : frames) st.push_back(MakeState(f));
		for (size_t i = 1; i < st.size(); i++) st[i].prev = &st[i - 1];
		r.Reset(st[0]);
		std::vector<float> out;
		for (auto& s : st) { r.PreStep(s); out.push_back(r.GetReward(s.players[0], s, false)); }
		return out;
	}

	// n idle steps then a touch, repeated: returns frames with a touch on the given 1-based steps
	std::vector<MA> TouchesAt(int total, std::vector<int> touchSteps) {
		std::vector<MA> fr(total);
		for (int s : touchSteps) fr[s - 1].touch = true;
		return fr;
	}
	float Sum(const std::vector<float>& v) { float s = 0; for (float x : v) s += x; return s; }
}

TEST(MultiAir_base_tiers_are_0p1_0p4_then_0p7_per_touch) {
	MultiAirTouchReward r;
	auto v = Run(r, TouchesAt(60, { 9, 17, 25, 33, 41 }));   // 8 steps = 0.533 s apart, all above the 0.5 s minimum
	CHECK_NEAR(v[8], 0.1f, 1e-6f);
	CHECK_NEAR(v[16], 0.4f, 1e-6f);
	CHECK_NEAR(v[24], 0.7f, 1e-6f);
	CHECK_NEAR(v[32], 0.7f, 1e-6f);                          // the third tier never grows
	CHECK_NEAR(v[40], 0.7f, 1e-6f);
	CHECK_NEAR(Sum(v), 0.1f + 0.4f + 3 * 0.7f, 1e-5f);
}

// A touch in the first 0.5 s of an episode can never count: the clock starts at 0 and lastTouchTime defaults to 0
TEST(MultiAir_a_touch_in_the_first_half_second_is_ignored) {
	MultiAirTouchReward r;
	auto v = Run(r, TouchesAt(30, { 7 }));                  // 7 steps = 0.467 s
	CHECK_EQ(Sum(v), 0.0f);
	MultiAirTouchReward r2;
	auto w = Run(r2, TouchesAt(30, { 8 }));                 // 8 steps = 0.533 s
	CHECK_NEAR(w[7], 0.1f, 1e-6f);
}

TEST(MultiAir_touches_closer_than_0p5_s_do_not_count_and_contact_counts_once_per_half_second) {
	MultiAirTouchReward r;
	auto v = Run(r, TouchesAt(60, { 9, 10, 11, 12, 13, 14, 15, 16, 17, 18 }));   // contact on 10 consecutive steps
	CHECK_NEAR(v[8], 0.1f, 1e-6f);
	for (int k = 9; k <= 15; k++) CHECK_EQ(v[k], 0.0f);       // steps 10-16 are within 0.5 s of step 9 (7 steps = 0.467 s)
	CHECK_NEAR(v[16], 0.4f, 1e-6f);                            // step 17: 8 steps after step 9 = 0.533 s: counted as the 2nd
	CHECK_EQ(v[17], 0.0f);                                     // step 18 is within 0.5 s of step 17
}

TEST(MultiAir_the_chain_resets_after_a_second_without_a_touch) {
	MultiAirTouchReward r;
	auto v = Run(r, TouchesAt(80, { 9, 17, 34 }));          // gap 17 steps = 1.133 s > 1.0 s
	CHECK_NEAR(v[8], 0.1f, 1e-6f);
	CHECK_NEAR(v[16], 0.4f, 1e-6f);
	CHECK_NEAR(v[33], 0.1f, 1e-6f);                          // a first touch again
	MultiAirTouchReward r2;
	auto w = Run(r2, TouchesAt(80, { 9, 17, 31 }));         // gap 14 steps = 0.933 s < 1.0 s: the chain continues
	CHECK_NEAR(w[30], 0.7f, 1e-6f);
}

TEST(MultiAir_height_gates_ball_above_150_and_car_above_300) {
	auto first = [](float carZ, float ballZ) {
		MultiAirTouchReward r;
		std::vector<MA> fr = TouchesAt(20, { 10 });
		fr[9].carPos = Vec(0, 0, carZ); fr[9].ballPos = Vec(0, 0, ballZ);
		for (size_t i = 0; i < fr.size(); i++) if (i != 9) { fr[i].carPos = Vec(0, 0, carZ); fr[i].ballPos = Vec(0, 0, std::max(ballZ, 400.f)); }
		return Sum(Run(r, fr));
	};
	CHECK_NEAR(first(1000, 1100), 0.1f, 1e-6f);
	CHECK_EQ(first(300, 1100), 0.0f);                       // car exactly 300: not above
	CHECK_NEAR(first(301, 1100), 0.1f, 1e-6f);
	CHECK_EQ(first(1000, 150), 0.0f);                       // ball exactly 150: not in the air
	CHECK_NEAR(first(1000, 151), 0.1f, 1e-6f);
}

TEST(MultiAir_the_ball_landing_resets_the_chain_and_pays_nothing_that_step) {
	MultiAirTouchReward r;
	std::vector<MA> fr = TouchesAt(60, { 9, 17, 26 });
	for (int i = 19; i < 24; i++) fr[i].ballPos = Vec(0, 0, 100);   // the ball drops under 150 for a few steps
	fr[25].touch = true;                                             // touch on step 26 after the ball is back up
	auto v = Run(r, fr);
	CHECK_NEAR(v[16], 0.4f, 1e-6f);
	CHECK_NEAR(v[25], 0.1f, 1e-6f);                                  // the chain was reset by the landing: a first touch again
}

// Opponent turnover: when state.lastTouchCarID belongs to the other team while a chain is open, the chain clears. A teammate's touch does not.
TEST(MultiAir_an_opponent_touch_clears_the_chain_but_a_teammates_does_not) {
	auto run = [](Team lastToucherTeam) {
		MultiAirTouchReward r;
		std::vector<GameState> st;
		for (int k = 0; k < 40; k++) {
			GameState gs; gs.deltaTime = DT;
			Player me = MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 1000)); me.ballTouchedStep = (k == 8 || k == 16 || k == 24); me.isOnGround = false; me.boost = 0; me.rotMat = RotMat::GetIdentity();
			Player other = MakePlayer(2, 1, lastToucherTeam, Vec(500, 0, 1000)); other.isOnGround = false;
			gs.players.push_back(me); gs.players.push_back(other);
			gs.ball.pos = Vec(0, 0, 1100);
			gs.lastTouchCarID = (k >= 12 && k < 16) ? 2 : 1;   // the other car touched on steps 12-15, then I touch again on step 16
			st.push_back(gs);
		}
		for (size_t i = 1; i < st.size(); i++) st[i].prev = &st[i - 1];
		r.Reset(st[0]);
		std::vector<float> out;
		for (auto& s : st) { r.PreStep(s); out.push_back(r.GetAllRewards(s, false)[0]); }
		return out;
	};
	auto opp = run(Team::ORANGE), mate = run(Team::BLUE);
	CHECK_NEAR(opp[8], 0.1f, 1e-6f);
	CHECK_NEAR(opp[16], 0.1f, 1e-6f);                       // an opponent touched in between: the 2nd touch is a fresh 1st
	CHECK_NEAR(mate[16], 0.4f, 1e-6f);                      // a teammate touched in between: the chain continues
}

TEST(MultiAir_horizontal_ball_speed_over_2300_halves_the_reward_unless_near_the_opponent_goal) {
	auto paid = [](Vec ballPos, float horizSpeed) {
		MultiAirTouchReward r;
		auto fr = TouchesAt(20, { 10 });
		for (auto& f : fr) { f.ballPos = ballPos; f.ballVel = Vec(horizSpeed, 0, 0); }
		return Sum(Run(r, fr));
	};
	CHECK_NEAR(paid(Vec(0, 0, 1100), 2300), 0.1f, 1e-6f);                      // at the limit: no penalty
	CHECK_NEAR(paid(Vec(0, 0, 1100), 2650), 0.1f * 0.75f, 1e-6f);              // halfway to 3000: x0.75
	CHECK_NEAR(paid(Vec(0, 0, 1100), 3000), 0.1f * 0.5f, 1e-6f);               // at 3000 and above: x0.5
	CHECK_NEAR(paid(Vec(0, 4500, 1100), 3000), 0.1f, 1e-6f);                   // within 3000 uu of the orange goal back (6000): waived
}

// Bonus arithmetic (boost 100 -> multiplier 1.0): first touch, ball directly above the roof, car and ball moving together, the ball gaining lift.
//   contact angle: desired 0.35 at midfield, actual 0 -> 0.25 * exp(-0.35^2 / 0.32) = 0.25 * 0.68180 = 0.17045
//   softness: |car vel - ball vel| = 0 -> 0.15
//   z-lift: dz = 3, dvz = 400 -> (403)/2 = 201.5 -> 0.25 * 0.403 = 0.10075
//   total = 0.1 + 0.17045 + 0.15 + 0.10075 = 0.5212
TEST(MultiAir_bonuses_at_full_boost_hand_values) {
	auto build = [](float boost) {
		MultiAirTouchReward r;
		std::vector<GameState> st(2);
		for (int k = 0; k < 2; k++) {
			st[k].deltaTime = DT;
			Player p = MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 1000)); p.isOnGround = false; p.boost = boost;
			p.rotMat = RotMat::GetIdentity(); p.rotMat.up = Vec(0, 0, 1);
			p.vel = k == 0 ? Vec(0, 0, 0) : Vec(0, 0, 400);
			p.ballTouchedStep = (k == 1);
			st[k].players.push_back(p);
			st[k].ball.pos = k == 0 ? Vec(0, 0, 1090) : Vec(0, 0, 1093);
			st[k].ball.vel = k == 0 ? Vec(0, 0, 0) : Vec(0, 0, 400);
		}
		st[1].prev = &st[0];
		// 8 idle steps so the 0.5 s start-up passes: only the last frame matters, so advance the clock directly
		r.Reset(st[0]);
		for (int i = 0; i < 8; i++) r.PreStep(st[0]);
		r.PreStep(st[1]);
		return r.GetReward(st[1].players[0], st[1], false);
	};
	CHECK_NEAR(build(100), 0.5212f, 1e-3f);
	CHECK_NEAR(build(25), 0.1f + 0.5f * 0.42125f, 1e-3f);    // boost 25 halves the bonuses: 0.3106
	CHECK_NEAR(build(0), 0.1f, 1e-6f);                       // no boost: the base tier only
}

// FARMING (a finding): the reward is a sum of events with no cap and no refund, so a juggle that keeps the ball up pays every half second
// forever. 20 touches 0.533 s apart (a 10 s juggle) pay 0.1 + 0.4 + 18 * 0.7 = 13.1 raw with boost 0, versus a ceiling of 1.0 for the
// potential ConsecutiveAirTouchReward.
TEST(MultiAir_an_endless_juggle_pays_without_limit_FINDING) {
	MultiAirTouchReward r;
	std::vector<int> steps; for (int i = 0; i < 20; i++) steps.push_back(9 + 8 * i);
	auto v = Run(r, TouchesAt(9 + 8 * 20 + 2, steps));
	CHECK_NEAR(Sum(v), 0.1f + 0.4f + 18 * 0.7f, 1e-4f);
	CHECK(Sum(v) > 10.0f);
	// at the commented-out v3 weight 50 that is +655 weighted, 4 goals, for ten seconds of juggling
	CHECK(50.f * Sum(v) > 600.f);
}

TEST(MultiAir_a_lone_air_touch_pays_0p1_where_ConsecutiveAirTouch_pays_zero) {
	MultiAirTouchReward m;
	auto v = Run(m, TouchesAt(30, { 10 }));
	CHECK_NEAR(Sum(v), 0.1f, 1e-6f);
	CHECK_EQ(ConsecutiveAirTouchReward::Phi(1.0f), 0.0f);
}

TEST(MultiAir_the_touch_counter_is_exposed_per_car) {
	MultiAirTouchReward r;
	Run(r, TouchesAt(40, { 9, 17, 25 }));
	CHECK_EQ(r.GetCurrentTouchCount(1), 3);
	CHECK_EQ(r.GetCurrentTouchCount(99), 0);
}

// Wiring as in the v3 stack: ZeroSum(MultiAir, 0.3), 2v2. Blue car 0 makes the 2nd touch of a chain: r = 0.4.
//   car 0: 0.4 * 0.7 + 0.3 * 0.2 = 0.34; teammate 0.06; each orange car -0.2
TEST(MultiAir_zero_sum_2v2_values_at_team_spirit_0p3) {
	Reward* zs = new ZeroSumReward(new MultiAirTouchReward(), 0.3f);
	std::vector<GameState> st;
	for (int k = 0; k < 30; k++) {
		GameState gs; gs.deltaTime = DT;
		for (int i = 0; i < 4; i++) {
			Player p = MakePlayer(1 + i, i, i < 2 ? Team::BLUE : Team::ORANGE, Vec(i * 100.f, 0, 1000)); p.isOnGround = false; p.boost = 0;
			p.rotMat = RotMat::GetIdentity();
			p.ballTouchedStep = (i == 0 && (k == 8 || k == 16));
			gs.players.push_back(p);
		}
		gs.ball.pos = Vec(0, 0, 1100); gs.lastTouchCarID = 1;
		st.push_back(gs);
	}
	for (size_t i = 1; i < st.size(); i++) st[i].prev = &st[i - 1];
	zs->Reset(st[0]);
	std::vector<float> v;
	for (size_t k = 0; k < st.size(); k++) { zs->PreStep(st[k]); auto w = zs->GetAllRewards(st[k], false); if (k == 16) v = w; }
	CHECK_NEAR(v[0], 0.34f, 1e-5f);
	CHECK_NEAR(v[1], 0.06f, 1e-5f);
	CHECK_NEAR(v[2], -0.2f, 1e-5f);
	CHECK_NEAR(v[3], -0.2f, 1e-5f);
	CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-5f);
	delete zs;
}
