#pragma once
#include "Reward.h"
#include "../CommonValues.h"
#include <unordered_map>

// Ported from RLGymBot_v3 (CommonRewards.h). Logic unchanged unless noted.

namespace RLGC {

	// ============================================================================
	// RolvWavedashReward — Rev 1.0 (2026-06-27, NJH)
	// Ported from rolv's WavedashReward (rlgym-tools)
	// Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/wavedash_reward.py
	//
	// scaleByAcceleration=true (default):
	//   Records velocity delta magnitude when flip starts.
	//   At landing (wavedash), reward = stored_accel / CAR_MAX_SPEED → [0, ~1]
	//   Rewards wavedashes that generated real speed gain.
	// scaleByAcceleration=false:
	//   Returns 1.0 on any wavedash landing (identical to WavedashBasicReward).
	// ============================================================================
	class RolvWavedashReward : public Reward {
		bool scaleByAcceleration;
		mutable std::unordered_map<uint32_t, float> prevAcceleration;

	public:
		RolvWavedashReward(bool scaleByAcceleration = true)
			: scaleByAcceleration(scaleByAcceleration) {}

		virtual void Reset(const GameState& initialState) override {
			prevAcceleration.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev) return 0.0f;
			const Player& prev = *player.prev;

			bool landedThisFrame = player.isOnGround && !prev.isOnGround;
			bool wavedash = landedThisFrame && (player.isFlipping || prev.isFlipping);

			if (!scaleByAcceleration)
				return wavedash ? 1.0f : 0.0f;

			float& storedAcc = prevAcceleration[player.carId];

			// Record velocity delta when flip starts
			if (player.isFlipping && !prev.isFlipping)
				storedAcc = (player.vel - prev.vel).Length();

			if (wavedash) {
				float acc = storedAcc;
				storedAcc = 0.0f;
				return acc / CommonValues::CAR_MAX_SPEED;
			}

			if (!player.isFlipping)
				storedAcc = 0.0f;

			return 0.0f;
		}
	};

} // namespace RLGC
