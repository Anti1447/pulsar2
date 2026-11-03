#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

using namespace RLGC;
using namespace RewardTest;

// Audit of TeamPressureReward (weight 0.15, wrapped ZeroSum(TEAM_SPIRIT 0.3)). Raw value per player, from the CURRENT state only (no prev, no PreStep):
//   0 if some ALIVE car of the player's team (the player included) is "pressuring" the ball, else -1.
//   pressuring = 3D distance to the ball d < 2500 (nearDist), OR  d < 4000 (closeDist) and  car.vel . (unit vector to the ball) > 500 (closeSpeed).
// All the strict comparisons: d = 2500 is NOT near, d = 4000 is NOT close, a closing speed of exactly 500 does NOT count.
// Teammates read the same value (it is a team property). Opponents are ignored. Demolished cars are skipped, so a team with every car demolished reads -1.
// Fixture: fabricated states (no engine). The engine side is in tools/reward_lab/packs/team_pressure.

namespace {
	// 1v1 or 2v2 state, ball at the origin z 93. Players: blue0, orange0 (, blue1, orange1). dist = 3D distance to the ball along x (z kept at 93).
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

TEST(TeamPressure_near_means_strictly_under_2500) {
	CHECK_EQ(One(2499.9f), 0.0f);
	CHECK_EQ(One(2500.f), -1.0f);
	CHECK_EQ(One(100.f), 0.0f);
	CHECK_EQ(One(0.f), 0.0f);
}

TEST(TeamPressure_closing_counts_between_2500_and_4000_above_500_uu_per_s) {
	CHECK_EQ(One(3000.f, 501.f), 0.0f);
	CHECK_EQ(One(3000.f, 500.f), -1.0f);                              // exactly 500 does not count
	CHECK_EQ(One(3000.f, 499.f), -1.0f);
	CHECK_EQ(One(3999.f, 2300.f), 0.0f);
	CHECK_EQ(One(4000.f, 2300.f), -1.0f);                             // 4000 is outside the closing range
	CHECK_EQ(One(3000.f, -2000.f), -1.0f);                            // moving away
	CHECK_EQ(One(3000.f, 0.f), -1.0f);                                // standing still
}

TEST(TeamPressure_only_the_speed_toward_the_ball_counts) {
	// a car 3000 uu away along x moving at 2300 uu/s sideways (y) has 0 closing speed
	GameState gs = Make({ C{ 3000.f }, C{ 0.f } });
	gs.players[0].vel = Vec(0, 2300, 0);
	TeamPressureReward r;
	CHECK_EQ(r.GetReward(gs.players[0], gs, false), -1.0f);
	gs.players[0].vel = Vec(0, 2300, 0) + Vec(600, 0, 0);            // add a 600 uu/s component toward the ball
	CHECK_EQ(r.GetReward(gs.players[0], gs, false), 0.0f);
}

TEST(TeamPressure_the_distance_is_3D) {
	GameState gs = Make({ C{ 2400.f }, C{ 0.f } });
	TeamPressureReward r;
	CHECK_EQ(r.GetReward(gs.players[0], gs, false), 0.0f);
	gs.ball.pos.z = 93 + 1000;                                        // ball 1000 up: d = sqrt(2400^2 + 1000^2) = 2600
	CHECK_EQ(r.GetReward(gs.players[0], gs, false), -1.0f);
}

TEST(TeamPressure_sitting_far_away_costs_one_raw_every_step_with_no_telescoping) {
	TeamPressureReward r; float sum = 0;
	for (int i = 0; i < 100; i++) { GameState gs = Make({ C{ 3000.f }, C{ 0.f } }); sum += r.GetReward(gs.players[0], gs, false); }
	CHECK_NEAR(sum, -100.f, 1e-4f);                                   // x0.15 = -15 weighted per 100 steps (6.7 s)
}

TEST(TeamPressure_needs_no_previous_state) {
	TeamPressureReward r;
	GameState gs = Make({ C{ 3000.f }, C{ 0.f } });
	CHECK(gs.prev == nullptr && gs.players[0].prev == nullptr);
	CHECK_EQ(r.GetReward(gs.players[0], gs, false), -1.0f);
}

TEST(TeamPressure_opponents_are_ignored) {
	auto v = Raw({ C{ 3000.f }, C{ 100.f } });                        // the opponent is on the ball
	CHECK_EQ(v[0], -1.0f);
	CHECK_EQ(v[1], 0.0f);
}

// 2v2 (order blue0, orange0, blue1, orange1): one teammate near is enough for both; both far = both -1
TEST(TeamPressure_one_teammate_on_the_ball_covers_the_whole_team) {
	auto v = Raw({ C{ 5000.f }, C{ 5000.f }, C{ 1000.f }, C{ 5000.f } });
	CHECK_EQ(v[0], 0.0f); CHECK_EQ(v[2], 0.0f);                       // blue: blue1 is near
	CHECK_EQ(v[1], -1.0f); CHECK_EQ(v[3], -1.0f);                     // orange: both far
	auto w = Raw({ C{ 5000.f }, C{ 5000.f }, C{ 3000.f }, C{ 5000.f } });
	CHECK_EQ(w[0], -1.0f); CHECK_EQ(w[2], -1.0f);
	auto c = Raw({ C{ 5000.f }, C{ 5000.f }, C{ 3000.f, 1000.f }, C{ 5000.f } });   // blue1 closing
	CHECK_EQ(c[0], 0.0f); CHECK_EQ(c[2], 0.0f);
}

TEST(TeamPressure_a_demolished_teammate_does_not_count_and_a_team_of_the_dead_reads_minus_one) {
	auto v = Raw({ C{ 5000.f }, C{ 5000.f }, C{ 100.f, 0.f, true }, C{ 5000.f } });   // blue1 would be on the ball but is demolished
	CHECK_EQ(v[0], -1.0f); CHECK_EQ(v[2], -1.0f);                     // including the dead car's own row
	auto d = Raw({ C{ 100.f, 0.f, true }, C{ 100.f } });              // 1v1: the only blue car is demolished (even though its body is on the ball)
	CHECK_EQ(d[0], -1.0f);                                            // FINDING: a demolished lone car is charged -1 for each of the ~45 respawn steps
	CHECK_EQ(d[1], 0.0f);
}

TEST(TeamPressure_custom_thresholds_are_honoured) {
	TeamPressureReward r(1000.f, 2000.f, 100.f);
	CHECK_EQ(Raw({ C{ 999.f }, C{ 0.f } }, &r)[0], 0.0f);
	CHECK_EQ(Raw({ C{ 1000.f }, C{ 0.f } }, &r)[0], -1.0f);
	CHECK_EQ(Raw({ C{ 1500.f, 101.f }, C{ 0.f } }, &r)[0], 0.0f);
	CHECK_EQ(Raw({ C{ 2000.f, 101.f }, C{ 0.f } }, &r)[0], -1.0f);
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
