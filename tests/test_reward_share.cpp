#include "TestFramework.h"

#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>

#include <filesystem>

using namespace RLGC;

// EnvSet's per-step reward magnitudes (behind the Reward Share % panels): |weight x final reward| summed over the arena's players.

namespace {
	struct Flat : public Reward { // 1 for every blue car, -0.5 for every orange car
		float GetReward(const Player& player, const GameState&, bool) override { return player.team == Team::BLUE ? 1.f : -0.5f; }
	};
}

TEST(RewardShare_magnitudes_are_weighted_absolute_sums_after_zero_sum) {
	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
	EnvSetConfig cfg = {};
	cfg.numArenas = 2;
	cfg.tickSkip = 4;
	cfg.saveRewards = true;
	cfg.envCreateFn = [](int index) {
		EnvCreateResult r = {};
		r.arena = Arena::Create(GameMode::SOCCAR);
		r.arena->AddCar(Team::BLUE);
		r.arena->AddCar(Team::ORANGE);
		if (index == 1) r.arena->AddCar(Team::BLUE); // 2 against 1
		r.actionParser = new DefaultAction();
		r.obsBuilder = new AdvancedObsPadded(3);
		r.stateSetter = new KickoffState();
		r.rewards = { { new Flat(), 2.f }, { new ZeroSumReward(new Flat(), 0.f), 10.f } };
		return r;
	};
	EnvSet envSet(cfg);
	envSet.StepFirstHalf(false);
	envSet.StepSecondHalf(IList(envSet.state.numPlayers, 0), false);
	envSet.Sync();
	// 1v1: bare 2 * (1 + 0.5) = 3; zero-sum: blue 1 - (-0.5) = 1.5, orange -0.5 - 1 = -1.5 -> 10 * 3 = 30
	CHECK_NEAR(envSet.state.rewardMagnitudes[0][0], 3.f, 1e-4f);
	CHECK_NEAR(envSet.state.rewardMagnitudes[0][1], 30.f, 1e-3f);
	// 2v1: bare 2 * (1 + 1 + 0.5) = 5; zero-sum: 10 * 1.5 * 3 = 45
	CHECK_NEAR(envSet.state.rewardMagnitudes[1][0], 5.f, 1e-4f);
	CHECK_NEAR(envSet.state.rewardMagnitudes[1][1], 45.f, 1e-3f);
}
