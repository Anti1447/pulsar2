#pragma once
#include "Reward.h"
#include "RewardMath.h"
#include <atomic>

// ============================================================================
// KickoffStandoffPenalty
// Backstop for the kickoff standoff (all cars idle/wavedash, nobody ever touches the ball).
// Fires ONCE, on the terminal step, when the episode ends with the ball still sitting
// untouched at the kickoff spot. In practice that is the NoTouchCondition truncation.
//
//   Trigger:  isFinal && nobody touched the ball this episode && RewardMath::IsKickoffActive(ball)
//   Payout:   -1 to every player (scale with the WeightedReward weight, ~100-150)
//   Not fired: goals (ball ends in the net), touched kickoffs, mid-play timeouts (ball has
//              left center), random-state episodes where the ball didn't start at center.
//   Anti-farming: nothing to farm; it is a fixed cost paid only for the standoff.
//
// Metrics (static atomics, W&B): Standoffs / Episodes -> Kickoff/Standoff/*
// ============================================================================
namespace RLGC {
	class KickoffStandoffPenalty : public Reward {
	private:
		inline static std::atomic<long long> s_standoffs{ 0 };
		inline static std::atomic<long long> s_episodes{ 0 };

		float penalty;
		bool touchedThisEpisode = false;
		bool countedThisEpisode = false;

	public:
		explicit KickoffStandoffPenalty(float penaltyIn = -1.0f) : penalty(penaltyIn) {}

		struct Metrics {
			long long standoffs = 0;
			long long episodes = 0;
			double rate = 0.0; // standoffs / episodes
		};

		static Metrics GetAndResetMetrics() {
			Metrics m;
			m.standoffs = s_standoffs.exchange(0);
			m.episodes = s_episodes.exchange(0);
			if (m.episodes > 0)
				m.rate = double(m.standoffs) / double(m.episodes);
			return m;
		}

		virtual void Reset(const GameState& initialState) override {
			touchedThisEpisode = false;
			countedThisEpisode = false;
			s_episodes.fetch_add(1);
		}

		// Runs once per step before GetReward, so a touch on the final step is already seen.
		virtual void PreStep(const GameState& state) override {
			if (touchedThisEpisode)
				return;
			for (const Player& p : state.players) {
				if (p.ballTouchedStep) {
					touchedThisEpisode = true;
					return;
				}
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!isFinal || touchedThisEpisode || state.goalScored)
				return 0.0f;
			if (!RewardMath::IsKickoffActive(state.ball.pos, state.ball.vel))
				return 0.0f;

			if (!countedThisEpisode) {
				countedThisEpisode = true;
				s_standoffs.fetch_add(1);
			}
			return penalty;
		}
	};
}
