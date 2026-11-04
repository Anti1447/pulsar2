#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"

namespace RLGC {

	// PickupBoostReward + the demo guard the original is missing. Demo-respawn
	// boost (a fresh spawn tank) is not a pickup the player earned: without this
	// guard, a ZeroSumReward wrapper CHARGES the demoer for the victim's respawn
	// tank, silently refunding part of the demo pair swing. Guard mirrors
	// BallProximityPotentialReward's demo/respawn-teleport convention.
	class GuardedPickupBoostReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev)
				return 0;

			// Respawn refill is a teleport side effect, not a pickup
			if (player.isDemoed || player.prev->isDemoed)
				return 0;

			if (player.boost > player.prev->boost) {
				return sqrtf(player.boost / 100.f) - sqrtf(player.prev->boost / 100.f);
			} else {
				return 0;
			}
		}
	};
}
