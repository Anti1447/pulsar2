#pragma once

/*
 * File: RoleCycleTransitionReward.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Fully discrete, event-gated companion extracted from RoleAwarenessReward
 *   (RotationRewards.h) — rewards a player only on the step they COMPLETE a
 *   correctly-ordered rotation leg, using the same DetermineRoleCommon/RoleType
 *   classification (FIRST/SECOND/LAST/TRANSITIONING):
 *
 *     Retreat leg:  FIRST -> LAST                      : +retreatReward
 *     Advance leg:  LAST -> SECOND -> FIRST (3v3)       : +advanceRewardFull
 *                   LAST -> FIRST (2v2, SECOND doesn't
 *                     structurally exist)               : +advanceRewardFull
 *                   LAST -> FIRST (3v3, SECOND WAS
 *                     reachable but skipped)             : +advanceRewardSkip (discounted)
 *
 *   Unlike RoleAwarenessReward's narrower AWAITING_ADVANCE (which only tracks
 *   LAST->SECOND and treats a direct LAST->FIRST skip as a silent, unrewarded
 *   cycle reset), this class's advance leg is scoped to LAST->FIRST directly —
 *   SECOND is an optional waypoint, not a mandatory gate. Whether SECOND was
 *   reachable at all (CountGoalsideTeammates >= 2) is captured once, the moment
 *   the advance leg begins (LAST is reached), not re-checked every step — a
 *   3v3 team down a player to a demo mid-leg still gets full credit, since no
 *   slot existed to skip. Skipping a genuinely available SECOND still counts
 *   as a valid, positive completion (per design), just discounted relative to
 *   visiting it.
 *
 *   Out-of-order or timed-out progress resets the leg's stage to NONE with no
 *   payout — same correct-order/timeout enforcement RoleAwarenessReward already
 *   has (5s windows), just decoupled into its own class and extended through
 *   FIRST instead of stopping at SECOND.
 *
 *   RoleAwarenessReward itself was trimmed (Rev 3.0, 2026-08-27) to drop its 4
 *   discrete one-time bonuses (RETREAT_BONUS, ADVANCE_BONUS, RECOVERY_BONUS,
 *   and the generic "+0.3 any lateral transition" bonus) — this class now owns
 *   that signal exclusively, avoiding double-payment at the 5 RewardConfig.cpp
 *   sites where both are wired (via ProgressiveRotationReward). RoleAwarenessReward
 *   keeps only its 2 continuous per-step penalties (SECOND-stall, maxRoleTime
 *   camping) plus the CycleStage/timeInRole bookkeeping those penalties depend on.
 *   The RECOVERY_BONUS (TRANSITIONING->goalside) concept was dropped entirely,
 *   not ported — orthogonal to cycle correctness, and GoalsideReward already
 *   continuously penalizes being TRANSITIONING elsewhere.
 *
 *   PreStep computes all shared per-player state once per step (not per-player
 *   re-derivation in GetReward) — same rationale as every other transition
 *   reward added this session. A role transition is inherently a single-player,
 *   single-event-per-step concept (mutually exclusive cycle stages), so a
 *   simple per-player pending-reward map suffices — no multi-event buffer
 *   needed (unlike InstantSpacingTransitionReward's per-pair case).
 *
 *   Demoed-player handling is a deliberate improvement over RoleAwarenessReward
 *   (which has no isDemoed check at all): GetReward returns 0 for a demoed
 *   player, and PreStep suspends (does not erase) a demoed player's tracked
 *   state, so the pre-demo baseline survives respawn.
 *
 *   Not ZeroSumReward-wrapped anywhere — matches InstantSpacingReward/
 *   ProgressiveRotationReward's precedent: a team's own rotation discipline is
 *   a within-team coordination signal, not competitive/zero-sum.
 *
 *   See Reward_Change_Log.md 2026-08-27.
 *
 * Author: Nathan + Claude
 * Date: August 2026
 * Revision: 1.0
 */

#include "Reward.h"
#include "../Math.h"
#include "RotationRewards.h"
#include "RewardTiming.h"
#include <atomic>
#include <cstdint>
#include <cfloat>
#include <unordered_map>

namespace RLGC {

    // ============================================================================
    // ROLE CYCLE TRANSITION REWARD
    // 2026-08-27 | Rev 1.0 | NJH
    // ============================================================================
    class RoleCycleTransitionReward : public Reward {
    public:
        struct Config {
            float retreatWindowSec  = 5.0f;
            float advanceWindowSec  = 5.0f;
            float retreatReward     = 0.7f;
            float advanceRewardFull = 0.7f;
            float advanceRewardSkip = 0.45f;
        };

        struct Metrics {
            int    retreatCompletions          = 0;
            int    advanceCompletionsViaSecond = 0;
            int    advanceCompletionsNoSlot    = 0;
            int    advanceCompletionsSkipped   = 0;
            double rewardSum                   = 0.0;
        };

    private:
        enum class RoleCycleStage { NONE, AWAITING_RETREAT, AWAITING_ADVANCE };

        struct PlayerCycleState {
            RoleType       lastRole = RoleType::TRANSITIONING;
            RoleCycleStage cycleStage = RoleCycleStage::NONE;
            float          cycleStageTime = 0.0f;
            bool           sawSecondThisAdvance = false;
            bool           secondWasReachableAtEntry = false;
        };

        RotationConfig rotConfig;
        Config config;

        std::unordered_map<uint32_t, PlayerCycleState> states;
        std::unordered_map<uint32_t, float> pendingRewards;

        float gameTime = 0.0f;

        inline static std::atomic<int> s_retreatCompletions{ 0 };
        inline static std::atomic<int> s_advanceCompletionsViaSecond{ 0 };
        inline static std::atomic<int> s_advanceCompletionsNoSlot{ 0 };
        inline static std::atomic<int> s_advanceCompletionsSkipped{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };

    public:
        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.retreatCompletions = s_retreatCompletions.exchange(0);
            m.advanceCompletionsViaSecond = s_advanceCompletionsViaSecond.exchange(0);
            m.advanceCompletionsNoSlot = s_advanceCompletionsNoSlot.exchange(0);
            m.advanceCompletionsSkipped = s_advanceCompletionsSkipped.exchange(0);
            m.rewardSum = double(s_rewardMilli.exchange(0)) / 1000.0;
            return m;
        }

        RoleCycleTransitionReward(RotationPhase phase)
            : rotConfig(RotationConfig::ForPhase(phase)), config() {}
        RoleCycleTransitionReward(RotationPhase phase, const Config& cfg)
            : rotConfig(RotationConfig::ForPhase(phase)), config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            states.clear();
            pendingRewards.clear();
            gameTime = 0.0f;
        }

        // PreStep runs exactly once per step — all role-transition detection lives
        // here. GetReward runs once per player and must not re-derive transitions.
        virtual void PreStep(const GameState& state) override {
            float dt = StepDt(state);
            gameTime += dt;
            pendingRewards.clear();

            if (!rotConfig.enableRoleCycling) return;

            // Kickoff gate — matches InstantSpacingReward/RoleAwarenessReward's
            // convention. Role churn during a kickoff scramble isn't real rotation.
            if (state.ball.pos.Length() < 200.0f && state.ball.vel.Length() < 150.0f)
                return;

            for (const auto& player : state.players) {
                if (player.isDemoed) continue;  // suspend, don't erase tracked state

                PlayerCycleState& st = states[player.carId];
                RoleType currentRole = DetermineRoleCommon(player, state);
                RoleType previousRole = st.lastRole;

                float pending = 0.0f;

                // Enter the cycle when leaving FIRST for another goalside role.
                if (previousRole == RoleType::FIRST && currentRole != RoleType::FIRST && currentRole != RoleType::TRANSITIONING) {
                    st.cycleStage = RoleCycleStage::AWAITING_RETREAT;
                    st.cycleStageTime = 0.0f;
                }

                if (currentRole == RoleType::TRANSITIONING) {
                    // The retreat/advance cycle doesn't apply while out of position.
                    st.cycleStage = RoleCycleStage::NONE;
                    st.cycleStageTime = 0.0f;
                }
                else if (st.cycleStage == RoleCycleStage::AWAITING_RETREAT) {
                    st.cycleStageTime += dt;

                    if (currentRole == RoleType::LAST) {
                        // Retreat leg complete.
                        pending += config.retreatReward;
                        s_retreatCompletions.fetch_add(1);

                        // Enter the advance leg regardless of whether a SECOND slot
                        // exists — 2v2 always advances LAST->FIRST directly.
                        st.secondWasReachableAtEntry = (CountGoalsideTeammates(player, state) >= 2);
                        st.sawSecondThisAdvance = false;
                        st.cycleStage = RoleCycleStage::AWAITING_ADVANCE;
                        st.cycleStageTime = 0.0f;
                    }
                    else if (currentRole == RoleType::FIRST) {
                        // New first-man opportunity — clear without payout.
                        st.cycleStage = RoleCycleStage::NONE;
                        st.cycleStageTime = 0.0f;
                    }
                    else if (st.cycleStageTime > config.retreatWindowSec) {
                        // Timed out without completing the retreat.
                        st.cycleStage = RoleCycleStage::NONE;
                        st.cycleStageTime = 0.0f;
                    }
                    // else: still SECOND (mid-retreat) — keep waiting.
                }
                else if (st.cycleStage == RoleCycleStage::AWAITING_ADVANCE) {
                    st.cycleStageTime += dt;

                    if (currentRole == RoleType::SECOND) {
                        // Waypoint visited — keep waiting for FIRST, don't resolve yet.
                        st.sawSecondThisAdvance = true;
                    }
                    else if (currentRole == RoleType::FIRST) {
                        // Advance leg complete.
                        if (st.sawSecondThisAdvance) {
                            pending += config.advanceRewardFull;
                            s_advanceCompletionsViaSecond.fetch_add(1);
                        } else if (!st.secondWasReachableAtEntry) {
                            pending += config.advanceRewardFull;
                            s_advanceCompletionsNoSlot.fetch_add(1);
                        } else {
                            pending += config.advanceRewardSkip;
                            s_advanceCompletionsSkipped.fetch_add(1);
                        }
                        st.cycleStage = RoleCycleStage::NONE;
                        st.cycleStageTime = 0.0f;
                    }
                    else if (st.cycleStageTime > config.advanceWindowSec) {
                        // Timed out without completing the advance.
                        st.cycleStage = RoleCycleStage::NONE;
                        st.cycleStageTime = 0.0f;
                    }
                    // else: still LAST — keep waiting.
                }

                st.lastRole = currentRole;

                if (pending != 0.0f) {
                    pendingRewards[player.carId] = pending;
                    s_rewardMilli.fetch_add(static_cast<long long>(pending * 1000.0f));
                }
            }
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;
            auto it = pendingRewards.find(player.carId);
            return (it != pendingRewards.end()) ? it->second : 0.0f;
        }
    };

} // namespace RLGC
