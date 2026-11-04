#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"

namespace RLGC {

	class AerialTouchReward : public Reward {
	public:
		constexpr static float BALL_MIN_Z = 150;            // below this pays 0
		constexpr static float BALL_FULL_Z = 1450;           // full height credit at/above
		constexpr static float MAX_CREDIT_AIR_TIME = 1.75f;  // seconds of flight for full air credit
		constexpr static float FULL_CREDIT_DELTA_V = 500;    // uu/s of ball delta-v for full credit
		constexpr static int REFIRE_COOLDOWN_STEPS = 12;     // ~0.8s at tickSkip 8 (15Hz steps); re-derive if tickSkip changes

		std::vector<int> stepsSincePay; // per player.index; per-arena instance, so safe

		virtual void Reset(const GameState& initialState) override {
			stepsSincePay.assign(initialState.players.size(), REFIRE_COOLDOWN_STEPS + 1);
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if ((size_t)player.index >= stepsSincePay.size())
				stepsSincePay.resize(player.index + 1, REFIRE_COOLDOWN_STEPS + 1);
			int& sincePay = stepsSincePay[player.index];
			sincePay++;

			if (!state.prev || !player.ballTouchedStep || player.isOnGround)
				return 0;
			if (sincePay <= REFIRE_COOLDOWN_STEPS)
				return 0;

			float ballFrac = RS_CLAMP((state.ball.pos.z - BALL_MIN_Z) / (BALL_FULL_Z - BALL_MIN_Z), 0, 1);
			float airFrac = RS_MIN(1.0f, player.airTime / MAX_CREDIT_AIR_TIME);
			float heightFrac = RS_MIN(ballFrac, airFrac);
			if (heightFrac <= 0)
				return 0;

			float impulseFrac = RS_MIN(1.0f, (state.ball.vel - state.prev->ball.vel).Length() / FULL_CREDIT_DELTA_V);
			float r = heightFrac * impulseFrac;
			if (r > 0)
				sincePay = 0;
			return r;
		}
	};
}
