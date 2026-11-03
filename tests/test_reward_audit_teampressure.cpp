#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

using namespace RLGC;
using namespace RewardTest;

// Audit of TeamPressureReward (weight 0.15, wrapped ZeroSum(TEAM_SPIRIT 0.3)), re-audited after the 2026-11-03 smoothing. Raw value per player, from the
// CURRENT state only (no prev, no PreStep):
//   0 for a demolished player.  Otherwise, over the player's alive team cars: d_eff = d - tau * min(v_toward, speedCap)  (tau 2 s, speedCap 1000 uu/s,
//   d = 3D distance to the ball, v_toward = max(0, velocity . unit vector to the ball));  lapse = smoothstep((d_eff - 2000) / 1000);  reward = -min over cars of lapse.
// Hand values of smoothstep(t) = 3t^2 - 2t^3:  d_eff 2000 -> 0;  2250 -> 0.15625;  2500 -> 0.5;  2750 -> 0.84375;  3000 and above -> 1.
// Teammates read the same value (the minimum over the team); opponents are ignored; a team with every car demolished reads 0.
// Fixture: fabricated states (no engine). The engine side is in tools/reward_lab/packs/team_pressure.

namespace {
	// State with the ball at the origin z 93. Players: blue0, orange0 (, blue1, orange1 (, blue2, orange2)). dist = distance to the ball along x (z kept at 93).
	struct C { float dist; float closing = 0.f; bool demoed = false; };

	GameState Make(const std::vector<C>& cars) {
		GameState gs; gs.deltaTime = STEP_DT; gs.ball.pos = Vec(0, 0, 93);
		for (size_t k = 0; k < cars.size(); k++) {
			Player p = MakePlayer(1 + (uint32_t)k, (int)k, (k % 2 == 0) ? Team::BLUE : Team::ORANGE, Vec(-cars[k].dist, 0, 93));
			p.vel = Vec(cars[k].closing, 0, 0);               // +x is toward the ball from the car's side
			p.isDemoed = cars[k].demoed;
			gs.players.push_back(p);
		}
		return gs;
	}

	std::vector<float> Raw(const std::vector<C>& cars, TeamPressureReward* use = nullptr) {
		TeamPressureReward local; TeamPressureReward& r = use ? *use : local;
		GameState gs = Make(cars);
		return r.GetAllRewards(gs, false);
	}
	float One(float dist, float closing = 0.f) { return Raw({ C{ dist, closing }, C{ 0 } })[0]; }   // blue0 alone is being tested; orange sits on the ball
}

TEST(TeamPressure_a_parked_car_pays_the_smoothstep_of_its_distance_between_2000_and_3000) {
	CHECK_EQ(One(2000.f), 0.0f);
	CHECK_NEAR(One(2250.f), -0.15625f, 1e-5f);
	CHECK_NEAR(One(2500.f), -0.5f, 1e-5f);
	CHECK_NEAR(One(2750.f), -0.84375f, 1e-5f);
	CHECK_EQ(One(3000.f), -1.0f);
	CHECK_EQ(One(9000.f), -1.0f);
	CHECK_EQ(One(100.f), 0.0f);
	CHECK_EQ(One(0.f), 0.0f);
}

TEST(TeamPressure_there_is_no_cliff_at_the_old_boundaries) {
	CHECK_NEAR(One(2499.9f) - One(2500.1f), 0.0f, 1e-3f);             // was 0 / -1
	CHECK_NEAR(One(2499.9f), -0.5f, 1e-3f);
	CHECK_NEAR(One(3999.9f, 600.f) - One(4000.1f, 600.f), 0.0f, 1e-3f);   // was 0 / -1
}

TEST(TeamPressure_closing_on_the_ball_moves_the_projected_distance_in_by_tau_times_the_speed) {
	CHECK_EQ(One(3000.f, 500.f), 0.0f);                               // d_eff = 3000 - 2 x 500 = 2000
	CHECK_NEAR(One(3000.f, 400.f), -0.104f, 1e-5f);                   // d_eff 2200: t 0.2 -> 3 x 0.04 - 2 x 0.008
	CHECK_NEAR(One(3000.f, 250.f), -0.5f, 1e-5f);                     // d_eff 2500
	CHECK_EQ(One(3000.f, 0.f), -1.0f);
}

TEST(TeamPressure_the_closing_credit_is_capped_at_1000_uu_per_s) {
	CHECK_EQ(One(4000.f, 1000.f), 0.0f);                              // d_eff = 4000 - 2000
	CHECK_EQ(One(4000.f, 5000.f), 0.0f);                              // the cap: no more credit for being faster
	CHECK_NEAR(One(4500.f, 5000.f), -0.5f, 1e-5f);                    // d_eff 2500, the same as at 1000 uu/s
	CHECK_NEAR(One(4500.f, 1000.f), -0.5f, 1e-5f);
}

TEST(TeamPressure_moving_away_or_sideways_neither_helps_nor_hurts) {
	CHECK_NEAR(One(2500.f, -2000.f), -0.5f, 1e-5f);                   // away: no credit, no extra charge
	GameState gs = Make({ C{ 3000.f }, C{ 0.f } });
	TeamPressureReward r;
	gs.players[0].vel = Vec(0, 2300, 0);                              // sideways: zero closing speed
	CHECK_EQ(r.GetReward(gs.players[0], gs, false), -1.0f);
	gs.players[0].vel = Vec(0, 2300, 0) + Vec(250, 0, 0);             // add a 250 uu/s component toward the ball: d_eff 2500
	CHECK_NEAR(r.GetReward(gs.players[0], gs, false), -0.5f, 1e-5f);
}

TEST(TeamPressure_the_distance_is_3D) {
	GameState gs = Make({ C{ 2400.f }, C{ 0.f } });
	TeamPressureReward r;
	CHECK_NEAR(r.GetReward(gs.players[0], gs, false), -0.352f, 1e-5f);   // t 0.4 -> 3 x 0.16 - 2 x 0.064
	gs.ball.pos.z = 93 + 1000;                                        // ball 1000 up: d = sqrt(2400^2 + 1000^2) = 2600
	CHECK_NEAR(r.GetReward(gs.players[0], gs, false), -0.648f, 1e-5f);   // t 0.6 -> 3 x 0.36 - 2 x 0.216
}

TEST(TeamPressure_sitting_far_away_costs_one_raw_every_step_with_no_telescoping) {
	TeamPressureReward r; float sum = 0;
	for (int i = 0; i < 100; i++) { GameState gs = Make({ C{ 3500.f }, C{ 0.f } }); sum += r.GetReward(gs.players[0], gs, false); }
	CHECK_NEAR(sum, -100.f, 1e-4f);                                   // x0.15 = -15 weighted per 100 steps (6.7 s)
}

TEST(TeamPressure_needs_no_previous_state) {
	TeamPressureReward r;
	GameState gs = Make({ C{ 3500.f }, C{ 0.f } });
	CHECK(gs.prev == nullptr && gs.players[0].prev == nullptr);
	CHECK_EQ(r.GetReward(gs.players[0], gs, false), -1.0f);
}

TEST(TeamPressure_opponents_are_ignored) {
	auto v = Raw({ C{ 3500.f }, C{ 100.f } });                        // the opponent is on the ball
	CHECK_EQ(v[0], -1.0f);
	CHECK_EQ(v[1], 0.0f);
}

// 2v2 (order blue0, orange0, blue1, orange1): the team's lapse is the MINIMUM over its alive cars, shared by both teammates
TEST(TeamPressure_the_team_reads_its_best_covered_car) {
	auto v = Raw({ C{ 5000.f }, C{ 5000.f }, C{ 2500.f }, C{ 5000.f } });
	CHECK_NEAR(v[0], -0.5f, 1e-5f); CHECK_NEAR(v[2], -0.5f, 1e-5f);   // blue1 half covered
	CHECK_NEAR(v[1], -1.0f, 1e-5f); CHECK_NEAR(v[3], -1.0f, 1e-5f);   // orange both far
	auto w = Raw({ C{ 5000.f }, C{ 5000.f }, C{ 2000.f }, C{ 5000.f } });
	CHECK_EQ(w[0], 0.0f); CHECK_EQ(w[2], 0.0f);
	auto c = Raw({ C{ 5000.f }, C{ 5000.f }, C{ 3000.f, 500.f }, C{ 5000.f } });   // blue1 closing at 500: d_eff 2000
	CHECK_EQ(c[0], 0.0f); CHECK_EQ(c[2], 0.0f);
}

// The same covering car gives the same team value in 1v1, 2v2 and 3v3
TEST(TeamPressure_the_value_does_not_depend_on_the_game_mode) {
	float v1 = Raw({ C{ 2500.f }, C{ 5000.f } })[0];
	auto v2 = Raw({ C{ 2500.f }, C{ 5000.f }, C{ 5000.f }, C{ 5000.f } });
	auto v3 = Raw({ C{ 5000.f }, C{ 5000.f }, C{ 5000.f }, C{ 5000.f }, C{ 2500.f }, C{ 5000.f } });
	CHECK_NEAR(v1, -0.5f, 1e-5f);
	CHECK_NEAR(v2[0], -0.5f, 1e-5f); CHECK_NEAR(v2[2], -0.5f, 1e-5f);
	CHECK_NEAR(v3[0], -0.5f, 1e-5f); CHECK_NEAR(v3[2], -0.5f, 1e-5f); CHECK_NEAR(v3[4], -0.5f, 1e-5f);
	CHECK_NEAR(v3[1], -1.0f, 1e-5f); CHECK_NEAR(v3[3], -1.0f, 1e-5f); CHECK_NEAR(v3[5], -1.0f, 1e-5f);   // the uncovered orange trio
}

// FIXED 2026-11-03: a demolished player's own row is 0, and a demolished teammate does not count as coverage
TEST(TeamPressure_a_demolished_car_reads_zero_and_does_not_cover_its_team) {
	auto v = Raw({ C{ 5000.f }, C{ 5000.f }, C{ 100.f, 0.f, true }, C{ 5000.f } });   // blue1 would be on the ball but is demolished
	CHECK_EQ(v[0], -1.0f);                                            // blue0 is not covered by a dead teammate
	CHECK_EQ(v[2], 0.0f);                                             // and the dead car itself is not charged
	auto d = Raw({ C{ 100.f, 0.f, true }, C{ 100.f } });              // 1v1: the only blue car is demolished
	CHECK_EQ(d[0], 0.0f);                                             // was -1 for each of the ~45 respawn steps
	CHECK_EQ(d[1], 0.0f);
	auto all = Raw({ C{ 5000.f, 0.f, true }, C{ 5000.f }, C{ 5000.f, 0.f, true }, C{ 5000.f } });   // the whole blue team is down
	CHECK_EQ(all[0], 0.0f); CHECK_EQ(all[2], 0.0f);
}

TEST(TeamPressure_custom_parameters_are_honoured) {
	TeamPressureReward r(1.f, 500.f, 1000.f, 2000.f);                 // tau 1 s, cap 500, window 1000..2000
	CHECK_EQ(Raw({ C{ 1000.f }, C{ 0.f } }, &r)[0], 0.0f);
	CHECK_NEAR(Raw({ C{ 1500.f }, C{ 0.f } }, &r)[0], -0.5f, 1e-5f);
	CHECK_EQ(Raw({ C{ 2000.f }, C{ 0.f } }, &r)[0], -1.0f);
	CHECK_NEAR(Raw({ C{ 2000.f, 1000.f }, C{ 0.f } }, &r)[0], -0.5f, 1e-5f);   // closing capped at 500: d_eff = 2000 - 500 = 1500
}

// Wiring as in training: ZeroSum(TeamPressure, 0.3), weight 0.15.
TEST(TeamPressure_zero_sum_1v1_mutual_passivity_cancels_and_a_lone_lapse_is_charged_in_full) {
	Reward* zs = new ZeroSumReward(new TeamPressureReward(), 0.3f);
	GameState both = Make({ C{ 5000.f }, C{ 5000.f } });
	auto a = zs->GetAllRewards(both, false);
	CHECK_NEAR(a[0], 0.0f, 1e-6f); CHECK_NEAR(a[1], 0.0f, 1e-6f);     // both far: -1 - (-1) = 0
	GameState lone = Make({ C{ 5000.f }, C{ 1000.f } });
	auto b = zs->GetAllRewards(lone, false);
	CHECK_NEAR(b[0], -1.0f, 1e-6f); CHECK_NEAR(b[1], 1.0f, 1e-6f);    // me far, they near: -1 for me, +1 for them
	CHECK_NEAR(0.15f * b[0], -0.15f, 1e-6f);                          // -2.25 per second of lapse (15 steps)
	GameState half = Make({ C{ 2500.f }, C{ 1000.f } });
	auto h = zs->GetAllRewards(half, false);
	CHECK_NEAR(h[0], -0.5f, 1e-6f); CHECK_NEAR(h[1], 0.5f, 1e-6f);    // the smooth middle
	delete zs;
}

// The demolished car pays nothing itself; its opponent's own lapse still reaches it through ZeroSum, symmetric with the opponent's gain/loss.
TEST(TeamPressure_zero_sum_a_demolished_car_is_not_charged_for_its_own_lapse) {
	Reward* zs = new ZeroSumReward(new TeamPressureReward(), 0.3f);
	GameState near = Make({ C{ 5000.f, 0.f, true }, C{ 1000.f } });  // blue down, orange on the ball
	auto a = zs->GetAllRewards(near, false);
	CHECK_NEAR(a[0], 0.0f, 1e-6f); CHECK_NEAR(a[1], 0.0f, 1e-6f);     // was -1 / +1
	delete zs;
}

TEST(TeamPressure_zero_sum_2v2_values) {
	Reward* zs = new ZeroSumReward(new TeamPressureReward(), 0.3f);
	GameState gs = Make({ C{ 5000.f }, C{ 1000.f }, C{ 6000.f }, C{ 5000.f } });   // blue team far, orange has a car near
	auto v = zs->GetAllRewards(gs, false);
	CHECK_NEAR(v[0], -1.0f, 1e-6f); CHECK_NEAR(v[2], -1.0f, 1e-6f);   // team value is shared: own = team average = -1; opponents' average 0 -> -1
	CHECK_NEAR(v[1], 1.0f, 1e-6f);  CHECK_NEAR(v[3], 1.0f, 1e-6f);
	CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-6f);
	delete zs;
}
