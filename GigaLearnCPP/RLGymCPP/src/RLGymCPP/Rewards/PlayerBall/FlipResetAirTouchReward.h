#pragma once
#include "FlipResetTouchReward.h"

namespace RLGC {

	// FlipResetAirTouchReward (2026-11-12, user-directed): FlipResetTouchReward with ONE difference - the touch must have happened with the ball above
	// AIR_BALL_Z (400 uu). Everything else is inherited unchanged: pays 1.0 when the player touched the ball on the PREVIOUS step and HasFlipReset() just
	// turned true (false -> true). See FlipResetTouchReward.h for the rationale and the known coarseness (wall/ramp exits also set HasFlipReset()).
	// Ball height is read at the TOUCH step (state.prev->ball.pos.z), not now: the ball has moved a step (8 ticks) since the touch. Strict ">": z == 400 does not pay.
	// No state.prev (first step of an episode) => no pay (ResetEdge is already false there).
	// Stacked with FlipResetTouchReward both pay on a high touch (weights 60 + 120 at 2026-11-12), so a high reset is worth the sum.
	class FlipResetAirTouchReward : public Reward {
	public:
		static constexpr float AIR_BALL_Z = 400.0f;

		static bool AirResetAfterTouch(const Player& p, const GameState& s) {
			return FlipResetTouchReward::ResetAfterTouch(p) && s.prev && s.prev->ball.pos.z > AIR_BALL_Z;
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			return AirResetAfterTouch(player, state) ? 1.0f : 0.0f;
		}
	};
}
