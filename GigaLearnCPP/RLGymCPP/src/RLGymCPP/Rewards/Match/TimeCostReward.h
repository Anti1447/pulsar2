#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"

namespace RLGC {

	// TIME COST (2026-07-16, user-directed): a small constant per-step penalty -
	// urgency pressure that taxes stalling (the measured 73%-airborne hover next to
	// uncontested balls) and slow play generally. NOT zero-sum in the strict sum
	// (both players pay), but a uniform constant CANCELS in every competitive margin
	// (PSD/league fitness = return DIFFERENCES), so fitness purity holds. The weight
	// must stay small vs GoalReward: at weight w a 30s episode costs 450w
	// (15Hz steps at tickSkip 8; re-derive if tickSkip changes) undiscounted -
	// keep 450w << 150 or ending episodes (including CONCEDING) becomes attractive.
	class TimeCostReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			return -1.f;
		}
	};
}
