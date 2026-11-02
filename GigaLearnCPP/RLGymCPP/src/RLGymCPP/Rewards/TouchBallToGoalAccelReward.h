#pragma once
#include "Reward.h"
#include "RewardTiming.h"
#include "../CommonValues.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace RLGC {

	// ============================================================================
	// TouchBallToGoalAccelReward — Lucy-SKG "Touch Ball-to-Goal Acceleration" (arXiv 2305.15801).
	//
	// On a touch, pay the change in ball speed ALONG the line to the opponent goal (not total speed):
	//   +  speeding the ball up toward the opponent goal (incl. turning an incoming shot around),
	//   -  a touch that leaves the ball heading toward OUR goal and made it worse,
	//   0  sideways touches, traps/slow-downs that still leave the ball heading upfield.
	// 1.0 = 0 -> MAX_SPEED (supersonic, ~2300 uu/s) toward goal in one touch. Signed, clamped to [-1, 1].
	//
	// Differs from TouchAccelReward (total speed, no direction, never negative) and
	// DirectionalAccelerationReward (goal-alignment multiplier, never negative).
	// Speed-pump guard copied from TouchAccelReward Rev 3.0: a decaying per-player high-water mark of
	// goal-directed speed, so speed the ball merely lost to drag/walls and the player re-adds is not repaid.
	//
	// 2026-10-17 | Rev 1.0 | NJH+Claude. Replaces TouchAccelReward in Gold (same weight).
	// ============================================================================
	class TouchBallToGoalAccelReward : public Reward {
	public:
		constexpr static float MAX_SPEED = 2300.0f;
		constexpr static float MIN_PAYABLE = 0.05f;        // ~115 uu/s of net goal-directed gain
		constexpr static float HW_DECAY_UU_PER_S = 200.0f;
		constexpr static float HW_RESET_SPEED = 600.0f;     // ball this slow = fresh start

		virtual void Reset(const GameState&) override { _highWater.clear(); }

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;
			const float dt = StepDt(state);

			float& hw = _highWater[player.carId];
			hw = std::max(0.0f, hw - HW_DECAY_UU_PER_S * dt);
			if (state.prev->ball.vel.Length() < HW_RESET_SPEED) hw = 0.0f;

			if (!player.ballTouchedStep) return 0.0f;

			const Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
			const Vec g = (oppGoal - state.prev->ball.pos).Normalized(); // geometry from the pre-contact position
			const float vPrev = state.prev->ball.vel.Dot(g);
			const float vNow = state.ball.vel.Dot(g);

			if (vNow < 0.0f) {
				// Ball now heads toward our goal: punish only if this touch made it worse.
				const float worse = std::min(0.0f, vNow - std::min(vPrev, 0.0f));
				return std::max(-1.0f, worse / MAX_SPEED);
			}

			// hw == 0 means "no mark": it must not floor the baseline, or turning an incoming shot around
			// (vPrev < 0) would lose the credit for the part below zero.
			const float base = (hw > 0.0f) ? std::max(vPrev, hw) : vPrev;
			const float gain = std::min(1.0f, (vNow - base) / MAX_SPEED);
			if (gain < MIN_PAYABLE) return 0.0f;
			hw = std::max(hw, vNow);
			return gain;
		}

	private:
		mutable std::unordered_map<uint32_t, float> _highWater;
	};

}
