#pragma once
#include "../Reward.h"
#include "../../Math.h"

namespace RLGC {

	// FlipResetTouchReward (2026-11-11, user-directed): a COARSE flip-reset reward. Deliberately has no geometry, no ball height, no car height, no wheels
	// test, no cooldown. It pays 1.0 on the step where
	//   (a) the player touched the ball on the PREVIOUS step, and
	//   (b) the engine's HasFlipReset() is true now and was false on the previous step (the false -> true edge).
	// Why the touch is read on the previous step: with >= 3 wheels on the ball the engine reports isOnGround = true on the touch step, so HasFlipReset()
	// only turns true on the NEXT step (see FlipResetReward's header, 2026-11-07 audit). Why the edge: HasFlipReset() stays true for up to 1.25 s after
	// a reset, so without it every step of that window would pay.
	//
	// KNOWN COARSENESS (by request; the telemetry measures it): HasFlipReset() = !isOnGround && has flip/jump && !hasJumped, so it is also true for any
	// airborne car that did not jump, e.g. one that drives off a wall, a ramp or the ceiling. A touch on the step before that (a wall-ride touch) pays
	// without a real reset. FlipReset/Any Rate vs FlipReset/After Touch Rate vs the FlipReset reward's own events show how much of the pay is real.
	// Wrapped ZeroSum in BuildRewards like every event term. Predicates are static so Telemetry/FlipResetTelemetry.h reads the exact same rule.
	class FlipResetTouchReward : public Reward {
	public:
		// HasFlipReset() false -> true between the previous step and this one (any cause). False on the first step of an episode.
		static bool ResetEdge(const Player& p) {
			return p.prev && !p.prev->HasFlipReset() && p.HasFlipReset();
		}
		// The edge, with a ball touch by this player on the previous step.
		static bool ResetAfterTouch(const Player& p) {
			return ResetEdge(p) && p.prev->ballTouchedStep;
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			return ResetAfterTouch(player) ? 1.0f : 0.0f;
		}
	};
}
