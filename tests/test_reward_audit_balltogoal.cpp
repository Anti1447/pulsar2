#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

#include <cmath>

using namespace RLGC;
using namespace RewardTest;

// Audit of BallToGoalPotentialReward (checklist item 2, weight 75 in BuildRewards, NOT ZeroSum-wrapped).
//   Phi_blue(ball) = 0.5 * (exp(-|ball - ORANGE_GOAL_BACK| / 2300) - exp(-|ball - BLUE_GOAL_BACK| / 2300))
//   Phi_orange     = -Phi_blue
//   r = gamma * Phi(ball now) - Phi(ball previous step)              (0 with no previous state)
// ORANGE_GOAL_BACK = (0, +6000, 321.3875), BLUE_GOAL_BACK = (0, -6000, 321.3875), ball z = 93 below.
// Expected numbers were computed independently of the C++ (a separate script of the same formula) before running:
//   Phi(y) at x=0, z=93:  y=-2000 -0.072179 | 0 0.000000 | 2000 0.072179 | 4000 0.201926 | 5000 0.315916 | 5213 0.346322
//   Phi at the goal back (0, 6000, 321.39) is 0.5 * (1 - exp(-12000/2300)) = 0.497289
// Fixture: fabricated states (no engine); engine behaviour is in tools/reward_lab/packs/ball_to_goal.

namespace {
	constexpr float TRAIN_G = 0.9969f; // TRAIN_GAMMA in ExampleMain.cpp

	GameState Frame(float x, float y, float z = 93.f) {
		GameState gs; gs.deltaTime = STEP_DT;
		gs.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(-3000, -3000, 17)));
		gs.players.push_back(MakePlayer(101, 1, Team::ORANGE, Vec(3000, 3000, 17)));
		gs.ball.pos = Vec(x, y, z);
		return gs;
	}

	// reward for [blue, orange] when the ball moves from `a` to `b` in one step
	std::pair<float, float> Step(Vec a, Vec b, float gamma) {
		GameState prev = Frame(a.x, a.y, a.z), cur = Frame(b.x, b.y, b.z);
		cur.prev = &prev;
		BallToGoalPotentialReward r(gamma);
		return { r.GetReward(cur.players[0], cur, false), r.GetReward(cur.players[1], cur, false) };
	}
}

TEST(BallToGoal_first_step_without_a_previous_state_is_zero) {
	GameState cur = Frame(0, 3000);
	BallToGoalPotentialReward r(TRAIN_G);
	CHECK_EQ(r.GetReward(cur.players[0], cur, false), 0.0f);
	CHECK_EQ(r.GetReward(cur.players[1], cur, false), 0.0f);
}

TEST(BallToGoal_a_ball_at_rest_at_midfield_pays_exactly_zero) {
	auto r = Step(Vec(0, 0, 93), Vec(0, 0, 93), TRAIN_G);
	CHECK_EQ(r.first, 0.0f); // Phi = 0 at the centre: the two goal distances are equal
	CHECK_EQ(r.second, 0.0f);
}

TEST(BallToGoal_carry_to_y2000_undiscounted_is_the_potential_difference) {
	auto r = Step(Vec(0, 0, 93), Vec(0, 2000, 93), 1.0f);
	CHECK_NEAR(r.first, 0.072179f, 1e-5f);   // Phi(2000) - Phi(0)
	CHECK_NEAR(r.second, -0.072179f, 1e-5f);
}

TEST(BallToGoal_discount_shaves_the_reward_slightly) {
	auto r = Step(Vec(0, 0, 93), Vec(0, 2000, 93), TRAIN_G);
	CHECK_NEAR(r.first, 0.071956f, 1e-5f);   // 0.9969 * 0.072179 - 0
}

TEST(BallToGoal_moving_toward_your_own_goal_is_negative) {
	auto r = Step(Vec(0, 0, 93), Vec(0, -2000, 93), 1.0f);
	CHECK_NEAR(r.first, -0.072179f, 1e-5f);  // blue loses; orange gains the same
	CHECK_NEAR(r.second, 0.072179f, 1e-5f);
}

TEST(BallToGoal_is_antisymmetric_between_the_teams_so_it_is_already_zero_sum) {
	uint32_t s = 12345;
	auto rnd = [&](float lo, float hi) { s = s * 1664525u + 1013904223u; return lo + (hi - lo) * ((s >> 8) / 16777216.f); };
	for (int i = 0; i < 300; i++) {
		Vec a(rnd(-4000, 4000), rnd(-5000, 5000), rnd(93, 1900)), b(rnd(-4000, 4000), rnd(-5000, 5000), rnd(93, 1900));
		auto r = Step(a, b, TRAIN_G);
		CHECK_NEAR(r.first + r.second, 0.0f, 1e-6f);
	}
}

// What a ZeroSum wrap would do (the code comment says never to): own - opponent = 2x, so the 75 would act as 150.
TEST(BallToGoal_wrapping_it_in_zero_sum_would_silently_double_it) {
	GameState prev = Frame(0, 0), cur = Frame(0, 2000);
	cur.prev = &prev;
	Reward* zs = new ZeroSumReward(new BallToGoalPotentialReward(1.0f), 0.3f);
	zs->Reset(prev);
	zs->PreStep(cur);
	auto v = zs->GetAllRewards(cur, false);
	CHECK_NEAR(v[0], 2 * 0.072179f, 1e-5f);
	delete zs;
}

TEST(BallToGoal_a_ball_resting_near_the_opponent_goal_leaks_a_small_negative_per_step) {
	// r = (gamma - 1) * Phi = -0.0031 * 0.201926 = -0.000626 at y 4000; x75 = -0.047 per step (-42 a minute at 15 Hz)
	auto r = Step(Vec(0, 4000, 93), Vec(0, 4000, 93), TRAIN_G);
	CHECK_NEAR(r.first, -0.000626f, 2e-6f);
	CHECK_NEAR(r.second, 0.000626f, 2e-6f);
}

// Farming check: carry the ball up and back. gamma 1 refunds in full; the real gamma loses only the tiny leak.
TEST(BallToGoal_going_up_and_back_refunds_in_full) {
	auto up = Step(Vec(0, 0, 93), Vec(0, 4000, 93), 1.0f), back = Step(Vec(0, 4000, 93), Vec(0, 0, 93), 1.0f);
	CHECK_NEAR(up.first + back.first, 0.0f, 1e-6f);
	auto up2 = Step(Vec(0, 0, 93), Vec(0, 4000, 93), TRAIN_G), back2 = Step(Vec(0, 4000, 93), Vec(0, 0, 93), TRAIN_G);
	// 0.9969*0.201926 + (0.9969*0 - 0.201926) = -0.000626: a loop pays slightly negative, never positive
	CHECK_NEAR(up2.first + back2.first, -0.000626f, 2e-6f);
	CHECK(up2.first + back2.first < 0.0f);
}

TEST(BallToGoal_discounted_sum_telescopes_to_gamma_pow_N_phi_end_minus_phi_start) {
	// 20 ball positions along a bouncing path; sum of gamma^t * r_t must equal gamma^N * Phi(end) - Phi(start)
	std::vector<Vec> path;
	for (int i = 0; i <= 20; i++) path.push_back(Vec(300.f * sinf(0.4f * i), -2500.f + 330.f * i + 400.f * sinf(0.9f * i), 93.f + 200.f * fabsf(sinf(0.7f * i))));
	double sum = 0, disc = 1;
	for (int i = 0; i < 20; i++) { sum += disc * Step(path[i], path[i + 1], TRAIN_G).first; disc *= TRAIN_G; }
	double expect = disc * BallToGoalPotentialReward::Phi(path[20], Team::BLUE) - BallToGoalPotentialReward::Phi(path[0], Team::BLUE);
	CHECK_NEAR((float)sum, (float)expect, 1e-5f);
}

TEST(BallToGoal_credit_concentrates_at_the_goal_mouth) {
	// the same 1000 uu step is worth about 3.5x more at y 4000-5000 than at midfield 0-1000
	float mid = Step(Vec(0, 0, 93), Vec(0, 1000, 93), 1.0f).first;
	float mouth = Step(Vec(0, 4000, 93), Vec(0, 5000, 93), 1.0f).first;
	CHECK_NEAR(mouth, 0.113990f, 1e-5f);
	CHECK(mouth > 3.0f * mid);
}

TEST(BallToGoal_potential_is_bounded_by_0p4973_and_one_end_to_end_carry_is_worth_under_75) {
	CHECK_NEAR(BallToGoalPotentialReward::Phi(Vec(0, 6000, 321.3875f), Team::BLUE), 0.497289f, 1e-5f);
	CHECK_NEAR(BallToGoalPotentialReward::Phi(Vec(0, -6000, 321.3875f), Team::BLUE), -0.497289f, 1e-5f);
	float worst = 0;
	for (int y = -6000; y <= 6000; y += 500)
		for (int x = -4000; x <= 4000; x += 1000)
			worst = std::max(worst, fabsf(BallToGoalPotentialReward::Phi(Vec((float)x, (float)y, 93), Team::BLUE)));
	CHECK(worst < 0.4973f);
	// own goal back to opponent goal back = 0.994578 -> x75 = 74.6, half of one Goal (150)
	CHECK_NEAR(75.f * 2 * 0.497289f, 74.5934f, 1e-3f);
}

TEST(BallToGoal_cars_do_not_matter_only_the_ball_and_the_team) {
	GameState prev = Frame(0, 1000), a = Frame(0, 2500), b = Frame(0, 2500);
	a.prev = &prev; b.prev = &prev;
	b.players[0].pos = Vec(0, 2400, 17); b.players[0].vel = Vec(0, 2000, 0); b.players[0].boost = 3;
	BallToGoalPotentialReward r(TRAIN_G);
	CHECK_EQ(r.GetReward(a.players[0], a, false), r.GetReward(b.players[0], b, false));
}
