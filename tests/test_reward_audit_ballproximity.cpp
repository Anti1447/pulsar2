#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

using namespace RLGC;
using namespace RewardTest;

// Audit of BallProximityPotentialReward (weight 4, wrapped ZeroSum(TEAM_SPIRIT 0.3), gamma = TRAIN_GAMMA 0.9969). Per step, for a player's team:
//   r = gamma * Phi(now) - Phi(prev),   Phi(team) = sum_k w_k * phi(d_k) * 1.08 / MaxSum(N)   over the team's ALIVE cars sorted by ball distance,
//   phi(d) = exp(-d / 1410) + 0.08 * max(0, 1 - d / 12000),  d = 3D car-ball distance,  b = (1, 1/2, 1/4),  the sum is scaled by 1.08 / MaxSum(N).
//   w_m = b_m;  w_k = b_k - (b_k - w_{k+1}) * c_k,  c_k = 1 - smoothstep((d_{k+1} - d_k - 500) / 1100)  (spacing-aware since 2026-11-03).
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

TEST(BallProx_a_demolished_team_has_zero_potential_and_a_dead_car_is_not_counted) {
	GameState gs; gs.ball.pos = Vec(0, 0, 93);
	gs.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(-110, 0, 93))); gs.players[0].isDemoed = true;
	CHECK_EQ(BallProximityPotentialReward::TeamPhi(gs, Team::BLUE), 0.0f);
	gs.players.push_back(MakePlayer(3, 2, Team::BLUE, Vec(-3000, 0, 93)));   // the alive teammate alone, rank 1: phi(3000) x 1.08 / 1.27815
	CHECK_NEAR(BallProximityPotentialReward::TeamPhi(gs, Team::BLUE), 0.151348f, 2e-5f);
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

// 2v2, spaced (gap 2500 > 1600, no crowding): the potential is (phi(500) + 1/2 phi(3000)) x 1.08 / 1.27815 = 0.733158, parked: hold tax (gamma-1) x that
TEST(BallProx_2v2_spaced_pair_parked_pays_the_hold_tax_of_the_whole_team_potential) {
	Vec O(0, 0, 93); Vec o(5000, 5000, 93);
	auto v = Run({ F{ O, { Vec(-500, 0, 93), o, Vec(-3000, 0, 93), o }, {} }, F{ O, { Vec(-500, 0, 93), o, Vec(-3000, 0, 93), o }, {} } })[1];
	CHECK_EQ(v[0], v[2]);                                             // both teammates read the same team value
	CHECK_NEAR(v[0], (G - 1.f) * 0.733158f, 1e-6f);
}

// Team potential anchors (ball at the origin, cars on the x axis at z 93, so d is the x offset). Hand values from the formula above.
namespace {
	float TP(std::vector<float> ds, int total = -1) {
		GameState gs; gs.ball.pos = Vec(0, 0, 93);
		for (size_t k = 0; k < ds.size(); k++) gs.players.push_back(MakePlayer(1 + 2 * (uint32_t)k, 2 * (int)k, Team::BLUE, Vec(-ds[k], 0, 93)));
		for (int k = (int)ds.size(); k < total; k++) { gs.players.push_back(MakePlayer(100 + k, 2 * k, Team::BLUE, Vec(-9000, 0, 93))); gs.players.back().isDemoed = true; }
		return BallProximityPotentialReward::TeamPhi(gs, Team::BLUE);
	}
}

TEST(BallProx_2v2_a_spaced_pair_counts_the_second_car_at_half) {
	CHECK_NEAR(TP({ 200, 2000 }), 0.930146f, 2e-5f);                  // (0.946423 + 0.5 x 0.308758) x 1.08 / 1.27815
}

TEST(BallProx_2v2_a_crowded_pair_counts_both_at_half_so_it_is_worth_less_than_spaced) {
	CHECK_NEAR(TP({ 200, 300 }), 0.774318f, 2e-5f);                   // 0.5 x (0.946423 + 0.886345) x 1.08 / 1.27815
	CHECK(TP({ 200, 300 }) < TP({ 200, 2000 }));                      // even though the second car is much closer to the ball
}

TEST(BallProx_2v2_the_discount_is_smooth_between_500_and_1600) {
	CHECK_NEAR(TP({ 200, 1250 }), 0.804154f, 2e-5f);                  // gap 1050: crowding 0.5, weights (0.75, 0.5)
	CHECK_NEAR(BallProximityPotentialReward::Crowd(500.f), 1.f, 1e-6f);
	CHECK_NEAR(BallProximityPotentialReward::Crowd(1050.f), 0.5f, 1e-6f);
	CHECK_NEAR(BallProximityPotentialReward::Crowd(1600.f), 0.f, 1e-6f);
	CHECK_EQ(BallProximityPotentialReward::Crowd(0.f), 1.f);
	CHECK_EQ(BallProximityPotentialReward::Crowd(5000.f), 0.f);
}

// Continuity at a rank swap: two cars passing each other (gap 0) have equal weights, so the potential does not jump
TEST(BallProx_2v2_the_potential_is_continuous_when_two_cars_swap_rank) {
	CHECK_NEAR(TP({ 400, 400 }), 0.701609f, 2e-5f);
	CHECK_NEAR(TP({ 399, 401 }), TP({ 400, 400 }), 1e-3f);
	CHECK_NEAR(TP({ 401, 399 }), TP({ 399, 401 }), 1e-6f);            // order of the cars in the list does not matter
	CHECK_NEAR(TP({ 2000, 200 }), 0.930146f, 2e-5f);                  // the rank is by distance, not by the order of the players
	CHECK_NEAR(TP({ 3400, 200, 1800 }), 0.946261f, 2e-5f);
}

TEST(BallProx_3v3_ranks_weigh_1_half_quarter_when_spaced_and_collapse_when_crowded) {
	CHECK_NEAR(TP({ 200, 1800, 3400 }), 0.946261f, 2e-5f);            // weights (1, 1/2, 1/4), scaled by 1.08 / 1.32016
	CHECK_NEAR(TP({ 200, 300, 400 }), 0.544664f, 2e-5f);              // all crowded: 1/4 each
	CHECK_NEAR(TP({ 200, 300, 2300 }), 0.802933f, 2e-5f);             // cars 1 and 2 crowded: (1/2, 1/2, 1/4)
	CHECK_NEAR(TP({ 200, 1900, 2000 }), 0.90433f, 2e-5f);            // cars 2 and 3 crowded: (1, 1/4, 1/4)
}

// The normalisation per team size: the largest potential is 1.08 in every mode (user-directed). MaxSum(2) = 1.27815 and MaxSum(3) = 1.32016 were found by
// grid search; here the same search is repeated independently over the distances (car 1 at 0..400 so partial crowding is covered).
TEST(BallProx_the_largest_team_potential_is_1p08_in_every_mode) {
	CHECK_NEAR(TP({ 0 }, 1), 1.08f, 1e-5f);
	CHECK_NEAR(TP({ 0, 1553 }), 1.08f, 1e-4f);                        // one car on the ball, the other just outside full crowding
	CHECK_NEAR(TP({ 0, 1544, 3100 }), 1.08f, 1e-4f);
	float best2 = 0, best3 = 0;
	for (float d1 : { 0.f, 100.f, 250.f, 400.f })
		for (float d2 = 0; d2 <= 4000; d2 += 10) {
			best2 = fmaxf(best2, TP({ d1, d2 }));
			if (d2 < 3000) for (float d3 = 0; d3 <= 5000; d3 += 50) best3 = fmaxf(best3, TP({ d1, d2, d3 }));
		}
	CHECK(best2 <= 1.08f + 1e-4f); CHECK(best2 > 1.08f - 2e-3f);       // never above 1.08, and the cap is reached
	CHECK(best3 <= 1.08f + 1e-4f); CHECK(best3 > 1.08f - 3e-3f);
}

// A team with a car down is still normalised by the FULL team size: the lone survivor of three at 200 uu reads 0.946423 x 1.08 / 1.32016 = 0.774258
TEST(BallProx_a_missing_car_does_not_change_the_normalisation) {
	CHECK_NEAR(TP({ 200 }, 3), 0.774258f, 2e-5f);
	CHECK_NEAR(TP({ 200 }, 1), 0.946423f, 2e-5f);
}

// Why it exists: a rear man backing off from a crowded spot to a spaced one is PAID, although his own proximity falls.
// Striker at 200, rear man 600 -> 1700 (gap 400 -> 1500): potential 0.7080 -> 0.9459, so with gamma 1 the step pays 0.238
TEST(BallProx_2v2_backing_off_the_striker_is_paid_while_stacking_is_charged) {
	Vec O(0, 0, 93), o(5000, 5000, 93);
	float back = Run({ F{ O, { Vec(-200, 0, 93), o, Vec(-600, 0, 93), o }, {} }, F{ O, { Vec(-200, 0, 93), o, Vec(-1700, 0, 93), o }, {} } }, 1.f)[1][2];
	float stack = Run({ F{ O, { Vec(-200, 0, 93), o, Vec(-1700, 0, 93), o }, {} }, F{ O, { Vec(-200, 0, 93), o, Vec(-600, 0, 93), o }, {} } }, 1.f)[1][2];
	CHECK_NEAR(back, 0.2379f, 2e-3f);
	CHECK_NEAR(stack, -back, 1e-6f);                                  // gamma 1: the exact opposite move
}

// ZeroSum 2v2 values at team spirit 0.3: every blue car reads the blue team change minus the orange one (checked above by the older sum-to-zero test)
TEST(BallProx_2v2_zero_sum_blue_cars_read_the_team_change_minus_the_opponents) {
	Reward* zs = new ZeroSumReward(new BallProximityPotentialReward(1.f), 0.3f);
	Vec O(0, 0, 93);
	// blue backs the rear man off 600 -> 1700 (striker at 200); orange stands still (200 and 2000 away): its change is 0 at gamma 1
	auto v = Run({ F{ O, { Vec(-200, 0, 93), Vec(3000, 0, 93), Vec(-600, 0, 93), Vec(-3000, 0, 93) }, {} },
	               F{ O, { Vec(-200, 0, 93), Vec(3000, 0, 93), Vec(-1700, 0, 93), Vec(-3000, 0, 93) }, {} } }, 1.f, zs)[1];
	CHECK(v[0] > 0.f); CHECK_NEAR(v[0], v[2], 1e-6f);
	CHECK_NEAR(v[1], -v[0], 1e-6f); CHECK_NEAR(v[3], -v[0], 1e-6f);
	delete zs;
}
