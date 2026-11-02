#pragma once

/*
 * File: InstantSpacingTransitionReward.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Fully discrete-transition companion to InstantSpacingReward (RotationRewards.h),
 *   which continuously rewards/penalizes teammate spacing every step, averaged across
 *   every teammate pair. This class instead pays a one-time bonus/penalty only on the
 *   step a teammate PAIR's spacing zone actually changes:
 *
 *     CLOSE --> GOOD : +entryReward  (pair just achieved good spacing)
 *     FAR   --> GOOD : +entryReward  (pair just achieved good spacing)
 *     GOOD  --> CLOSE: -entryReward  (pair just clustered too close)
 *     GOOD  --> FAR   : -entryReward  (pair just spread too far, when enableMaxSpacing)
 *     CLOSE <-> FAR   : NO PAYOUT — a direct jump that never confirmed GOOD in between
 *                        never "entered" or "left" GOOD, so neither event occurred.
 *                        Realistic trigger: a demo-respawn teleport. The confirmed
 *                        zone still silently updates so the next real transition is
 *                        evaluated correctly.
 *
 *   Zone classification is byte-for-byte identical to InstantSpacingReward's own
 *   branch logic (horizontal XY-only distance vs. RotationConfig::ForPhase(phase)'s
 *   minTeammateSpacing/maxTeammateSpacing/enableMaxSpacing), so the two rewards never
 *   disagree about which zone a given distance falls into. RotationConfig's other
 *   fields (role-cycling timers, back-post proximity, clusterPenaltyWeight) are not
 *   used here — this reward's payout is a flat entryReward, not severity-scaled.
 *
 *   Every unordered living, non-demoed teammate PAIR gets its own CLOSE/GOOD/FAR state
 *   machine (not per-player) — in 3v3 a player can be part of two pairs at once and be
 *   paid for both if both transition on the same step. A pair's very first sighting
 *   (episode start, or after Reset) initializes confirmed=pending=<current zone> with
 *   no payout — there's nothing to transition from yet.
 *
 *   A transition only fires after the new zone has held continuously for
 *   config.confirmHoldSec (default 0.2s — wider than DribbleCarryTransitionReward's
 *   0.1s, since teammate spacing changes more slowly than ball-in-a-box geometry, so a
 *   longer hold filters more boundary noise without gating out genuine repositioning).
 *
 *   Kickoff (ball parked at center, near-zero velocity — same gate as InstantSpacingReward)
 *   suspends ALL pair processing for the step, not just payout, so confirm-hold windows
 *   don't silently tick during kickoff-spawn proximity. A demoed player's pairs are
 *   likewise suspended (skipped) for the step; see Rev 1.1 below for what happens on
 *   respawn (this paragraph originally said the pre-demo baseline survives respawn —
 *   that turned out to be the wrong call and was fixed).
 *
 *   Unlike PossessionTransitionReward/DribbleCarryTransitionReward (both added earlier
 *   this session), this class is deliberately NOT ZeroSumReward-wrapped anywhere —
 *   matches InstantSpacingReward's own precedent: teammate spacing is a within-team
 *   coordination signal, not a competitive/zero-sum one.
 *
 *   PreStep computes all shared per-pair state once per step (not per-player) — same
 *   rationale as the other transition rewards this session: GetReward runs once per
 *   player and must not re-derive or re-trigger transitions per player.
 *
 *   See Reward_Change_Log.md 2026-08-27.
 *
 * Rev 1.1 (2026-08-30, NJH + Claude) — two fixes from a 6-scenario investigation
 * (ClaudeFiles/docs/InstantSpacingReward_vs_Transition_Investigation.md) done ahead
 * of retiring InstantSpacingReward everywhere in favor of this class:
 *
 *   1. BUG FIX — demo respawn used a stale pre-demo baseline. The "suspend, don't
 *      erase" convention (correct for continuous mechanics like flip resets, where
 *      pre/post-demo state is meaningfully related) was wrong here: a demo respawn
 *      point is essentially random relative to a surviving teammate's position, so
 *      comparing the post-respawn distance against a confirmedZone from possibly
 *      minutes earlier risked firing a spurious reward/penalty off pure spawn luck.
 *      A player's pairs now re-arm fresh (no payout) the step they respawn, exactly
 *      like a pair's first-ever sighting — tracked via a per-player wasDemoed map
 *      to detect the demoed->alive edge.
 *
 *   2. Sustained CLOSE/FAR trickle — the original discrete design paid only on zone
 *      ENTRY, so a pair that clustered once and then stayed clustered for the rest
 *      of the episode cost nothing further after the first frame. This is fine as a
 *      comparison sibling, but not safe as a full replacement: it has no ongoing
 *      cost for CAMPING in bad spacing, unlike InstantSpacingReward's continuous
 *      per-step penalty. Confirmed live at RewardConfig.cpp's Phase 5 Diamond site,
 *      where retiring the continuous reward at one experimental CHAMPION_PLUS site
 *      required temporarily bumping this class's weight to 50.0f just to make the
 *      one-shot event register — a symptom of the missing sustained cost, not a
 *      real fix for it (that site's weight is 6.0f as of Rev 1.2 — see
 *      RewardConfig.cpp:829; the 50.0f figure is historical only, don't trust a
 *      "50.0f" reference elsewhere without checking the actual argument). Added a
 *      flat per-step
 *      trickle (-rotConfig.clusterPenaltyWeight * config.sustainedPenaltyScale)
 *      applied every step a pair's CONFIRMED zone is CLOSE, or FAR when
 *      enableMaxSpacing, independent of whether a transition also fired that step.
 *      Deliberately flat, not severity-scaled like InstantSpacingReward's own
 *      distance-proportional penalty — one new variable at a time. Deliberately
 *      one-sided (no equivalent sustained bonus for confirmed GOOD) since staying
 *      well-spaced isn't a risk that needs disincentivizing the way camping in bad
 *      spacing is; the one-time entry bonus is left as the only positive signal.
 *
 * Rev 1.2 (2026-09-13, NJH + Claude) — two fixes from a scenario-test audit
 * (ClaudeFiles/RewardScenarioTests/InstantSpacingTransitionReward_2026-09-13.md):
 *
 *   1. BUG FIX — kickoff resume used a stale pre-kickoff pendingSince. gameTime
 *      advances every step regardless of the kickoff gate above (only pair
 *      processing is suspended, not the clock), so a pendingZone/pendingSince set
 *      just before a kickoff could satisfy confirmHoldSec purely from elapsed
 *      kickoff-parked wall-clock time on the very first live step after kickoff,
 *      if the post-kickoff spawn zone happened to match the stale pendingZone —
 *      not from genuine continuous holding. Same root cause as the Rev 1.1 demo-
 *      respawn bug (a discontinuous teleport invalidates the baseline it's being
 *      compared against), same fix: every pair re-arms fresh (no payout) on the
 *      first step after the kickoff gate releases, tracked via wasInKickoff
 *      detecting the kickoff->live edge.
 *
 *   2. FARMING GAP — the sustained trickle was gated on the debounced
 *      confirmedZone, so a policy could oscillate a pair's distance across the
 *      CLOSE/FAR boundary faster than confirmHoldSec (e.g. 0.15s each side) to
 *      keep resetting pendingSince before the 0.2s hold ever completed —
 *      confirmedZone then never updates from its initial GOOD, so NEITHER the
 *      transition event NOR the trickle ever fires, despite ~50% real dwell time
 *      in bad spacing. The trickle now gates on the raw per-step zone instead,
 *      so it tracks actual position every step (matching InstantSpacingReward's
 *      own always-continuous penalty) regardless of whether a transition has
 *      confirmed. The entry/exit event's confirm-hold noise filter is untouched.
 *
 * Author: Nathan + Claude
 * Date: August 2026 (Rev 1.2: September 2026)
 * Revision: 1.2
 */

#include "Reward.h"
#include "../Math.h"
#include "RotationRewards.h"
#include "RewardTiming.h"
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace RLGC {

    // ============================================================================
    // INSTANT SPACING TRANSITION REWARD
    // 2026-08-27 | Rev 1.0 | NJH
    // ============================================================================
    class InstantSpacingTransitionReward : public Reward {
    public:
        struct Config {
            float confirmHoldSec       = 0.20f;
            float entryReward          = 1.0f;
            float sustainedPenaltyScale = 0.1f; // Rev 1.1: per-step trickle while confirmed CLOSE/FAR, as a fraction of rotConfig.clusterPenaltyWeight
        };

        struct Metrics {
            int    goodEntries         = 0;
            int    closeEntries        = 0;
            int    farEntries          = 0;
            double rewardSum           = 0.0; // entry/exit transition events only
            double sustainedPenaltySum = 0.0; // Rev 1.1: ongoing CLOSE/FAR trickle
        };

    private:
        enum class SpacingZone { CLOSE, GOOD, FAR };

        struct PairState {
            SpacingZone confirmedZone = SpacingZone::GOOD;
            SpacingZone pendingZone   = SpacingZone::GOOD;
            float       pendingSince  = 0.0f;
            uint32_t    carIdA = 0;
            uint32_t    carIdB = 0;
        };

        struct FiredPairEvent {
            uint32_t carIdA;
            uint32_t carIdB;
            float    signedReward;
        };

        RotationConfig rotConfig;
        Config config;

        std::unordered_map<uint64_t, PairState> pairs;
        std::vector<FiredPairEvent> firedEvents;
        std::unordered_map<uint32_t, bool> wasDemoed; // Rev 1.1: detects the demoed->alive edge

        float gameTime = 0.0f;
        bool wasInKickoff = false; // Rev 1.2: detects the kickoff->live edge

        inline static std::atomic<int> s_goodEntries{ 0 };
        inline static std::atomic<int> s_closeEntries{ 0 };
        inline static std::atomic<int> s_farEntries{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };
        inline static std::atomic<long long> s_sustainedMilli{ 0 }; // Rev 1.1

        static uint64_t MakePairKey(uint32_t a, uint32_t b) {
            uint32_t lo = (a < b) ? a : b;
            uint32_t hi = (a < b) ? b : a;
            return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
        }

        // Byte-for-byte matches InstantSpacingReward's own branch logic
        // (RotationRewards.h:363-376) so the two rewards never disagree on zone.
        SpacingZone ClassifyDistance(float dist) const {
            if (dist < rotConfig.minTeammateSpacing) return SpacingZone::CLOSE;
            if (dist > rotConfig.maxTeammateSpacing && rotConfig.enableMaxSpacing) return SpacingZone::FAR;
            return SpacingZone::GOOD;
        }

    public:
        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.goodEntries = s_goodEntries.exchange(0);
            m.closeEntries = s_closeEntries.exchange(0);
            m.farEntries = s_farEntries.exchange(0);
            m.rewardSum = double(s_rewardMilli.exchange(0)) / 1000.0;
            m.sustainedPenaltySum = double(s_sustainedMilli.exchange(0)) / 1000.0;
            return m;
        }

        InstantSpacingTransitionReward(RotationPhase phase)
            : rotConfig(RotationConfig::ForPhase(phase)), config() {}
        InstantSpacingTransitionReward(RotationPhase phase, const Config& cfg)
            : rotConfig(RotationConfig::ForPhase(phase)), config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            pairs.clear();
            firedEvents.clear();
            wasDemoed.clear();
            gameTime = 0.0f;
            wasInKickoff = false;
        }

        // PreStep runs exactly once per step — all pair-transition detection lives
        // here. GetReward runs once per player and must not re-derive or re-trigger
        // transitions per player (a single pair transition can pay two players).
        virtual void PreStep(const GameState& state) override {
            gameTime += StepDt(state);
            firedEvents.clear();

            // Rev 1.1: detect the demoed->alive edge for every player, even during
            // the kickoff gate below — cheap, and correct if a respawn ever straddles
            // the kickoff-parked window (implausible in real play, not worth assuming).
            std::unordered_map<uint32_t, bool> justRespawned;
            for (const auto& p : state.players) {
                bool prevDemoed = wasDemoed.count(p.carId) ? wasDemoed[p.carId] : false;
                justRespawned[p.carId] = (prevDemoed && !p.isDemoed);
                wasDemoed[p.carId] = p.isDemoed;
            }

            // Kickoff gate: ball parked at center, near-zero velocity — suspends ALL
            // pair processing this step, matching InstantSpacingReward's own gate but
            // scoped to detection, not just payout.
            if (state.ball.pos.Length() < 200.0f && state.ball.vel.Length() < 150.0f) {
                wasInKickoff = true;
                return;
            }

            // Rev 1.2 BUG FIX — gameTime keeps advancing through the entire
            // kickoff-parked window above (pair processing is skipped, but the
            // clock isn't), so a pendingSince set just before kickoff could
            // satisfy confirmHoldSec purely from elapsed kickoff-parked time on
            // the very first live step after kickoff, rather than from genuine
            // continuous holding — and the post-kickoff spawn position is just
            // as unrelated to that stale baseline as a demo respawn is. Same
            // root cause as the Rev 1.1 demo-respawn bug, same fix: re-arm every
            // pair fresh (no payout) on the first step after kickoff releases.
            bool justLeftKickoff = wasInKickoff;
            wasInKickoff = false;

            for (int teamIdx = 0; teamIdx < 2; ++teamIdx) {
                Team team = (teamIdx == 0) ? Team::BLUE : Team::ORANGE;

                std::vector<const Player*> teammates;
                for (const auto& p : state.players) {
                    if (p.team != team) continue;
                    if (p.isDemoed) continue;
                    teammates.push_back(&p);
                }

                for (size_t i = 0; i < teammates.size(); ++i) {
                    for (size_t j = i + 1; j < teammates.size(); ++j) {
                        const Player* pa = teammates[i];
                        const Player* pb = teammates[j];

                        Vec a = pa->pos; a.z = 0.0f;
                        Vec b = pb->pos; b.z = 0.0f;
                        float dist = (a - b).Length();

                        SpacingZone z = ClassifyDistance(dist);
                        uint64_t key = MakePairKey(pa->carId, pb->carId);

                        auto it = pairs.find(key);
                        bool freshStart = (it == pairs.end()) || justRespawned[pa->carId] || justRespawned[pb->carId] || justLeftKickoff;

                        PairState* ps;
                        if (freshStart) {
                            // First-ever sighting, one side just respawned (Rev 1.1), OR
                            // this is the first live step after a kickoff (Rev 1.2): any
                            // existing baseline reflects a positioning relationship that
                            // either never existed yet or no longer means anything — a
                            // demo respawn point (or a kickoff-formation spawn point) is
                            // essentially random relative to a stale pre-event baseline,
                            // so comparing against it risked firing a spurious
                            // reward/penalty off pure spawn luck. Re-arm fresh with no
                            // transition payout either way.
                            PairState fresh;
                            fresh.confirmedZone = z;
                            fresh.pendingZone = z;
                            fresh.pendingSince = gameTime;
                            fresh.carIdA = pa->carId;
                            fresh.carIdB = pb->carId;
                            auto res = pairs.insert_or_assign(key, fresh);
                            ps = &res.first->second;
                        } else {
                            ps = &it->second;

                            if (z != ps->pendingZone) {
                                ps->pendingZone = z;
                                ps->pendingSince = gameTime;
                            }

                            bool held = (gameTime - ps->pendingSince) >= config.confirmHoldSec;
                            if (held && ps->pendingZone != ps->confirmedZone) {
                                bool enteringGood = (ps->confirmedZone != SpacingZone::GOOD && ps->pendingZone == SpacingZone::GOOD);
                                bool leavingGood  = (ps->confirmedZone == SpacingZone::GOOD && ps->pendingZone != SpacingZone::GOOD);

                                if (enteringGood) {
                                    s_goodEntries.fetch_add(1);
                                    float r = config.entryReward;
                                    s_rewardMilli.fetch_add(static_cast<long long>(r * 1000.0f));
                                    firedEvents.push_back({ ps->carIdA, ps->carIdB, r });
                                } else if (leavingGood) {
                                    if (ps->pendingZone == SpacingZone::CLOSE) s_closeEntries.fetch_add(1);
                                    else                                      s_farEntries.fetch_add(1);
                                    float r = -config.entryReward;
                                    s_rewardMilli.fetch_add(static_cast<long long>(r * 1000.0f));
                                    firedEvents.push_back({ ps->carIdA, ps->carIdB, r });
                                }
                                // else: direct CLOSE<->FAR jump — no transition payout, silent update.

                                ps->confirmedZone = ps->pendingZone;
                            }
                        }

                        // Rev 1.1: sustained-state trickle. Applies every step the
                        // pair's spacing is CLOSE (or FAR, when enableMaxSpacing),
                        // independent of whether a transition also fired this step —
                        // closes the "pay once, then camp in bad spacing for free" gap
                        // a pure entry/exit design has. Flat per-step value, no
                        // deltaTime scaling, matching InstantSpacingReward's convention.
                        //
                        // Rev 1.2 FIX: gated on the raw per-step zone `z`, not the
                        // debounced `confirmedZone`. Gating on confirmedZone let a
                        // policy oscillate across a boundary faster than
                        // confirmHoldSec to freeze confirmedZone at GOOD forever —
                        // pendingZone/pendingSince keep resetting before the 0.2s hold
                        // ever completes, so confirmedZone never updates and the
                        // trickle never fires, even with ~50% real dwell time in bad
                        // spacing. Using the instantaneous zone closes that gap (the
                        // trickle now tracks real position every step, matching how
                        // InstantSpacingReward's own continuous penalty always worked)
                        // while leaving the confirm-hold noise filter untouched for
                        // the one-time entry/exit event above, which still needs it.
                        if (z == SpacingZone::CLOSE ||
                            (z == SpacingZone::FAR && rotConfig.enableMaxSpacing)) {
                            float trickle = -rotConfig.clusterPenaltyWeight * config.sustainedPenaltyScale;
                            s_sustainedMilli.fetch_add(static_cast<long long>(trickle * 1000.0f));
                            firedEvents.push_back({ ps->carIdA, ps->carIdB, trickle });
                        }
                    }
                }
            }
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            float total = 0.0f;
            for (const auto& ev : firedEvents) {
                if (ev.carIdA == player.carId || ev.carIdB == player.carId)
                    total += ev.signedReward;
            }
            return total;
        }
    };

} // namespace RLGC
