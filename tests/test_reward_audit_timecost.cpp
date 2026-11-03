#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

#include <cmath>

using namespace RLGC;
using namespace RewardTest;

// Audit of TimeCostReward (weight 0.01, NOT wrapped in ZeroSum). The reward is the constant -1 for every player on every step: it reads nothing from the state.
// Hand values at weight 0.01: -0.01 per step; a 30 s episode is 450 steps at 15 Hz (tickSkip 8) = -4.5 undiscounted (3% of a Goal = 150); at gamma 0.9969 the
// discounted sum is 0.01 x (1 - 0.9969^450) / (1 - 0.9969) = 0.01 x 242.8075 = -2.4281; the infinite-horizon value is 0.01 / (1 - 0.9969) = -3.2258.
// Fixture: fabricated states (no engine). The engine side is in tools/reward_lab/packs/time_cost.

namespace {
	GameState Make(int perTeam) {
		GameState gs; gs.deltaTime = STEP_DT;
		for (int i = 0; i < perTeam; i++) {
			gs.players.push_back(MakePlayer(1 + 2 * i, 2 * i, Team::BLUE, Vec(-1000.f, 300.f * i, 17)));
			gs.players.push_back(MakePlayer(2 + 2 * i, 2 * i + 1, Team::ORANGE, Vec(1000.f, 300.f * i, 17)));
		}
		gs.ball.pos = Vec(0, 0, 93);
		return gs;
	}
}

TEST(TimeCost_is_minus_one_for_every_player_in_every_mode) {
	TimeCostReward r;
	for (int perTeam = 1; perTeam <= 3; perTeam++) {
		GameState gs = Make(perTeam);
		auto v = r.GetAllRewards(gs, false);
		CHECK_EQ((int)v.size(), 2 * perTeam);
		for (float x : v) CHECK_EQ(x, -1.0f);
	}
}

TEST(TimeCost_reads_nothing_from_the_state) {
	TimeCostReward r;
	GameState gs = Make(1);
	gs.players[0].ballTouchedStep = true; gs.players[0].boost = 100; gs.players[0].vel = Vec(2300, 0, 0);   // active, fast, touching
	gs.players[1].isDemoed = true; gs.players[1].boost = 0;                                                  // demolished
	gs.goalScored = true;                                                                                    // the goal step
	auto v = r.GetAllRewards(gs, false);
	CHECK_EQ(v[0], -1.0f); CHECK_EQ(v[1], -1.0f);                     // the demolished car and the goal step pay the same
	GameState far = Make(1); far.ball.pos = Vec(0, 5000, 93); far.players[0].pos = Vec(-4000, -5000, 17);
	CHECK_EQ(r.GetAllRewards(far, false)[0], -1.0f);
}

TEST(TimeCost_charges_the_final_step_and_needs_no_previous_state) {
	TimeCostReward r;
	GameState gs = Make(1);
	CHECK(gs.prev == nullptr && gs.players[0].prev == nullptr);
	CHECK_EQ(r.GetReward(gs.players[0], gs, true), -1.0f);            // the terminal step is charged too
	CHECK_EQ(r.GetReward(gs.players[0], gs, false), -1.0f);           // and the first step
}

TEST(TimeCost_weighted_values_at_0p01) {
	CHECK_NEAR(0.01f * -1.0f, -0.01f, 1e-9f);
	CHECK_NEAR(450 * 0.01f, 4.5f, 1e-5f);                              // a 30 s episode at 15 Hz
	CHECK_NEAR(450 * 0.01f / 150.f, 0.03f, 1e-6f);                     // 3% of a Goal
	// discounted at the learner's gamma: the geometric sums quoted in the header
	const double g = 0.9969;
	double s450 = (1 - pow(g, 450)) / (1 - g);
	CHECK_NEAR((float)s450, 242.8075f, 1e-3f);
	CHECK_NEAR((float)(0.01 * s450), 2.42808f, 1e-4f);
	CHECK_NEAR((float)(0.01 / (1 - g)), 3.22581f, 1e-4f);              // never ending: the value floor the term adds
}

// Not zero-sum on purpose: 1v1 sums to -2 per pair-step raw (-0.02 weighted). Wrapped in ZeroSum it would be exactly 0, which is why it is NOT wrapped.
TEST(TimeCost_is_not_zero_sum_and_a_zero_sum_wrapper_would_cancel_it_entirely) {
	GameState gs = Make(1);
	TimeCostReward r;
	auto raw = r.GetAllRewards(gs, false);
	CHECK_NEAR(raw[0] + raw[1], -2.0f, 1e-6f);
	for (int perTeam = 1; perTeam <= 3; perTeam++) {
		GameState g2 = Make(perTeam);
		Reward* zs = new ZeroSumReward(new TimeCostReward(), 0.3f);
		for (float x : zs->GetAllRewards(g2, false)) CHECK_NEAR(x, 0.0f, 1e-6f);
		delete zs;
	}
}

// Uniform: the difference between any two players (the league / PSD margin) is exactly 0, so it cannot favour anyone
TEST(TimeCost_cancels_in_every_difference_between_players) {
	TimeCostReward r;
	GameState gs = Make(3);
	auto v = r.GetAllRewards(gs, false);
	for (size_t i = 0; i < v.size(); i++) for (size_t j = 0; j < v.size(); j++) CHECK_EQ(v[i] - v[j], 0.0f);
}
