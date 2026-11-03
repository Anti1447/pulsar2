#pragma once
// ============================================================================
// RewardRegistry.h - name -> constructor, so a scenario file can say which rewards to run.
// 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md (P1)
//
// Only rewards that have been checked to run correctly under the Lab belong here; adding one is a one-line Register().
// A scenario naming anything else is an error, not a silent skip.
// ============================================================================
#include "Scenario.h"
#include "../Rewards/Reward.h"
#include "../Rewards/CommonRewards.h"
#include "../Rewards/OffsidePenalty.h"
#include "../Rewards/PassingSetupRewards.h"
#include "../Rewards/ZeroSumReward.h"
#include "../Rewards/KickoffOutcomeReward.h"
#include "../Rewards/MultiAirTouchReward.h"
#include <functional>
#include <map>

namespace RLGC {
namespace Lab {

	typedef std::function<Reward*(const RewardSpec&)> RewardFactory;

	inline std::map<std::string, RewardFactory>& RewardRegistry() {
		static std::map<std::string, RewardFactory> reg = {
			{ "OffsidePenalty",            [](const RewardSpec&) -> Reward* { return new OffsidePenalty(); } },
			{ "CarEnergyPotentialReward",  [](const RewardSpec& s) -> Reward* { return new CarEnergyPotentialReward(s.gamma); } },
			{ "PassingSetupReward",        [](const RewardSpec&) -> Reward* { return new PassingSetupReward(); } },
			{ "GoalReward",                [](const RewardSpec&) -> Reward* { return new GoalReward(); } },
			{ "AerialTouchReward",         [](const RewardSpec&) -> Reward* { return new AerialTouchReward(); } },
			{ "BallToGoalPotentialReward", [](const RewardSpec& s) -> Reward* { return new BallToGoalPotentialReward(s.gamma); } },
			{ "AirInterceptPotentialReward", [](const RewardSpec& s) -> Reward* { return new AirInterceptPotentialReward(s.gamma); } },
			{ "WallJumpToBallReward",      [](const RewardSpec& s) -> Reward* { return new WallJumpToBallReward(s.gamma); } },
			{ "KickoffRaceReward",         [](const RewardSpec&) -> Reward* { return new KickoffRaceReward(); } },
			{ "KickoffOutcomeReward",      [](const RewardSpec&) -> Reward* { return new KickoffOutcomeReward(); } },
			{ "ConsecutiveAirTouchReward", [](const RewardSpec& s) -> Reward* { return new ConsecutiveAirTouchReward(s.gamma); } },
			{ "MultiAirTouchReward",       [](const RewardSpec&) -> Reward* { return new MultiAirTouchReward(); } },
			{ "DemoReward",                [](const RewardSpec&) -> Reward* { return new DemoReward(); } },
			{ "TouchAccelReward",         [](const RewardSpec&) -> Reward* { return new TouchAccelReward(); } },
			{ "GuardedPickupBoostReward", [](const RewardSpec&) -> Reward* { return new GuardedPickupBoostReward(); } },
			{ "TimeCostReward", [](const RewardSpec&) -> Reward* { return new TimeCostReward(); } },
			{ "TeamPressureReward", [](const RewardSpec&) -> Reward* { return new TeamPressureReward(); } },
			{ "BallProximityPotentialReward", [](const RewardSpec& s) -> Reward* { return new BallProximityPotentialReward(s.gamma); } },
			{ "OpposedSaveReward",         [](const RewardSpec&) -> Reward* { return new OpposedSaveReward(); } },
		};
		return reg;
	}

	// Build the stack a scenario asks for. The caller owns the rewards (delete w.reward for each; a ZeroSum wrapper owns its child).
	inline bool BuildRewardStack(const std::vector<RewardSpec>& specs, std::vector<WeightedReward>& out, std::vector<std::string>& errors) {
		bool ok = true;
		for (size_t i = 0; i < specs.size(); i++) {
			auto it = RewardRegistry().find(specs[i].name);
			if (it == RewardRegistry().end()) {
				std::string known;
				for (auto& kv : RewardRegistry()) known += (known.empty() ? "" : ", ") + kv.first;
				errors.push_back("rewards[" + std::to_string(i) + "].name: unknown reward \"" + specs[i].name + "\" (registered: " + known + ")");
				ok = false; continue;
			}
			Reward* r = it->second(specs[i]);
			if (specs[i].zeroSum >= 0) r = new ZeroSumReward(r, specs[i].zeroSum);
			out.push_back(WeightedReward(r, specs[i].weight));
		}
		return ok;
	}

	inline void FreeRewardStack(std::vector<WeightedReward>& stack) {
		for (auto& w : stack) delete w.reward;
		stack.clear();
	}

} // namespace Lab
} // namespace RLGC
