#pragma once
#include "Reward.h"
#include "RewardTiming.h"
#include <atomic>
#include <cmath>
#include <map>

// Ported from RLGymBot_v3 (AerialRewards.h). Logic unchanged unless noted.

namespace RLGC {

	// ============================================================================
	// PitchControlAerialReward - Reward fast aerial technique with pitch control
	// Detects and rewards jump → pitch up → jump sequence for quick aerial boost
	// 2026-02-08 | Rev 2.0 | NJH
	//
	// Rev 2.0 fixes (6 bugs, 2 critical):
	// [CRITICAL] Boost gate zeroed all rewards — removed sequenceStartBoost/boostMultiplier
	// [CRITICAL] Height computed on same tick as 2nd jump (always 0) — deferred to next tick
	// [minor] COMPLETED state was unreachable — removed from enum
	// [minor] DOUBLE_JUMP_WINDOW 0.4s too tight for humans — widened to 0.55s
	// [minor] GetPitchAccuracyScore clamped to 90° instead of MAX_PITCH_DEGREES — fixed
	// [minor] No boost-during-ascent signal — added 1.5x multiplier for boost use in Phase 2
	// ============================================================================
	class PitchControlAerialReward : public Reward {
	private:
		struct FastAerialTracker {
			enum State { WAITING, FIRST_JUMP, PITCHED_UP, SECOND_JUMP };

			State state = WAITING;
			float jumpStartTime = 0.0f;
			float firstJumpHeight = 0.0f;
			float maxPitchDuringFirstJump = 0.0f;
			bool isPitchedUpEnough = false;
			bool secondJumpDetected = false;
			float maxHeightAfterSecondJump = 0.0f;
			bool rewardPending = false;        // deferred payout — pay on the tick AFTER 2nd jump
			bool boostedDuringAscent = false;  // tracks boost use in Phase 2 (FIRST_JUMP → PITCHED_UP)
		};

		mutable std::map<uint32_t, FastAerialTracker> trackers;

		// Metrics
		mutable std::atomic<uint64_t> s_totalAwardedCount{0};
		mutable std::atomic<uint64_t> s_boostBonusCount{0};
		mutable std::atomic<int64_t>  s_totalRewardSum{0};  // stored as reward * 1000 (milliunits)

		// Configuration
		static constexpr float DOUBLE_JUMP_WINDOW    = 0.55f;   // widened from 0.4s — gives humans more headroom
		static constexpr float MIN_PITCH_DEGREES     = 25.0f;
		static constexpr float MAX_PITCH_DEGREES     = 65.0f;
		static constexpr float OPTIMAL_PITCH_DEGREES = 45.0f;
		static constexpr float PITCH_TOLERANCE       = 15.0f;
		static constexpr float BASE_REWARD           = 2.0f;
		static constexpr float HEIGHT_BONUS_SCALE    = 0.005f;
		static constexpr float MAX_HEIGHT_BONUS      = 3.0f;
		static constexpr float PITCH_BONUS_SCALE     = 0.02f;
		static constexpr float MAX_PITCH_BONUS       = 1.0f;
		static constexpr float BOOST_ASCENT_MULTIPLIER = 1.5f;  // applied when boost used during ascent

	public:
		virtual void Reset(const GameState& initialState) override {
			trackers.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;

			auto& tracker = trackers[player.carId];
			const Player& prevPlayer = state.prev->players[player.index];
			float dt = StepDt(state);

			// ====================================================================
			// Deferred payout: pay the reward the tick AFTER second jump so that
			// maxHeightAfterSecondJump has had at least one frame to accumulate.
			// ====================================================================
			if (tracker.rewardPending) {
				tracker.rewardPending = false;

				float heightGained = RS_MAX(0.0f, tracker.maxHeightAfterSecondJump - tracker.firstJumpHeight);
				float heightBonus  = RS_MIN(MAX_HEIGHT_BONUS, heightGained * HEIGHT_BONUS_SCALE);

				float pitchAccuracy = GetPitchAccuracyScore(tracker.maxPitchDuringFirstJump);
				float pitchBonus    = RS_MIN(MAX_PITCH_BONUS, pitchAccuracy * PITCH_BONUS_SCALE * 100.0f);

				float reward = BASE_REWARD + heightBonus + pitchBonus;

				if (tracker.boostedDuringAscent) {
					reward *= BOOST_ASCENT_MULTIPLIER;
					s_boostBonusCount.fetch_add(1);
				}

				tracker.state = FastAerialTracker::WAITING;
				tracker.secondJumpDetected = false;

				s_totalAwardedCount.fetch_add(1);
				s_totalRewardSum.fetch_add(static_cast<int64_t>(reward * 1000.0f));
				return reward;
			}

			// ====================================================================
			// Phase 1: First Jump Detection
			// ====================================================================
			if (prevPlayer.isOnGround && !player.isOnGround) {
				tracker.state                  = FastAerialTracker::FIRST_JUMP;
				tracker.jumpStartTime          = 0.0f;
				tracker.firstJumpHeight        = player.pos.z;
				tracker.maxPitchDuringFirstJump = 0.0f;
				tracker.isPitchedUpEnough      = false;
				tracker.secondJumpDetected     = false;
				tracker.boostedDuringAscent    = false;
				tracker.rewardPending          = false;
				return 0.0f;
			}

			// Global timeout: reset if airborne > 2.5s without completing sequence
			if (tracker.state != FastAerialTracker::WAITING) {
				tracker.jumpStartTime += dt;
				if (tracker.jumpStartTime > 2.5f) {
					tracker.state = FastAerialTracker::WAITING;
					return 0.0f;
				}
			}

			// ====================================================================
			// Phase 2: Pitch Validation (FIRST_JUMP state)
			// ====================================================================
			if (tracker.state == FastAerialTracker::FIRST_JUMP) {
				float pitchRadians = atan2(player.rotMat.forward.z,
					sqrt(player.rotMat.forward.x * player.rotMat.forward.x +
					     player.rotMat.forward.y * player.rotMat.forward.y));
				float pitchDegrees = pitchRadians * 57.2958f;

				if (pitchDegrees > tracker.maxPitchDuringFirstJump)
					tracker.maxPitchDuringFirstJump = pitchDegrees;

				if (pitchDegrees >= MIN_PITCH_DEGREES && pitchDegrees <= MAX_PITCH_DEGREES) {
					tracker.isPitchedUpEnough = true;
					tracker.state = FastAerialTracker::PITCHED_UP;
				}
			}

			// ====================================================================
			// Phase 3b: Boost-during-ascent tracking (FIRST_JUMP or PITCHED_UP)
			// ====================================================================
			if ((tracker.state == FastAerialTracker::FIRST_JUMP ||
			     tracker.state == FastAerialTracker::PITCHED_UP)
			    && player.prevAction.boost > 0.5f)
			{
				tracker.boostedDuringAscent = true;
			}

			// ====================================================================
			// Phase 3: Second Jump Detection (PITCHED_UP state)
			// ====================================================================
			if (tracker.state == FastAerialTracker::PITCHED_UP &&
			    tracker.jumpStartTime > 0.0f && tracker.jumpStartTime <= DOUBLE_JUMP_WINDOW)
			{
				// Pure double jump only — flips do NOT set hasDoubleJumped (RocketSim guarantee)
				if (player.hasDoubleJumped && !prevPlayer.hasDoubleJumped) {
					tracker.state              = FastAerialTracker::SECOND_JUMP;
					tracker.secondJumpDetected = true;
					tracker.maxHeightAfterSecondJump = player.pos.z;
					// Don't pay yet — defer to next tick so height can accumulate
					tracker.rewardPending = true;
					return 0.0f;
				}
			}

			// Track max height while in SECOND_JUMP (during deferred window)
			if (tracker.state == FastAerialTracker::SECOND_JUMP &&
			    player.pos.z > tracker.maxHeightAfterSecondJump)
			{
				tracker.maxHeightAfterSecondJump = player.pos.z;
			}

			return 0.0f;
		}

		struct Metrics {
			uint64_t totalAwarded;
			uint64_t boostBonus;
			double   rewardSum;
			double   boostBonusRate;
		};
		Metrics CollectMetrics() {
			Metrics m{};
			m.totalAwarded   = s_totalAwardedCount.exchange(0);
			m.boostBonus     = s_boostBonusCount.exchange(0);
			m.rewardSum      = double(s_totalRewardSum.exchange(0)) / 1000.0;
			m.boostBonusRate = (m.totalAwarded > 0) ? double(m.boostBonus) / m.totalAwarded : 0.0;
			return m;
		}

	protected:
		// Pitch accuracy score [0, 1]: peak at OPTIMAL_PITCH_DEGREES, zero outside [MIN, MAX].
		float GetPitchAccuracyScore(float pitchDegrees) const {
			float clampedPitch   = RS_CLAMP(pitchDegrees, MIN_PITCH_DEGREES, MAX_PITCH_DEGREES);
			float diffFromOptimal = fabs(clampedPitch - OPTIMAL_PITCH_DEGREES);
			float normalized     = diffFromOptimal / (OPTIMAL_PITCH_DEGREES - MIN_PITCH_DEGREES);
			return RS_CLAMP(1.0f - (normalized * normalized), 0.0f, 1.0f);
		}
	};

} // namespace RLGC
