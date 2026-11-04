#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"

namespace RLGC {

	// TEMPO CREDIT (2026-07-16, user-directed "energy reward"): the player's total
	// MECHANICAL energy as an exact PBRS potential — Phi ~ (0.5|v|^2 + g*z), r =
	// gamma*Phi(s') - Phi(s). The farmability the raw form invites is removed by
	// construction, not by tuning: telescoping makes every energy-pump loop worth
	// exactly zero (speed up / flip about / slow down nets 0), the ZeroSum wrapper
	// kills co-farming, and gamma MUST match the learner's gaeGamma (stack rule).
	// What it buys is INSTANT, LOCAL credit for momentum decisions: a flip that dies
	// in place prices negative within a step instead of diffusing across the horizon
	// (the measured wavedash-class SNR starvation, MECHANICS.md). PE is included so
	// KE->PE conversion (jumping, climbing) is energy-neutral — this term must never
	// tax aerials. Ball energy is deliberately absent: TouchAccel IS the ball-energy
	// reward. STORED BOOST is in the sum since 2026-07-19 (see Phi): fuel is energy,
	// so the potential is indifferent between holding and spending it — the dump-
	// into-ground-speed incentive the boost-free form created is gone.
	class CarEnergyPotentialReward : public Reward {
	public:
		float gamma;
		// No default: gamma MUST match the learner's gaeGamma (stack rule)
		explicit CarEnergyPotentialReward(float gamma) : gamma(gamma) {}

		static float Phi(const Player& p) {
			constexpr float KE_NORM = 0.5f * 2300.f * 2300.f; // KE at supersonic ~ 1.0
			float ke = 0.5f * p.vel.LengthSq();
			float pe = 650.f * RS_MAX(0.f, p.pos.z - 17.f);   // g = 650 uu/s^2
			// STORED BOOST IS ENERGY (2026-07-19, measured fix): without this term the
			// potential valued fuel at zero, so dumping boost into ground speed was free
			// energy gain and holding it was worthless - and the 29.3B aerial census
			// caught the consequence: mean boost at aerial opportunities collapsed
			// 54 -> 18 while takeoff ATTEMPTS rose 8x (the bot wants to go up, arrives
			// empty). SQRT-shaped (RLGym-PPO guide's scarcity curve, adopted same day):
			// the marginal value of fuel is highest near empty - exactly where the
			// census caught the failure - and cheap near full, so the term defends a
			// working reserve without paying for hoarding, and small pads become
			// automatically worth detouring for when low. Full tank = 1.0 = supersonic
			// KE. Still exact PBRS: ANY Phi telescopes - unfarmable; pad pickups are
			// real player actions (the demo-respawn tank stays guarded in GetReward).
			// Pickup credit intentionally overlaps GuardedPickupBoost - this term adds
			// the SPEND side of the ledger, which a pickup reward cannot express.
			// Softened near empty: (sqrt(0.01b + 0.01) - 0.1) / 0.905 keeps 0 at empty and 1.0 at full
			// (0.905 = sqrt(1.01) - 0.1) but caps the slope at empty, so draining the last 2 boost costs
			// ~0.087 per step instead of 0.148 under plain sqrt(0.01b)
			float be = (sqrtf(0.01f * p.boost + 0.01f) - 0.1f) / 0.905f * KE_NORM; // boost 0-100 -> 0..KE_NORM, concave
			return (ke + pe + be) / KE_NORM;
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev || !player.prev) {
				if (trace) trace->Gate(player.carId, "has_prev", false, "first step of the episode");
				return 0;
			}
			// Demo/respawn teleports are not the player's action
			if (player.isDemoed || player.isDemoed != player.prev->isDemoed) {
				if (trace) trace->Gate(player.carId, "not_demo_transition", false, "demoed, or demo state changed (teleport is not the player's action)");
				return 0;
			}
			// Terminal: on a goal step Phi(s') = 0, so the episode leaves no energy bonus behind.
			// Truncations still bootstrap, so they keep the full gamma * Phi(s').
			float phiNext = state.goalScored ? 0.f : Phi(player);
			if (trace) {
				trace->Gate(player.carId, "has_prev", true);
				trace->Gate(player.carId, "not_demo_transition", true);
				trace->Gate(player.carId, "not_goal_step", !state.goalScored, state.goalScored ? "Phi(s') = 0 on the goal step" : "");
				trace->Component(player.carId, "phi_prev", Phi(*player.prev));
				trace->Component(player.carId, "phi_next", phiNext);
				trace->Component(player.carId, "gamma", gamma);
				trace->Point(player.carId, TraceRole::Phi, player.pos);
			}
			return gamma * phiNext - Phi(*player.prev);
		}
	};
}
