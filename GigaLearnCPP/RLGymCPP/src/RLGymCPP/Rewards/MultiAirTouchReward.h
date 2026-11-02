#pragma once
#include "Reward.h"
#include "RewardTiming.h"
#include "AirDribbleUtilities.h"
#include "../CommonValues.h"
#include "../Math.h"
#include <cmath>
#include <unordered_map>

// Ported from RLGymBot_v3 (PreemptiveSaveRewards.h). Logic unchanged unless noted.

namespace RLGC {

    // ============================================================================
    // MULTI-AIR TOUCH REWARD
    // Rewards multiple consecutive touches on the ball in the air before it hits ground.
    // Teaches combo plays, aerial control, and ball possession in the air.
    //
    // Rev 1.1: Cross-player sequence intent (deliberate, not an oversight) — a
    // TEAMMATE touching the ball mid-sequence does NOT invalidate a player's own
    // touchCount; only an opponent touch (state.lastTouchCarID going to the other
    // team) or the ball grounding does. This matches "ball possession in the air"
    // above rather than treating this as a strictly-solo juggling reward.
    // ============================================================================
    class MultiAirTouchReward : public Reward {
    private:
        struct AirTouchMemory {
            int touchCount = 0;           // Number of touches in current air sequence
            float lastTouchTime = 0.0f;   // currentTime value at last touch (seconds)
            float highestAltitude = 0.0f; // Highest altitude reached
            bool ballWasInAir = false;    // Was ball in air on last frame?

            // NEW: Contact angle and softness tracking
            float lastContactAngleFactor = 0.0f;  // How well-angled was last touch (0-1)
            float lastSoftnessScore = 0.0f;       // How soft was last touch (0-1)
            float ballZVelocityGainLast = 0.0f;   // Z velocity gain from last touch
            Vec lastBallPos = Vec(0, 0, 0);       // Ball position last frame
            Vec lastBallVel = Vec(0, 0, 0);       // Ball velocity last frame

            // NEW: Boost tracking
            float sequenceStartBoost = 0.0f;      // Boost level when sequence started
        };

        std::unordered_map<uint32_t, AirTouchMemory> memory;
        // Rev 1.1: was an int incremented once per PreStep call (~15Hz post-tick-skip,
        // not the 120Hz the old TOUCH_TIMEOUT_STEPS/MIN_TOUCH_DELAY_STEPS comments
        // assumed) — switched to float seconds via state.deltaTime per the house
        // timing convention (CLAUDE.md §7C).
        float currentTime = 0.0f;

        static constexpr float BALL_GROUND_THRESHOLD = 150.0f;  // Height considered "on ground"
        static constexpr float MIN_TOUCH_HEIGHT = 300.0f;       // Only count touches above this height
        static constexpr float TOUCH_TIMEOUT_SECONDS = 1.0f;      // Reset if no touch for 1 real second
        static constexpr float MIN_TOUCH_DELAY_SECONDS = 0.5f;  // Minimum 0.5 real seconds between counted touches
        static constexpr float SINGLE_TOUCH_REWARD = 0.1f;
        static constexpr float DOUBLE_TOUCH_REWARD = 0.4f;
        static constexpr float TRIPLE_PLUS_REWARD = 0.7f;
        static constexpr float CONSISTENCY_BONUS = 0.3f;         // For spacing touches well
        static constexpr float HEIGHT_BONUS = 0.2f;             // For high aerial touches

        // NEW: Enhanced touch quality bonuses
        static constexpr float CONTACT_ANGLE_BONUS = 0.25f;       // Bonus for bottom-of-ball contacts
        static constexpr float SOFTNESS_BONUS = 0.15f;           // Bonus for soft first touch
        static constexpr float Z_VELOCITY_BONUS = 0.25f;         // Bonus for increasing ball height
        static constexpr float MAX_VELOCITY_DIFF_SOFT = 500.0f;  // Velocity diff threshold for "soft" contact
        static constexpr float ANGLE_TOLERANCE = 0.4f;           // Radians (~23°) tolerance for optimal angle

        // Soft penalty when ball horizontal (XY) speed > 2300 uu/s — car can't keep up.
        // Waived within 3000 UU of the opposing goal (hard-shot intent).
        float GetHorizVelPenaltyMul(const Player& player, const GameState& state) const {
            constexpr float HORIZ_VEL_LIMIT = 2300.0f;
            constexpr float HORIZ_VEL_MAX   = 3000.0f;
            constexpr float NEAR_GOAL_DIST  = 3000.0f;
            constexpr float MIN_MUL         = 0.5f;

            Vec oppGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_BACK
                : CommonValues::BLUE_GOAL_BACK;
            if ((state.ball.pos - oppGoal).Length() <= NEAR_GOAL_DIST)
                return 1.0f;

            float horizSpeed = sqrtf(state.ball.vel.x * state.ball.vel.x +
                                     state.ball.vel.y * state.ball.vel.y);
            if (horizSpeed <= HORIZ_VEL_LIMIT)
                return 1.0f;

            float t = RS_CLAMP((horizSpeed - HORIZ_VEL_LIMIT) / (HORIZ_VEL_MAX - HORIZ_VEL_LIMIT),
                               0.0f, 1.0f);
            return 1.0f - (1.0f - MIN_MUL) * t;  // [1.0, 0.5]
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            memory.clear();
            currentTime = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            auto& mem = memory[player.carId];
            float reward = 0.0f;

            bool ballInAir = state.ball.pos.z > BALL_GROUND_THRESHOLD;

            // Check if ball has landed (was in air, now not) - reset sequence
            if (mem.ballWasInAir && !ballInAir) {
                // Sequence ended, ball hit ground
                mem.touchCount = 0;
                mem.highestAltitude = 0.0f;
                mem.ballWasInAir = false;
                return 0.0f;
            }

            // Check if touch timeout exceeded (too long without touch)
            if (mem.touchCount > 0 && (currentTime - mem.lastTouchTime) > TOUCH_TIMEOUT_SECONDS) {
                // Reset if we haven't touched in a while (ball likely hit ground or sequence broke)
                mem.touchCount = 0;
                mem.highestAltitude = 0.0f;
            }

            // Rev 1.1: Opponent-touch invalidation — if the ball's last toucher is on the
            // opposing team while our sequence is open, a turnover happened mid-air; clear
            // the sequence so a later retouch starts fresh instead of banking "combo" credit
            // across the interruption. A teammate holding last-touch is deliberately a no-op
            // (see class header). Modeled on AirborneOnPossessionLostPenalty.h's team lookup.
            if (mem.touchCount > 0 && state.lastTouchCarID >= 0
                && (uint32_t)state.lastTouchCarID != player.carId) {
                for (const auto& p : state.players) {
                    if (p.carId != (uint32_t)state.lastTouchCarID) continue;
                    if (p.team != player.team) {
                        mem.touchCount = 0;
                        mem.highestAltitude = 0.0f;
                    }
                    break;
                }
            }

            // Detect NEW touch
            if (player.ballTouchedStep && ballInAir && player.pos.z > MIN_TOUCH_HEIGHT) {
                // Check if this is a new touch (not the same touch as last frame)
                // AND ensure minimum 0.5s delay between touches
                if (currentTime != mem.lastTouchTime &&
                    (currentTime - mem.lastTouchTime) >= MIN_TOUCH_DELAY_SECONDS) {
                    mem.touchCount++;
                    mem.lastTouchTime = currentTime;

                    // NEW: Capture boost on first touch of sequence
                    if (mem.touchCount == 1) {
                        mem.sequenceStartBoost = player.boost;
                    }

                    // Track highest altitude of touches
                    mem.highestAltitude = RS_MAX(mem.highestAltitude, player.pos.z);

                    // Reward based on touch count — base tier, deliberately NOT boost-scaled
                    // (Rev 1.1: see boostMultiplier below, which now only scales bonusReward)
                    float baseTouchReward = 0.0f;
                    if (mem.touchCount == 1) {
                        baseTouchReward = SINGLE_TOUCH_REWARD;
                    }
                    else if (mem.touchCount == 2) {
                        baseTouchReward = DOUBLE_TOUCH_REWARD;
                    }
                    else if (mem.touchCount >= 3) {
                        baseTouchReward = TRIPLE_PLUS_REWARD;
                    }

                    float bonusReward = 0.0f;

                    // Bonus for well-spaced touches (consistency)
                    // We want touches spread out, not rapid-fire
                    if (mem.touchCount >= 2) {
                        bonusReward += CONSISTENCY_BONUS * 0.3f;  // Small bonus per touch
                    }

                    // Bonus for high altitude touches
                    if (mem.highestAltitude > 1500.0f) {
                        bonusReward += HEIGHT_BONUS;
                    }

                    // ========== NEW: ENHANCED TOUCH QUALITY BONUSES ==========

                    // 1. CONTACT ANGLE BONUS - Reward touches on bottom of ball
                    if (state.prev) {
                        // Calculate optimal contact angle for field position
                        float desiredAngle = AirDribbleUtils::GetOptimalContactAngleForPosition(
                            player.team, state.ball.pos);

                        // Use ball normal relative to car position for actual angle
                        Vec toBall = (state.ball.pos - player.pos).Normalized();
                        float dotWithUp = toBall.Dot(player.rotMat.up);
                        float actualAngle = acos(RS_CLAMP(dotWithUp, -1.0f, 1.0f));

                        // Calculate angle factor (Gaussian scaling, peak at desired angle)
                        mem.lastContactAngleFactor = AirDribbleUtils::CalculateContactAngleFactor(
                            desiredAngle, actualAngle, ANGLE_TOLERANCE);

                        bonusReward += CONTACT_ANGLE_BONUS * mem.lastContactAngleFactor;
                    }

                    // 2. SOFTNESS BONUS - Reward softer first touches
                    // Only apply strong bonus on first touch of sequence (more important to be soft on pickup)
                    if (mem.touchCount == 1 || mem.touchCount == 2) {
                        Vec velDiff = player.vel - state.ball.vel;
                        float velocityDifference = velDiff.Length();
                        mem.lastSoftnessScore = AirDribbleUtils::CalculateSoftnessBonus(
                            velocityDifference, MAX_VELOCITY_DIFF_SOFT);

                        // First touch gets full softness bonus, second touch gets reduced bonus
                        float softnessMultiplier = (mem.touchCount == 1) ? 1.0f : 0.5f;
                        bonusReward += SOFTNESS_BONUS * mem.lastSoftnessScore * softnessMultiplier;
                    }

                    // 3. Z-VELOCITY GAIN BONUS - Reward lifting the ball higher
                    if (state.prev) {
                        mem.ballZVelocityGainLast = AirDribbleUtils::CalculateBallZVelocityGain(
                            state.prev->ball.pos.z,
                            state.ball.pos.z,
                            state.prev->ball.vel.z,
                            state.ball.vel.z);

                        // Only reward if ball is actually gaining height
                        if (mem.ballZVelocityGainLast > 0.0f) {
                            bonusReward += Z_VELOCITY_BONUS * RS_CLAMP(mem.ballZVelocityGainLast / 500.0f, 0.0f, 1.0f);
                        }
                    }

                    // Rev 1.1: boost multiplier now scoped to bonus components only — a
                    // legitimate low-boost defensive clear still banks its base touch-tier
                    // reward, only the "extra polish" bonuses get discounted.
                    if (bonusReward > 0.0f) {
                        float boostMultiplier = RS_CLAMP(mem.sequenceStartBoost / 50.0f, 0.0f, 1.0f);
                        bonusReward *= boostMultiplier;
                    }

                    reward = baseTouchReward + bonusReward;

                    // Store ball state for next frame
                    mem.lastBallPos = state.ball.pos;
                    mem.lastBallVel = state.ball.vel;
                }
            }

            mem.ballWasInAir = ballInAir;

            // Horizontal speed cap: penalize ball horiz speed > 1900 uu/s (car can't keep up)
            if (reward > 0.0f)
                reward *= GetHorizVelPenaltyMul(player, state);

            return reward;
        }

        void PreStep(const GameState& state) override {
            currentTime += StepDt(state);
        }

        // Helper to get current touch count for a player (useful for debugging/logging)
        int GetCurrentTouchCount(uint32_t carId) const {
            auto it = memory.find(carId);
            if (it != memory.end()) {
                return it->second.touchCount;
            }
            return 0;
        }
    };

} // namespace RLGC
