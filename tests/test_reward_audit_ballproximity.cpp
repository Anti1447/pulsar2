#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

using namespace RLGC;
using namespace RewardTest;

// Audit of BallProximityPotentialReward (weight 4, wrapped ZeroSum(TEAM_SPIRIT 0.3), gamma = TRAIN_GAMMA 0.9969). Per step, for a player's team:
//   r = gamma * Phi(now) - Phi(prev),   Phi(team) = max over the team's ALIVE cars of  exp(-d / 1410) + 0.08 * max(0, 1 - d / 12000),  d = 3D car-ball distance.
// 0 on the first step (no state.prev), and 0 on any step where a car of the player's team changed its demolished state (or has no prev).
// Hand values of Phi: d=0 -> 1.08;  110 -> 1.004218;  1000 -> 0.565361;  1410 -> 0.438479;  3000 -> 0.179116;  6000 -> 0.054189;  10000 -> 0.014165.
// Fixture: fabricated states (no engine). The ZeroSum/2v2 sharing is also covered in tests/test_team_rewards.cpp (the BallProx_* tests, whose oracle restates Phi).

namespace {
	constexpr float G = 0.9969f;

	// A frame: ball position and every car's position/demolished flag; 1v1 order blue0, orange0 (2v2: blue0, orange0, blue1, orange1)
	struct F { Vec ball; std::vector<Vec> car; std::vector<int> demoed; };

	std::vector<GameState> Build(const std::vector<F>& frames) {
		std::vector<GameState> st;
		for (auto& f : frames) {
			GameState gs; gs.deltaTime = STEP_DT; gs.ball.pos = f.ball;
			for (size_t k = 0; k < f.car.size(); k++) {
				gs.players.push_back(MakePlayer(1 + (uint32_t)k, (int)k, (k % 2 == 0) ? Team::BLUE : Team::ORANGE, f.car[k]));
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

	std::vector<std::vector<float>> Run(const std::vector<F>& frames, float gamma = G, Reward* use = nullptr) {
		BallProximityPotentialReward local(gamma); Reward& r = use ? *use : local;
		auto st = Build(frames);
		r.Reset(st[0]);
		std::vector<std::vector<float>> out;
		for (auto& s : st) { r.PreStep(s); out.push_back(r.GetAllRewards(s, false)); }
		return out;
	}

	// 1v1 with the ball at the origin (z 93) and blue d0 -> d1 away along x; orange parked 6000 away on the other side
	float Step(float d0, float d1, float gamma = G) {
		Vec O(0, 0, 93);
		return Run({ F{ O, { Vec(-d0, 0, 93), Vec(6000, 0, 93) }, {} }, F{ O, { Vec(-d1, 0, 93), Vec(6000, 0, 93) }, {} } }, gamma)[1][0];
	}
}

TEST(BallProx_potential_anchor_values_of_the_closest_car) {
	GameState gs; gs.ball.pos = Vec(0, 0, 93);
	struct A { float d, phi; } as[] = { { 0, 1.08f }, { 110, 1.004218f }, { 1000, 0.565361f }, { 1410, 0.438479f }, { 3000, 0.179116f }, { 6000, 0.054189f }, { 10000, 0.014165f } };
	for (auto& a : as) {
		gs.players.clear(); gs.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(a.d, 0, 93)));
		CHECK_NEAR(BallProximityPotentialReward::TeamPhi(gs, Team::BLUE), a.phi, 2e-5f);
	}
}

TEST(BallProx_the_far_field_slope_never_dies_inside_12000_and_the_tail_beyond_is_exp_only) {
	// 1000 uu closer, gamma 1: at 8000 -> 7000 the linear part alone is 0.08/12 = 0.006667 and the whole step is 0.008412 (measured at d 8000/9000)
	CHECK_NEAR(Step(9000, 8000, 1.f), 0.008412f, 2e-5f);
	CHECK(Step(9000, 8000, 1.f) > 0.0066f);
	CHECK_NEAR(Step(14000, 13000, 1.f), 0.0000487f, 1e-5f);        // beyond 12000 only the exp tail is left: no gradient there
}

TEST(BallProx_closing_pays_and_the_values_are_hand_checked_at_gamma_0p9969) {
	CHECK_NEAR(Step(3000, 2847), 0.014072f, 1e-5f);                 // 153 uu is one decision step at 2300 uu/s
	CHECK_NEAR(Step(1500, 1347), 0.039165f, 1e-5f);
	CHECK_NEAR(Step(200, 47),   0.097234f, 1e-5f);                  // the steepest part is at the ball
}

TEST(BallProx_a_ball_knocked_away_is_charged_at_once) {
	CHECK_NEAR(Step(100, 700), -0.328970f, 1e-5f);                  // 0.9969 * 0.684019 - 1.010868
}

TEST(BallProx_standing_still_pays_a_small_tax_of_one_minus_gamma_times_phi) {
	float r = Step(100, 100);
	CHECK_NEAR(r, -0.0031337f, 1e-6f);                              // (0.9969 - 1) * 1.010868
	CHECK_NEAR(4.f * r, -0.012535f, 1e-5f);                         // weighted at 4; over 225 steps (15 s) next to the ball = -2.82
	CHECK_NEAR(Step(6000, 6000), (G - 1.f) * 0.054189f, 1e-6f);   // and the tax is 20x smaller far away
}

TEST(BallProx_with_gamma_1_a_closed_loop_pays_zero_and_a_path_pays_the_potential_difference) {
	Vec O(0, 0, 93);
	std::vector<F> loop; for (float d : { 3000.f, 1000.f, 200.f, 1500.f, 3000.f }) loop.push_back(F{ O, { Vec(-d, 0, 93), Vec(6000, 0, 93) }, {} });
	float sum = 0; for (auto& row : Run(loop, 1.f)) sum += row[0];
	CHECK_NEAR(sum, 0.0f, 1e-5f);
	CHECK_NEAR(Step(3000, 0, 1.f), 0.900884f, 1e-5f);              // 1.08 - 0.179116; weighted 3.60
}

TEST(BallProx_the_distance_is_3D_a_ball_overhead_counts_the_height) {
	Vec B(0, 0, 1093);                                              // 1000 above a car at z 93
	GameState gs; gs.ball.pos = B; gs.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 93)));
	CHECK_NEAR(BallProximityPotentialReward::TeamPhi(gs, Team::BLUE), 0.565361f, 2e-5f);
}

TEST(BallProx_pays_nothing_on_the_first_step) {
	BallProximityPotentialReward r(G);
	GameState gs; gs.ball.pos = Vec(0, 0, 93); gs.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(-100, 0, 93)));
	CHECK(gs.prev == nullptr);
	CHECK_EQ(r.GetReward(gs.players[0], gs, false), 0.0f);
}

TEST(BallProx_a_demolished_team_has_zero_potential_and_a_dead_car_is_not_the_closest) {
	GameState gs; gs.ball.pos = Vec(0, 0, 93);
	gs.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(-110, 0, 93))); gs.players[0].isDemoed = true;
	CHECK_EQ(BallProximityPotentialReward::TeamPhi(gs, Team::BLUE), 0.0f);
	gs.players.push_back(MakePlayer(3, 2, Team::BLUE, Vec(-3000, 0, 93)));   // the alive teammate is the team's potential
	CHECK_NEAR(BallProximityPotentialReward::TeamPhi(gs, Team::BLUE), 0.179116f, 2e-5f);
}

// The respawn teleport is not an action: the step the car's demolished state flips pays 0 (both ways), and a steady demolished car reads 0 vs 0
TEST(BallProx_the_step_a_car_is_demolished_or_respawns_pays_zero_and_the_rest_reads_zero) {
	Vec O(0, 0, 93); Vec far(6000, 0, 93);
	auto v = Run({ F{ O, { Vec(-100, 0, 93), far }, { 0, 0 } },
	               F{ O, { Vec(-100, 0, 93), far }, { 1, 0 } },       // demolished now
	               F{ O, { Vec(-100, 0, 93), far }, { 1, 0 } },       // steady demolished
	               F{ O, { Vec(-3000, 0, 93), far }, { 0, 0 } },      // respawned far away
	               F{ O, { Vec(-2847, 0, 93), far }, { 0, 0 } } });   // then an ordinary closing step
	CHECK_EQ(v[1][0], 0.0f); CHECK_EQ(v[2][0], 0.0f); CHECK_EQ(v[3][0], 0.0f);
	CHECK_NEAR(v[4][0], 0.014072f, 1e-5f);
}

// An OPPONENT'S demolition does not skip my step: my reward is still my own potential change
TEST(BallProx_an_opponents_demolition_does_not_skip_my_step) {
	Vec O(0, 0, 93);
	auto v = Run({ F{ O, { Vec(-3000, 0, 93), Vec(2000, 0, 93) }, { 0, 0 } }, F{ O, { Vec(-2847, 0, 93), Vec(2000, 0, 93) }, { 0, 1 } } });
	CHECK_NEAR(v[1][0], 0.014072f, 1e-5f);
	CHECK_EQ(v[1][1], 0.0f);                                          // the demolished car's own step is skipped
}

// ZeroSum 1v1 (Psi = Phi_own - Phi_opp): blue closes 3000 -> 2847 while orange stands still 2000 away (its own tax is -0.000957)
TEST(BallProx_zero_sum_1v1_is_own_change_minus_the_opponents_change_weighted_at_4) {
	Reward* zs = new ZeroSumReward(new BallProximityPotentialReward(G), 0.3f);
	Vec O(0, 0, 93);
	auto v = Run({ F{ O, { Vec(-3000, 0, 93), Vec(2000, 0, 93) }, {} }, F{ O, { Vec(-2847, 0, 93), Vec(2000, 0, 93) }, {} } }, G, zs)[1];
	CHECK_NEAR(v[0], 0.015029f, 1e-5f); CHECK_NEAR(v[1], -0.015029f, 1e-5f);
	CHECK_NEAR(4.f * v[0], 0.060116f, 1e-4f);
	delete zs;
}

// 2v2: the second man earns nothing for crowding the ball. blue0 stays 500 away; blue1 closes 3000 -> 2847, still farther than blue0
TEST(BallProx_2v2_only_the_closest_team_car_moves_the_team_potential) {
	Vec O(0, 0, 93); Vec o(5000, 5000, 93);
	auto v = Run({ F{ O, { Vec(-500, 0, 93), o, Vec(-3000, 0, 93), o }, {} }, F{ O, { Vec(-500, 0, 93), o, Vec(-2847, 0, 93), o }, {} } })[1];
	CHECK_EQ(v[0], v[2]);                                             // both teammates read the same team value
	CHECK_NEAR(v[0], (G - 1.f) * (expf(-500.f / 1410.f) + 0.08f * (1.f - 500.f / 12000.f)), 1e-6f);   // just the hold tax of the nearer car
}
