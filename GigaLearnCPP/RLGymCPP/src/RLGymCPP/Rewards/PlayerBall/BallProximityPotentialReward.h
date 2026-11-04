#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"

namespace RLGC {

	// True-potential form of player->ball proximity (Nexto's liu_dist player quality):
	// r = gamma*Phi(s') - Phi(s), Phi = exp(-dist/LIU_DIST_SCALE). Exact PBRS, so movement
	// cycles and whack-and-chase loops telescope to ~0 and episode truncation can't be
	// harvested — unlike VelocityPlayerToBallReward, the Phi drop when the ball is knocked
	// away is charged immediately. gamma must match the learner's gaeGamma.
	//
	// TEAM POTENTIAL (4.0 team play, spacing-aware since 2026-11-03): Phi is a rank-weighted, crowding-discounted sum of the team's
	// cars (see TeamPhi): the closest car counts fully, the next at 1/2, the next at 1/4, and cars at nearly the same ball distance
	// share the lower weight, so stacking on the ball pays less than spreading. Normalised per team size (MaxSum) so 1v1, 2v2 and 3v3 have the
	// same largest potential, 1.08. With one car per team this is algebraically identical to the old per-player form (verified by test).
	// Every teammate receives the same value, so under ZeroSumReward the result is exactly Psi = Phi_ownTeam - Phi_oppTeam at any
	// teamSpirit. Still an exact potential (a function of state only): telescoping holds.
	class BallProximityPotentialReward : public Reward {
	public:
		constexpr static float LIU_DIST_SCALE = 1410; // Nexto: max driving speed without boost
		float gamma;
		BallProximityPotentialReward(float gamma = 0.99f) : gamma(gamma) {}

		// SPACING-AWARE TEAM POTENTIAL (2026-11-03, user-directed; was: the closest alive car only).
		// Alive team cars sorted by ball distance, d_1 <= d_2 <= ...; phi_k = CarPhi(d_k). Rank weights b_k = RANK_DECAY^(k-1)
		// (1, 1/2, 1/4), cut by a CROWDING discount between neighbours: c_k = 1 - smoothstep((d_{k+1} - d_k - CROWD_INNER) /
		// (CROWD_OUTER - CROWD_INNER)) is 1 when two cars are within CROWD_INNER of the same ball distance and 0 beyond CROWD_OUTER.
		// Built from the back, w_m = b_m and w_k = b_k - (b_k - w_{k+1}) * c_k, so a crowded car takes the weight of the car behind it
		// (two crowded cars = 1/2 each, a crowded trio = 1/4 each) and a spaced team keeps (1, 1/2, 1/4). At a rank swap the gap is 0,
		// c = 1 and the two weights are equal, so the potential is continuous. Phi = sum(w_k * phi_k) * phi(0) / MaxSum(N), N = the team's
		// full size (alive or not), so the largest team potential is the same 1.08 in 1v1, 2v2 and 3v3 and one weight serves all modes.
		// With one car per team this is exactly the old per-player form (w = 1, MaxSum = 1.08). Every teammate gets the same value, and
		// the potential is still a function of the state only, so telescoping and the demo-flip skip below are unchanged.
		// TODO (user, 2026-11-03): CROWD_INNER 500 / CROWD_OUTER 1600 and the (1, 1/2, 1/4) ranks are a baseline; re-evaluate on real
		// 2v2/3v3 results (spacing the policy actually plays, rear-man distance, panel share) and adjust.
		constexpr static float RANK_DECAY = 0.5f;
		constexpr static float CROWD_INNER = 500, CROWD_OUTER = 1600;
		constexpr static int MAX_TEAM = 16;

		// One car's proximity. 5.0 FAR-FIELD TERM (PULSAR5.md): the pure exp saturates beyond ~2000uu - the measured
		// "gradient desert" behind ~9% dead far-field frames (behavior_symptoms). A small linear component gives the potential
		// a nonzero slope everywhere.
		static float CarPhi(float d) {
			return expf(-d / LIU_DIST_SCALE) + 0.08f * RS_MAX(0.f, 1.f - d / 12000.f);
		}

		// 1 when the gap is <= CROWD_INNER, 0 when >= CROWD_OUTER, smoothstep between
		static float Crowd(float gap) {
			float t = RS_CLAMP((gap - CROWD_INNER) / (CROWD_OUTER - CROWD_INNER), 0.f, 1.f);
			return 1.f - t * t * (3.f - 2.f * t);
		}

		// The largest value the un-normalised sum can reach for a team of `total` cars (found by grid search over the car distances, see the
		// audit test): 1v1 phi(0) = 1.08; 2v2 1.27815 (closest car at 0, second at ~1553 uu, just outside full crowding); 3v3 1.32016 (0, ~1544,
		// ~3100). Dividing by it and scaling to phi(0) makes the largest team potential 1.08 in EVERY mode, so one weight serves 1v1, 2v2 and
		// 3v3 (user-directed 2026-11-03). Teams larger than 3 reuse the 3v3 constant (not tuned for them).
		static float MaxSum(int total) {
			if (total <= 1) return 1.08f;
			if (total == 2) return 1.27815f;
			return 1.32016f;
		}

		// 0 if the whole team is demoed (no car = no coverage).
		static float TeamPhi(const GameState& state, Team team) {
			float d[MAX_TEAM]; int m = 0, total = 0;
			for (const Player& p : state.players) {
				if (p.team != team)
					continue;
				total++;
				if (p.isDemoed || m >= MAX_TEAM)
					continue;
				float dist = (state.ball.pos - p.pos).Length();
				int k = m++;
				while (k > 0 && d[k - 1] > dist) { d[k] = d[k - 1]; k--; } // insertion sort, ascending
				d[k] = dist;
			}
			if (m == 0)
				return 0;

			float b[MAX_TEAM]; b[0] = 1.f;
			for (int k = 1; k < m; k++) b[k] = b[k - 1] * RANK_DECAY;

			float w = b[m - 1], sum = w * CarPhi(d[m - 1]);
			for (int k = m - 2; k >= 0; k--) {
				w = b[k] - (b[k] - w) * Crowd(d[k + 1] - d[k]);
				sum += w * CarPhi(d[k]);
			}

			return sum * CarPhi(0.f) / MaxSum(total);
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0; // First step of the episode: nothing to diff against

			// A demo/respawn teleport is not anyone's action; skip the step whenever a team
			// car's demo state flips (the team analogue of the old self-only guard — with one
			// car per team the returned values are identical: transitions return 0 here, and
			// steady-demoed steps diff an empty-team Phi of 0 against 0).
			for (const Player& p : state.players) {
				if (p.team != player.team)
					continue;
				if (!p.prev || p.isDemoed != p.prev->isDemoed)
					return 0;
			}

			return gamma * TeamPhi(state, player.team) - TeamPhi(*state.prev, player.team);
		}
	};
}
