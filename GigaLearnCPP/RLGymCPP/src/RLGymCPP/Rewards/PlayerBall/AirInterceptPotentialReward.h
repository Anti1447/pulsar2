#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"

namespace RLGC {

	// Pre-touch aerial-approach potential - the gradient BEFORE the first air
	// touch ever lands. Phi = ballHighFrac * carUpFrac * exp(-dist/LIU_DIST_SCALE);
	// r = gamma*Phi(s') - Phi(s). Phi is 0 whenever the car is grounded or the
	// ball is low, so parking under a dropping ball earns nothing, while
	// jumping/boosting toward a high ball pays +dPhi immediately and a whiff or
	// landing refunds it - attempts become gradient-visible at ~zero net cost.
	// Exact PBRS: telescopes to ~0 over any cycle, cannot be farmed. NEVER gate.
	class AirInterceptPotentialReward : public Reward {
	public:
		constexpr static float LIU_DIST_SCALE = 1410;
		constexpr static float BALL_MIN_Z = 300, BALL_FULL_Z = 1600;
		constexpr static float CAR_MIN_Z = 17, CAR_FULL_Z = 517;
		float gamma;
		AirInterceptPotentialReward(float gamma = 0.99f) : gamma(gamma) {}

		static float Phi(const Vec& ballPos, const Vec& carPos) {
			float ballFrac = RS_CLAMP((ballPos.z - BALL_MIN_Z) / (BALL_FULL_Z - BALL_MIN_Z), 0, 1);
			float carFrac = RS_CLAMP((carPos.z - CAR_MIN_Z) / (CAR_FULL_Z - CAR_MIN_Z), 0, 1);
			return ballFrac * carFrac * expf(-(ballPos - carPos).Length() / LIU_DIST_SCALE);
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev || !player.prev)
				return 0; // First step of the episode: nothing to diff against

			// A demo/respawn teleport is not the player's action; don't charge/pay the jump
			if (player.isDemoed || player.prev->isDemoed)
				return 0;

			return gamma * Phi(state.ball.pos, player.pos) - Phi(state.prev->ball.pos, player.prev->pos);
		}
	};
}
