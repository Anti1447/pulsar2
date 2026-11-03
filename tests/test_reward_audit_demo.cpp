#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

#include <type_traits>

using namespace RLGC;
using namespace RewardTest;

// Audit of DemoReward (weight 37.5, wrapped ZeroSum(TEAM_SPIRIT 0.3)): `PlayerDataEventReward<&PlayerEventState::demo, false>`, i.e.
// 1.0 for a player whose eventState.demo is set this step, else 0. The flag is set by EnvSet's bump callback (EnvSet.cpp _BumpCallback)
// when the engine reports a car-hit-car event with isDemo and the two cars are on DIFFERENT teams; it is a bool, reset at the start of every
// step, so any number of demos by one bumper in a step pay once. The demolished victim's own flag (`demoed`) is not used by this reward.
// Hand values: 1v1 wrapped [+1, -1] (x37.5 = +37.5 / -37.5, a pair swing of 75 = half a goal); 2v2 wrapped [0.85, 0.15, -0.5, -0.5].
// Fixture: fabricated states with the event flag set directly (no engine). The engine side (when does a demo happen, who gets the flag, the
// 3 s respawn) is in tools/reward_lab/packs/demo.

namespace {
	GameState Make(int perTeam, int bumper = -1, int victim = -1, int bumper2 = -1) {
		GameState gs; gs.deltaTime = STEP_DT;
		for (int i = 0; i < perTeam; i++) {
			gs.players.push_back(MakePlayer(1 + 2 * i, 2 * i, Team::BLUE, Vec(-1000.f, 300.f * i, 17)));
			gs.players.push_back(MakePlayer(2 + 2 * i, 2 * i + 1, Team::ORANGE, Vec(1000.f, 300.f * i, 17)));
		}
		if (bumper >= 0) gs.players[bumper].eventState.demo = true;
		if (bumper2 >= 0) gs.players[bumper2].eventState.demo = true;
		if (victim >= 0) { gs.players[victim].eventState.demoed = true; gs.players[victim].isDemoed = true; }
		return gs;
	}
}

TEST(Demo_pays_one_to_the_bumper_and_nothing_to_anyone_else) {
	DemoReward r;
	GameState gs = Make(2, /*bumper=*/0, /*victim=*/1);
	auto v = r.GetAllRewards(gs, false);
	CHECK_EQ(v[0], 1.0f);
	CHECK_EQ(v[1], 0.0f);   // the victim's raw reward is 0 (DemoedPenalty is not in the stack)
	CHECK_EQ(v[2], 0.0f); CHECK_EQ(v[3], 0.0f);
	GameState none = Make(2);
	for (float x : r.GetAllRewards(none, false)) CHECK_EQ(x, 0.0f);
}

TEST(Demo_reads_the_demo_flag_not_the_bump_or_demoed_flags) {
	DemoReward r;
	GameState gs = Make(1);
	gs.players[0].eventState.bump = true; gs.players[1].eventState.bumped = true; gs.players[1].eventState.demoed = true; gs.players[1].isDemoed = true;
	for (float x : r.GetAllRewards(gs, false)) CHECK_EQ(x, 0.0f);   // a bump, being bumped or being demolished pays nothing here
}

TEST(Demo_the_flag_is_a_bool_so_several_demos_in_one_step_pay_once) {
	static_assert(std::is_same<decltype(PlayerEventState::demo), bool>::value, "demo is a bool");
	DemoReward r;
	GameState gs = Make(2, 0, 1);       // one bumper, one demo flag: the most a bool can say
	CHECK_EQ(r.GetAllRewards(gs, false)[0], 1.0f);
}

// 1v1 through the production wrapper: the bumper gains, the victim is charged the same: a pair swing of 2 x 37.5 = 75
TEST(Demo_zero_sum_1v1_is_plus_and_minus_one_so_a_pair_swing_of_75_at_weight_37p5) {
	Reward* zs = new ZeroSumReward(new DemoReward(), 0.3f);
	GameState gs = Make(1, 0, 1);
	zs->Reset(gs); zs->PreStep(gs);
	auto v = zs->GetAllRewards(gs, false);
	CHECK_NEAR(v[0], 1.0f, 1e-6f);
	CHECK_NEAR(v[1], -1.0f, 1e-6f);
	CHECK_NEAR(37.5f * v[0] - 37.5f * v[1], 75.0f, 1e-4f);   // the swing between the two players: half a Goal (150)
	delete zs;
}

// 2v2: blue car 0 demolishes orange car 0.  car 0: 1 * 0.7 + 0.3 * 0.5 = 0.85; teammate 0.15; each orange car -0.5
TEST(Demo_zero_sum_2v2_values_at_team_spirit_0p3) {
	Reward* zs = new ZeroSumReward(new DemoReward(), 0.3f);
	GameState gs = Make(2, 0, 1);       // players: blue0, orange0, blue1, orange1
	zs->Reset(gs); zs->PreStep(gs);
	auto v = zs->GetAllRewards(gs, false);
	CHECK_NEAR(v[0], 0.85f, 1e-6f);     // blue bumper
	CHECK_NEAR(v[1], -0.5f, 1e-6f);     // the victim
	CHECK_NEAR(v[2], 0.15f, 1e-6f);     // the bumper's teammate shares
	CHECK_NEAR(v[3], -0.5f, 1e-6f);     // the victim's teammate is charged too
	CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-6f);
	CHECK_NEAR(37.5f * v[0], 31.875f, 1e-4f);
	delete zs;
}

// Both teams demolish each other in the same step: the wrapper cancels it exactly in 1v1
TEST(Demo_a_mutual_demo_in_the_same_step_nets_zero) {
	Reward* zs = new ZeroSumReward(new DemoReward(), 0.3f);
	GameState gs = Make(1, 0, 1, /*bumper2=*/1);
	zs->Reset(gs); zs->PreStep(gs);
	auto v = zs->GetAllRewards(gs, false);
	CHECK_NEAR(v[0], 0.0f, 1e-6f); CHECK_NEAR(v[1], 0.0f, 1e-6f);
	delete zs;
}

// Nothing in the reward limits how often it pays: one demo per step on consecutive steps pays every step (the engine only allows about one demo
// per victim per 3 s respawn, 45 steps, which is the real rate limit)
TEST(Demo_has_no_cooldown_of_its_own_FINDING) {
	DemoReward r;
	float total = 0;
	for (int k = 0; k < 10; k++) { GameState gs = Make(1, 0, 1); total += r.GetAllRewards(gs, false)[0]; }
	CHECK_EQ(total, 10.0f);
}
