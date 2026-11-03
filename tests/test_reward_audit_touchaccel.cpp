#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

using namespace RLGC;
using namespace RewardTest;

// Audit of TouchAccelReward (weight 10, wrapped ZeroSum(TEAM_SPIRIT 0.3)). On a step where the player's ballTouchedStep is set it pays
//   max(0, min(1, |ball.vel| / M) - min(1, |prev.ball.vel| / M))      with M = KPHToVel(130) = 130 * 250/9 = 3611.11 uu/s
// i.e. the increase in the capped speed fraction between the previous step and this one; 0 with no previous state, no touch, or a
// touch that did not speed the ball up. Direction is ignored (|v|, not the component toward the opposing goal).
// Hand values at M = 3611.111: 1000 uu/s -> 0.276923;  1500 -> 0.415385;  3000 -> 0.830769;  500 -> 0.138462.
// Fixture: fabricated states (no engine). The engine side (what a real touch does to the ball speed, how many steps a contact lasts) is in
// tools/reward_lab/packs/touch_accel.

namespace {
	constexpr float M = 130.f * 250.f / 9.f;

	GameState Make(Vec ballVel, int perTeam, int toucher = -1, bool both = false) {
		GameState gs; gs.deltaTime = STEP_DT;
		for (int i = 0; i < perTeam; i++) {
			gs.players.push_back(MakePlayer(1 + 2 * i, 2 * i, Team::BLUE, Vec(-500.f, 300.f * i, 17)));
			gs.players.push_back(MakePlayer(2 + 2 * i, 2 * i + 1, Team::ORANGE, Vec(500.f, 300.f * i, 17)));
		}
		gs.ball.pos = Vec(0, 0, 93); gs.ball.vel = ballVel;
		if (toucher >= 0) gs.players[toucher].ballTouchedStep = true;
		if (both) for (auto& p : gs.players) p.ballTouchedStep = true;
		return gs;
	}

	// raw reward for player 0 for a (prev speed -> cur speed) transition along +y, touched by player 0 or not
	float Step(float prevSpeed, float curSpeed, bool touched = true) {
		TouchAccelReward r;
		GameState a = Make(Vec(0, prevSpeed, 0), 1);
		GameState b = Make(Vec(0, curSpeed, 0), 1, touched ? 0 : -1);
		b.prev = &a;
		return r.GetReward(b.players[0], b, false);
	}
}

TEST(TouchAccel_the_cap_is_130_kph_in_uu_per_s) {
	CHECK_NEAR(TouchAccelReward::MAX_REWARDED_BALL_SPEED, 3611.111f, 0.01f);
}

TEST(TouchAccel_pays_the_increase_in_speed_fraction_on_a_touch) {
	CHECK_NEAR(Step(0, 1000), 0.276923f, 1e-5f);
	CHECK_NEAR(Step(1000, 2000), 0.276923f, 1e-5f);          // linear: the same 1000 uu/s gain pays the same anywhere below the cap
	CHECK_NEAR(Step(500, 1500), 0.276923f, 1e-5f);
	CHECK_NEAR(Step(0, 1500), 0.415385f, 1e-5f);
}

TEST(TouchAccel_is_capped_at_130_kph_so_the_total_for_a_full_speed_up_is_one) {
	CHECK_NEAR(Step(3000, 4000), 1.0f - 3000.f / M, 1e-5f);  // only the part below the cap: 1 - 0.830769 = 0.169231
	CHECK_NEAR(Step(0, 9999), 1.0f, 1e-6f);                  // one huge hit pays 1.0, not more
	CHECK_NEAR(Step(M, 6000), 0.0f, 1e-6f);                          // already at the cap: nothing left to earn
	CHECK_EQ(Step(4000, 5000), 0.0f);                        // both above the cap
}

TEST(TouchAccel_pays_nothing_when_the_touch_does_not_speed_the_ball_up) {
	CHECK_EQ(Step(1500, 1000), 0.0f);                        // slowed
	CHECK_EQ(Step(1000, 1000), 0.0f);                        // unchanged
}

TEST(TouchAccel_pays_nothing_without_a_touch_however_fast_the_ball_gets) {
	CHECK_EQ(Step(0, 2000, /*touched=*/false), 0.0f);
}

TEST(TouchAccel_pays_nothing_without_a_previous_state) {
	TouchAccelReward r;
	GameState b = Make(Vec(0, 2000, 0), 1, 0);
	CHECK(b.prev == nullptr);
	CHECK_EQ(r.GetReward(b.players[0], b, false), 0.0f);
}

// Direction-blind (REWARD_SHAPING D5): a hit into the own goal and a hit that merely reverses a fast ball are paid by speed alone
TEST(TouchAccel_ignores_the_direction_of_the_ball) {
	TouchAccelReward r;
	GameState a = Make(Vec(0, 1000, 0), 1);
	GameState b = Make(Vec(0, -1500, 0), 1, 0);               // reversed and faster: speed 1000 -> 1500
	b.prev = &a;
	CHECK_NEAR(r.GetReward(b.players[0], b, false), 500.f / M, 1e-5f);   // 0.138462
	GameState c = Make(Vec(0, -1000, 0), 1, 0);               // a pure reversal at the same speed pays nothing
	c.prev = &a;
	CHECK_EQ(r.GetReward(c.players[0], c, false), 0.0f);
	GameState d = Make(Vec(1000, 0, 0), 1, 0);               // a sideways redirect of the same speed pays nothing too
	d.prev = &a;
	CHECK_EQ(r.GetReward(d.players[0], d, false), 0.0f);
	// the z component counts in the speed: popping the ball up pays like any other direction
	GameState e = Make(Vec(0, 0, 1000), 1, 0);
	GameState z = Make(Vec(0, 0, 0), 1);
	e.prev = &z;
	CHECK_NEAR(r.GetReward(e.players[0], e, false), 1000.f / M, 1e-5f);
}

// Every player whose ballTouchedStep is set is paid the same full increment: no splitting between two touchers
TEST(TouchAccel_a_simultaneous_touch_pays_every_toucher_the_full_increment) {
	TouchAccelReward r;
	GameState a = Make(Vec(0, 0, 0), 1);
	GameState b = Make(Vec(0, 1000, 0), 1, -1, /*both=*/true);
	b.prev = &a;
	auto v = r.GetAllRewards(b, false);
	CHECK_NEAR(v[0], 0.276923f, 1e-5f); CHECK_NEAR(v[1], 0.276923f, 1e-5f);
}

// A chain of touch steps with the speed rising each step telescopes to the final fraction (the "total is 1.0" claim in the source comment)
TEST(TouchAccel_a_monotone_chain_telescopes_to_the_final_speed_fraction) {
	TouchAccelReward r;
	float speeds[] = { 0, 400, 900, 1700, 2600, 3400, 5000 };
	std::vector<GameState> st;
	for (float s : speeds) st.push_back(Make(Vec(0, s, 0), 1, 0));
	float total = 0;
	for (size_t i = 1; i < st.size(); i++) { st[i].prev = &st[i - 1]; total += r.GetReward(st[i].players[0], st[i], false); }
	CHECK_NEAR(total, 1.0f, 1e-5f);
}

// FINDING: the reward is a ratchet, not a potential. Speed gained is paid again after the ball loses it (touch-free slowdown by drag, gravity
// on a roll, a wall, an opponent), so a 0 -> 1000 -> 500 -> 1500 sequence pays 0.2769 + 0 + 0.2769 = 0.5538, more than the final fraction 0.4154.
TEST(TouchAccel_FINDING_speed_lost_between_touches_is_paid_again) {
	TouchAccelReward r;
	float speeds[] = { 0, 1000, 500, 1500 };        // the drop 1000 -> 500 is a step with no touch by this player
	std::vector<GameState> st;
	for (int i = 0; i < 4; i++) st.push_back(Make(Vec(0, speeds[i], 0), 1, i == 2 ? -1 : 0));
	float total = 0;
	for (size_t i = 1; i < st.size(); i++) { st[i].prev = &st[i - 1]; total += r.GetReward(st[i].players[0], st[i], false); }
	CHECK_NEAR(total, 2.f * 1000.f / M, 1e-5f);     // 0.553846
	CHECK(total > 1500.f / M);
}

// Wiring as in training: ZeroSum(TouchAccel, 0.3), weight 10.  1v1: toucher +x, opponent -x.  2v2: 0.85x, 0.15x, -0.5x, -0.5x
TEST(TouchAccel_zero_sum_values_1v1_and_2v2_at_team_spirit_0p3_weight_10) {
	const float x = 1000.f / M;                     // 0.276923
	{
		Reward* zs = new ZeroSumReward(new TouchAccelReward(), 0.3f);
		GameState a = Make(Vec(0, 0, 0), 1); zs->Reset(a);
		GameState b = Make(Vec(0, 1000, 0), 1, 0); b.prev = &a; zs->PreStep(b);
		auto v = zs->GetAllRewards(b, false);
		CHECK_NEAR(v[0], x, 1e-6f); CHECK_NEAR(v[1], -x, 1e-6f);
		CHECK_NEAR(10.f * v[0], 2.76923f, 1e-4f);
		delete zs;
	}
	{
		Reward* zs = new ZeroSumReward(new TouchAccelReward(), 0.3f);
		GameState a = Make(Vec(0, 0, 0), 2); zs->Reset(a);
		GameState b = Make(Vec(0, 1000, 0), 2, 0); b.prev = &a; zs->PreStep(b);
		auto v = zs->GetAllRewards(b, false);
		CHECK_NEAR(v[0], 0.85f * x, 1e-6f); CHECK_NEAR(v[2], 0.15f * x, 1e-6f);
		CHECK_NEAR(v[1], -0.5f * x, 1e-6f); CHECK_NEAR(v[3], -0.5f * x, 1e-6f);
		CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-6f);
		delete zs;
	}
}
