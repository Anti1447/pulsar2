#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"

namespace RLGC {

	// WALL-JUMP-TO-BALL (2026-07-21, user-directed): rewards going for the ball in
	// the air after launching off a wall - the wall-read aerial (drive up a wall,
	// leave it, strike the ball). Built as an exact-telescoping PBRS so it is
	// UNFARMABLE BY CONSTRUCTION (the user's requirement): while the player is
	// airborne AND has left a wall since it was last supported, Phi = exp(-|ball -
	// car| / LIU_DIST_SCALE); otherwise Phi = 0. r = gamma*Phi(s') - Phi(s). Closing
	// on the ball after a wall launch pays +dPhi immediately; landing clears the
	// latch and refunds the approach in full (Phi -> 0 is charged as -Phi at the
	// landing step), so climb-and-retreat cycles telescope to ~0 - the discounted
	// sum over ANY trajectory is -Phi(start). Leaving a wall AWAY from the ball pays
	// ~0 (dist large -> Phi ~ 0), so it rewards wall exits TOWARD the ball only.
	// gamma MUST equal the learner's gaeGamma. Wrap in ZeroSumReward (a linear map
	// of a per-player potential stays a telescoping potential -> keeps the stack
	// zero-sum). NEVER gate.
	//
	// "Launched off a wall" = a per-player latch set the step the player becomes
	// airborne having been ON A WALL the previous step, held through the WHOLE
	// aerial (so the full wall-to-ball flight is shaped, not just the launch tick),
	// and cleared on any ground/wall contact or demo. It is intentionally NOT
	// cleared on the ball touch: clearing mid-air would drop Phi without charging
	// the refund and reopen a farm. A wall = the car is supported (isOnGround) with its roof
	// pointing closer to horizontal than the engine's own ground/wall split (|up.z| < 1/sqrt(2), the autoflip
	// NORM_Z_THRESH): flat ground (up.z~1) and ceiling (up.z~-1) are excluded, side/back/corner walls (up.z~0)
	// included. (The first version used the body-contact flag worldContact.hasContact, which wheels on a wall never
	// set; replaced 2026-11-02, see REWARD_CHANGE_LOG.) Being a plain state
	// latch, Phi stays a function of (augmented) state, so telescoping holds.
	// Per-arena instance -> no cross-arena state.
	class WallJumpToBallReward : public Reward {
	public:
		constexpr static float LIU_DIST_SCALE = 1410;            // matches the other proximity potentials
		constexpr static float WALL_NORMAL_Z_MAX = 0.7071068f;   // 1/sqrt(2) = engine ground/wall split
		float gamma;
		WallJumpToBallReward(float gamma = 0.99f) : gamma(gamma) {}

		std::vector<char> launched; // per player.index (char = bool latch)

		// Wheels on a wall: supported by the surface and the car's roof pointing sideways (|up.z| < 1/sqrt(2), the engine's
		// ground/wall split). Flat ground (up.z ~ 1) and a ceiling or turtled car (up.z ~ -1) are excluded. NOT worldContact:
		// that flag is a BODY collision, cleared every tick, and a car riding a wall on its wheels never sets it
		// (2026-11-02 audit: the previous detector fired on 1 step of a 40-step wall drive and never while up the wall).
		static bool OnWall(const Player& p) {
			return p.isOnGround && fabsf(p.rotMat.up.z) < WALL_NORMAL_Z_MAX;
		}

		virtual void Reset(const GameState& initialState) override {
			launched.assign(initialState.players.size(), 0);
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if ((size_t)player.index >= launched.size())
				launched.resize(player.index + 1, 0);
			char& latch = launched[player.index];
			bool prevLatch = latch != 0;

			// Need both previous states to diff a potential; a demo/respawn teleport is
			// not the player's action. In any of these cases, drop the latch and skip.
			if (!state.prev || !player.prev || player.isDemoed || player.prev->isDemoed) {
				latch = 0;
				return 0;
			}

			bool curLatch;
			if (player.isOnGround)
				curLatch = false;             // supported (ground OR wall): no active launch
			else if (OnWall(*player.prev))
				curLatch = true;              // just left a wall into the air
			else
				curLatch = prevLatch;         // persist through the aerial
			latch = curLatch ? 1 : 0;

			float phiPrev = prevLatch
				? expf(-(state.prev->ball.pos - player.prev->pos).Length() / LIU_DIST_SCALE) : 0.f;
			float phiCur = curLatch
				? expf(-(state.ball.pos - player.pos).Length() / LIU_DIST_SCALE) : 0.f;

			return gamma * phiCur - phiPrev;
		}
	};
}
