#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

using namespace RLGC;
using namespace RewardTest;

// Audit of GuardedPickupBoostReward (weight 6, wrapped ZeroSum(TEAM_SPIRIT 0.3)). Reads only the player's own boost (0..100) and its previous step:
//   0 when player.prev is missing, or the player is demolished now, or was demolished on the previous step (respawn refill is not a pickup);
//   sqrt(b / 100) - sqrt(b_prev / 100) when b > b_prev;  0 otherwise (spending boost costs nothing).
// Hand values: small pad (+12) from 0 -> sqrt(0.12) = 0.346410;  big pad (+100) from 0 -> 1.0;  small pad from 50 -> sqrt(0.62) - sqrt(0.5) = 0.080290;
// small pad from 88 -> 1 - sqrt(0.88) = 0.061918 (so a small pad is worth 5.595x more from empty than from 88).
// Fixture: fabricated states (no engine). The engine side (pad amounts, cooldowns, who gets a contested pad, the demo respawn) is in tools/reward_lab/packs/guarded_pickup.

namespace {
	// One frame = every player's boost and demolished flag; players in order blue0, orange0, blue1, orange1
	struct F { std::vector<float> boost; std::vector<int> demoed; };

	std::vector<GameState> Build(const std::vector<F>& frames, int perTeam) {
		std::vector<GameState> st;
		for (auto& f : frames) {
			GameState gs; gs.deltaTime = STEP_DT;
			for (int i = 0; i < perTeam; i++) {
				gs.players.push_back(MakePlayer(1 + 2 * i, 2 * i, Team::BLUE, Vec(-1000.f, 300.f * i, 17)));
				gs.players.push_back(MakePlayer(2 + 2 * i, 2 * i + 1, Team::ORANGE, Vec(1000.f, 300.f * i, 17)));
			}
			for (size_t k = 0; k < gs.players.size(); k++) {
				gs.players[k].boost = f.boost[k];
				gs.players[k].isDemoed = k < f.demoed.size() && f.demoed[k];
			}
			st.push_back(gs);
		}
		for (size_t i = 1; i < st.size(); i++) {
			st[i].prev = &st[i - 1];
			for (size_t k = 0; k < st[i].players.size(); k++) st[i].players[k].prev = &st[i - 1].players[k];
		}
		return st;
	}

	std::vector<std::vector<float>> Run(const std::vector<F>& frames, int perTeam = 1, Reward* use = nullptr) {
		GuardedPickupBoostReward local; Reward& r = use ? *use : local;
		auto st = Build(frames, perTeam);
		r.Reset(st[0]);
		std::vector<std::vector<float>> out;
		for (auto& s : st) { r.PreStep(s); out.push_back(r.GetAllRewards(s, false)); }
		return out;
	}

	// reward of player 0 for a boost change b0 -> b1 (1v1, orange idle at 60)
	float Step(float b0, float b1) { return Run({ F{ { b0, 60 }, {} }, F{ { b1, 60 }, {} } })[1][0]; }

	float R(float b) { return sqrtf(b / 100.f); }
}

TEST(GuardedPickup_a_small_pad_from_empty_pays_sqrt_of_0p12) {
	CHECK_NEAR(Step(0, 12), 0.346410f, 1e-5f);
}

TEST(GuardedPickup_a_big_pad_from_empty_pays_exactly_one) {
	CHECK_NEAR(Step(0, 100), 1.0f, 1e-6f);
}

TEST(GuardedPickup_the_same_pad_is_worth_5p6_times_more_from_empty_than_from_88) {
	float low = Step(0, 12), high = Step(88, 100);
	CHECK_NEAR(high, 1.f - sqrtf(0.88f), 1e-5f);                 // 0.061918
	CHECK_NEAR(low / high, 5.595f, 0.01f);                       // the D3 diminishing return, measured on the formula
	CHECK_NEAR(Step(50, 62), sqrtf(0.62f) - sqrtf(0.5f), 1e-6f); // 0.080290: the middle of the range
}

TEST(GuardedPickup_a_pad_taken_at_full_boost_pays_nothing) {
	CHECK_EQ(Step(100, 100), 0.0f);
}

TEST(GuardedPickup_spending_boost_costs_nothing_and_no_change_pays_nothing) {
	CHECK_EQ(Step(100, 0), 0.0f);
	CHECK_EQ(Step(60, 59.9f), 0.0f);
	CHECK_EQ(Step(60, 60), 0.0f);
}

TEST(GuardedPickup_pays_nothing_without_a_previous_player_state) {
	GuardedPickupBoostReward r;
	GameState gs; gs.deltaTime = STEP_DT;
	gs.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 17))); gs.players[0].boost = 100;
	CHECK(gs.players[0].prev == nullptr);
	CHECK_EQ(r.GetReward(gs.players[0], gs, false), 0.0f);
}

TEST(GuardedPickup_a_demolished_car_is_paid_nothing_even_if_its_boost_rises) {
	auto v = Run({ F{ { 0, 60 }, { 1, 0 } }, F{ { 100, 60 }, { 1, 0 } } });
	CHECK_EQ(v[1][0], 0.0f);
}

// The step the car is demolished (not demolished before, demolished now): a boost rise on that very step is not paid either
TEST(GuardedPickup_the_step_a_car_is_demolished_pays_nothing_even_if_its_boost_rises) {
	auto v = Run({ F{ { 0, 60 }, { 0, 0 } }, F{ { 100, 60 }, { 1, 0 } } });
	CHECK_EQ(v[1][0], 0.0f);
}

// The respawn refill: the step the car comes back its boost jumps (0 -> 33 in the engine's default), and isDemoed is already false but
// the PREVIOUS step it was true. That step pays 0; the step after it pays normally again.
TEST(GuardedPickup_the_respawn_refill_step_pays_nothing_and_the_next_pickup_pays_normally) {
	auto v = Run({ F{ { 0, 60 }, { 1, 0 } }, F{ { 33, 60 }, { 0, 0 } }, F{ { 45, 60 }, { 0, 0 } } });
	CHECK_EQ(v[1][0], 0.0f);                                     // refill is not a pickup
	CHECK_NEAR(v[2][0], R(45) - R(33), 1e-6f);                   // 0.670820 - 0.574456 = 0.096364
}

// The boost circuit: burning the tank is free and refilling it pays again every time, so a 100 -> 0 -> 100 cycle is worth +1.0 each lap
TEST(GuardedPickup_a_burn_and_refill_cycle_pays_one_per_lap) {
	auto v = Run({ F{ { 100, 60 }, {} }, F{ { 0, 60 }, {} }, F{ { 100, 60 }, {} }, F{ { 0, 60 }, {} }, F{ { 100, 60 }, {} } });
	float sum = 0; for (auto& row : v) sum += row[0];
	CHECK_NEAR(sum, 2.0f, 1e-6f);
	CHECK_EQ(v[1][0], 0.0f); CHECK_NEAR(v[2][0], 1.0f, 1e-6f);
}

// Partial laps too: 50 -> 0 (spent) -> 50 (a pad) pays R(50) = 0.707107 every lap, with no memory of having been paid for it
TEST(GuardedPickup_a_pickup_is_paid_again_for_boost_that_was_spent_since_the_last_one) {
	auto v = Run({ F{ { 0, 60 }, {} }, F{ { 50, 60 }, {} }, F{ { 0, 60 }, {} }, F{ { 50, 60 }, {} } });
	CHECK_NEAR(v[1][0], R(50), 1e-6f);
	CHECK_NEAR(v[3][0], R(50), 1e-6f);
}

// Without any spending the pickups telescope: 0 -> 12 -> 24 -> ... -> 96 -> 100 pays exactly R(100) - R(0) = 1.0
TEST(GuardedPickup_pickups_with_no_spending_in_between_telescope_to_one) {
	std::vector<F> fr; for (float b : { 0.f, 12.f, 24.f, 36.f, 48.f, 60.f, 72.f, 84.f, 96.f, 100.f }) fr.push_back(F{ { b, 60 }, {} });
	float sum = 0; for (auto& row : Run(fr)) sum += row[0];
	CHECK_NEAR(sum, 1.0f, 1e-5f);
}

TEST(GuardedPickup_reads_only_the_players_own_boost) {
	auto v = Run({ F{ { 50, 0 }, {} }, F{ { 50, 100 }, {} } });
	CHECK_EQ(v[1][0], 0.0f);                                     // the opponent's pickup pays the blue car nothing here
	CHECK_NEAR(v[1][1], 1.0f, 1e-6f);
}

// Wiring as in training: ZeroSum(GuardedPickupBoost, 0.3), weight 6.   1v1: the taker +x, the opponent -x.  2v2: 0.85x, 0.15x, -0.5x, -0.5x
TEST(GuardedPickup_zero_sum_values_1v1_and_2v2_at_team_spirit_0p3_weight_6) {
	const float x = 0.346410f;                                   // a small pad from empty
	for (int perTeam = 1; perTeam <= 2; perTeam++) {
		Reward* zs = new ZeroSumReward(new GuardedPickupBoostReward(), 0.3f);
		std::vector<float> b0(2 * perTeam, 60.f), b1 = b0; b0[0] = 0; b1[0] = 12;
		auto v = Run({ F{ b0, {} }, F{ b1, {} } }, perTeam, zs)[1];
		if (perTeam == 1) {
			CHECK_NEAR(v[0], x, 1e-5f); CHECK_NEAR(v[1], -x, 1e-5f);
			CHECK_NEAR(6.f * v[0], 2.07846f, 1e-4f);
		} else {
			CHECK_NEAR(v[0], 0.85f * x, 1e-5f); CHECK_NEAR(v[2], 0.15f * x, 1e-5f);
			CHECK_NEAR(v[1], -0.5f * x, 1e-5f); CHECK_NEAR(v[3], -0.5f * x, 1e-5f);
			CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-5f);
		}
		delete zs;
	}
}

// Stealing a pad the other car was about to take: the thief is paid x and the other is charged x, and a same-step tie cancels
TEST(GuardedPickup_zero_sum_a_contested_pad_goes_to_one_car_and_a_tie_cancels) {
	Reward* zs = new ZeroSumReward(new GuardedPickupBoostReward(), 0.3f);
	auto v = Run({ F{ { 0, 0 }, {} }, F{ { 12, 12 }, {} } }, 1, zs)[1];
	CHECK_NEAR(v[0], 0.0f, 1e-6f); CHECK_NEAR(v[1], 0.0f, 1e-6f);
	delete zs;
}

// The reason for the guard: without it a respawned victim would be PAID 0 -> 33 raw and ZeroSum would CHARGE the demoer for it
TEST(GuardedPickup_zero_sum_the_victims_respawn_refill_does_not_charge_the_demoer) {
	Reward* zs = new ZeroSumReward(new GuardedPickupBoostReward(), 0.3f);
	auto v = Run({ F{ { 60, 0 }, { 0, 1 } }, F{ { 60, 33 }, { 0, 0 } } }, 1, zs)[1];
	CHECK_EQ(v[0], 0.0f); CHECK_EQ(v[1], 0.0f);
	delete zs;
}
