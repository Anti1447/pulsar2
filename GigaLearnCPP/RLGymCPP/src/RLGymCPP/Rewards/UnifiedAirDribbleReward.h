#pragma once
#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include "RewardMath.h"
#include "RewardTiming.h"
#include "BallPredCache.h"
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

// Ported from RLGymBot_v3 (CompleteMechanicsRewards.h: UnifiedAirDribbleReward; BallPred::Cache -> BallPredCache (engine prediction, 2.0s horizon)). Logic unchanged unless noted.

namespace RLGC {

// ============================================================================
// UnifiedAirDribbleReward (2025-02-17)
// Originally three air dribble rewards (offensive, defensive, general) behind one mode switch; ONLY the offensive mode is implemented
// here (the defensive/general evaluators were never ported). Notes below about Defensive/General describe the v3 original.
//
// Rev 6.0 (2026-11-07, audit): see the AUDIT FIXES block below. A lone jump-hit used to pay (~22-41 at weight 40) because the first
// underside touch opened a sequence AND paid the entry bonus; it now needs a second roof-side touch before anything pays.
//
// Rev 3.0 changes (2026-02-24):
//  - Added BallPredCache _ballCache + PreStep() override
//  - Per-touch trajectory quality multiplier (0.7x–1.5x) applied in all three
//    Evaluate functions based on predicted ball position at 0.75s ahead.
//    Offensive/General: reward * (1 + clamp((curDist - futureDist)/1200, -0.3, 0.5))
//    Defensive: reward * (1 + clamp((futureDist - curDist)/1200, -0.3, 0.5))
//    where dist is to opponent goal (off/gen) or own goal (def).
//
// Rev 5.0 changes (2026-08-01):
//  - Added behindMul: a horizontal contact-alignment multiplier (0.8x-1.2x)
//    scoped to the first two touches of a sequence (seq.touchCount <= 1).
//    Rewards contacting the ball on the side opposite the intended travel
//    direction (i.e. behind it, not just underneath), matching real-world
//    air dribble technique for converting height into forward distance on
//    the pop touch. Applied alongside undersideMul in GetReward().
//
// AUDIT FIXES (Rev 6.0, 2026-11-07; probe: one aerial pop paid 0.55-1.04 raw, 22-41 at weight 40, goal = 150):
//  1. Nothing pays (nor releases the travel bonus) until the SECOND touch of a sequence (MIN_TOUCHES_TO_PAY). The opening touch pays 0.
//  2. A touch only counts when it lands on the ROOF side: (ball - car) . car.up >= ROOF_CONTACT_MIN. A wheel/underside bump from below
//     (the flip-reset pose) neither opens a sequence nor counts as a touch.
//  3. The opponent-touch end trigger now looks at EVERY toucher this step (it used to `break` after the first one in player order, so a
//     teammate or the owner listed first hid a simultaneous opponent touch).
//  4. Weight: raw per-touch max is 4.5 and the sequence cap 65, so a full sequence is worth 65 x weight. The commented-out BuildRewards entry
//     is re-derived to 2 (65 x 2 = 130, about one goal). Do not use 40.
// ============================================================================
class UnifiedAirDribbleReward : public Reward {
private:
    // Unified per-sequence state
    struct SequenceState {
        float accumReward = 0.0f;
        float timeElapsed = 0.0f;
        int touchCount = 0;
        bool exhausted = false;
        int touchesSeen = 0;                  // roof-side touches in this sequence, including the opening one; nothing pays below MIN_TOUCHES_TO_PAY
        float startHeight = 0.0f;
        bool firstTouchProcessed = false;
        float boostAtStart     = -1.0f;  // boost level when sequence opened (-1 = unset)
        Vec   playerPosAtStart = {};     // player position when sequence opened

        // Velocity tracking (distance-dependent requirements)
        float initialBallVelocity = 0.0f;       // Ball speed when sequence started
        float distanceToOpponentGoal = 0.0f;    // Distance from start position to opponent goal

        // Ball travel integral since last touch (height × distance; adapted from BallTravelReward)
        float travelIntegralAccum = 0.0f;

        // Rev 4.0 — outcome-gated resolution
        bool  endedByOpponentTouch = false;   // set in PreStep, consumed+resolved in the owner's next GetReward
        bool  scoringTouchPaid     = false;   // one-shot latch, guards the goal-mouth resolution path
        float bestOutplayMul       = 1.0f;    // max ComputeOutplayMultiplier() seen this sequence
    };

    mutable std::unordered_map<uint32_t, SequenceState> sequenceStates;
    float minBallHeight = 250.0f;

    // Consolidated metrics
    mutable std::atomic<uint64_t> offensiveAttempts{ 0 };
    mutable std::atomic<uint64_t> offensiveCompletions{ 0 };

    const float EPS_REWARD_SKIP = 1e-7f;

    BallPredCache _ballCache;  // Updated once per step in PreStep(); read in EvaluateOffensive()/IsHeadingToGoalMouth()
    mutable Vec  _prevBallPos       = {};     // For ball travel integral (accumulated in PreStep)
    mutable bool _prevBallPosValid  = false;

    // ========================================================================
    // PRE-FLIP SETUP MECHANIC (Rev 4.0) — a gentle, diagonal flip touch off a
    // wall/backboard jump on a slow, distant, airborne ball, used to set up an
    // air dribble from range instead of the close-range underside-contact path.
    // ========================================================================
    mutable std::unordered_map<uint32_t, bool> _wasFlipping;        // flip-start edge tracking
    mutable std::unordered_map<uint32_t, bool> _lastJumpFromWall;   // true if the current airborne excursion began with a jump off a wall/backboard

    struct PendingPreFlip {
        float timeRemaining = 0.0f;
    };
    mutable std::unordered_map<uint32_t, PendingPreFlip> pendingPreFlip;

    const float PREFLIP_MAX_BALL_SPEED = 1400.0f;  // "slow" — roughly a car's unboosted top speed
    const float PREFLIP_MIN_GOAL_DIST  = 5100.0f;  // must be far from the net — this is a setup, not a finish
    const float PREFLIP_MIN_HIT_FORCE  = 200.0f;   // floor: touch must actually redirect the ball
    const float PREFLIP_MAX_HIT_FORCE  = 800.0f;   // ceiling: well under ROUGH_TOUCH_THRESHOLD=1200, stays "gentle"
    const float PREFLIP_WINDOW_SECONDS = 0.5f;     // follow-through window length
    const float PREFLIP_VEL_ALIGN_DOT  = 0.5f;     // cos(60°) — generous chase-angle tolerance
    const float PREFLIP_SETUP_BONUS    = 0.35f;    // < ENTRY_BONUS (0.45) — separate skill, separate payout
    const float WALL_UP_Z_THRESHOLD    = 0.2f;     // matches WallDashInputReward's UP_Z_THRESHOLD

    // ========================================================================
    // SEQUENCE GATE + RESOLUTION (Rev 4.0) — continuous angle taper (no hard
    // cliff, no time-based grace), and outcome-gated travel-bonus release.
    // ========================================================================
    const float HARD_FAIL_UNDERSIDE_Z = 0.00f;   // ~90° — only truly bad angles end the sequence
    const float IDEAL_UNDERSIDE_Z     = 0.30f;   // old 70° line — now just "full quality," not pass/fail
    const float ROOF_CONTACT_MIN      = 0.3f;     // (ball - car) . car.up: the ball must sit on the roof side for a touch to count
    const int   MIN_TOUCHES_TO_PAY    = 2;        // a lone pop is not an air dribble
    const float MAX_CONTROL_DIST      = 1100.0f;  // UU — beyond this, no plausible dribble contact remains
    const float SCORING_TOUCH_BONUS   = 3.5f;    // flat bonus when a cap-exhausted sequence's scoring touch is bypassed through

    // ========================================================================
    // BEHIND-BALL HORIZONTAL ALIGNMENT (Rev 5.0) — first-two-touches-only
    // refinement on top of undersideMul: also reward contacting the ball on
    // the side opposite the intended travel direction (behind, not just under).
    // ========================================================================
    const float BEHIND_ALIGN_TOLERANCE = 0.25f;  // dot-product slack before behind-quality starts climbing
    const float BEHIND_MUL_MIN         = 0.8f;  // floor: car in front of / orthogonal to travel direction
    const float BEHIND_MUL_MAX         = 1.2f;  // ceiling: contact directly behind, opposite travel direction

    // ========================================================================
    // VELOCITY REQUIREMENT HELPERS (NEW - Distance-dependent)
    // ========================================================================

    float GetDistanceToOpponentGoal(const Player& player, const GameState& state) const {
        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        return (player.pos - oppGoal).Length();
    }

    float GetMinVelocityForDistance(float distanceToGoal) const {
        // Distance-based minimum velocity requirements (scaled for 10240 UU field)
        if (distanceToGoal >= 9000.0f) return 1800.0f;      // Very far (own corner ~9000+ UU)
        if (distanceToGoal >= 7000.0f) return 1700.0f;      // Far
        if (distanceToGoal >= 5000.0f) return 1500.0f;      // Medium
        if (distanceToGoal >= 3000.0f) return 1200.0f;      // Close
        return 0.0f;                                         // Very close - no minimum
    }

    float GetMaxBonusVelocity(float distanceToGoal) const {
        // Optimal velocity ranges for bonuses (scaled for 10240 UU field)
        if (distanceToGoal >= 9000.0f) return 2300.0f;
        if (distanceToGoal >= 7000.0f) return 2200.0f;
        if (distanceToGoal >= 5000.0f) return 2000.0f;
        if (distanceToGoal >= 3000.0f) return 1800.0f;
        return 1500.0f;
    }

    float GetInitialVelocityBonus(float ballVelocity, float distanceToGoal) const {
        // Higher bonus for faster starts when far from goal
        float minVelocity = GetMinVelocityForDistance(distanceToGoal);

        if (ballVelocity < minVelocity && minVelocity > 0.0f) {
            // Penalize slow starts from far away
            float penalty = RS_CLAMP(ballVelocity / minVelocity, 0.5f, 1.0f);
            return penalty;
        }

        // Bonus for exceeding minimum (up to 1.2x)
        float maxVelocity = GetMaxBonusVelocity(distanceToGoal);
        float bonus = RS_CLAMP((ballVelocity - minVelocity) / (maxVelocity - minVelocity), 0.0f, 0.2f);
        return 1.0f + bonus;  // Range [1.0, 1.2x]
    }

    // ========================================================================
    // OUTPLAY / BOOST-CONSERVATION HELPERS
    // ========================================================================

    // Returns a multiplier based on whether the ball is moving past an actively-challenging
    // opponent.  1.0x = no challenger (or ball heading into them), 1.15x = 50/50 contest,
    // 1.45x = ball clearly redirected past them (outplay).
    float ComputeOutplayMultiplier(const Player& player, const GameState& state) const {
        constexpr float CHALLENGE_DIST      = 900.0f;   // UU from ball — "in a challenge"
        constexpr float MIN_VEL_TOWARD_BALL = 250.0f;   // min approach speed to count
        constexpr float OUTPLAY_BONUS       = 1.45f;    // ball going past challenger
        constexpr float CONTEST_BONUS       = 1.15f;    // 50/50 or sideways redirect

        float bestDist = 1e9f;
        const Player* challenger = nullptr;
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            // 2026-09-13 (NJH): a demoed car's pos/vel are frozen at their pre-demo
            // values for the full 3s DEMO_RESPAWN_TIME (Car::Demolish never resets
            // them, rigidbody sim is disabled until Respawn()) — without this check
            // a defender removed by a bump still counted as an actively-closing
            // challenger for the entire respawn window. See
            // ClaudeFiles/RewardScenarioTests/UnifiedAirDribbleReward_2026-09-13.md.
            if (opp.isDemoed) continue;
            float distToBall = (opp.pos - state.ball.pos).Length();
            if (distToBall >= CHALLENGE_DIST) continue;
            Vec oppToBall = (state.ball.pos - opp.pos).Normalized();
            float velTowardBall = RS_MAX(0.0f, opp.vel.Dot(oppToBall));
            if (velTowardBall < MIN_VEL_TOWARD_BALL) continue;
            if (distToBall < bestDist) {
                bestDist = distToBall;
                challenger = &opp;
            }
        }

        if (!challenger) return 1.0f;  // nobody actively challenging

        // awayScore: +1 = ball moving directly away from challenger (outplay),
        //            -1 = ball heading straight into challenger
        Vec ballToChallenger = (challenger->pos - state.ball.pos).Normalized();
        float ballLen = state.ball.vel.Length();
        float awayScore = (ballLen > 1.0f)
            ? -(state.ball.vel * (1.0f / ballLen)).Dot(ballToChallenger)
            : 0.0f;

        if (awayScore >  0.3f) return OUTPLAY_BONUS;
        if (awayScore > -0.3f) return CONTEST_BONUS;
        return 1.0f;  // heading into challenger, no bonus
    }

    // Returns true if BallPred shows the ball crossing the offensive goal plane
    // through the actual goal opening (accounting for ball radius).
    // Used to waive the boost-conservation penalty when committing all boost is correct.
    bool IsHeadingToGoalMouth(const Player& player) const {
        if (!_ballCache.valid) return false;
        constexpr float BALL_R      = 91.25f;
        constexpr float MOUTH_W     = 892.755f - BALL_R;   // 801.505 UU half-width
        // 2026-08-28 (NJH): lowered 200 UU (was 642.775-BALL_R=551.525, right under the
        // crossbar) — bot was consistently air-dribbling into the crossbar/just over the
        // goal line because trajectories aimed that high were already fully credited as
        // "goal-bound" by this check. 351.525 now requires aiming meaningfully lower for
        // max ComputeOutcomeScore()/boost-conservation-waiver credit.
        constexpr float MOUTH_H_MAX = 642.775f - BALL_R - 250.0f;   // 301.525 UU top
        // 2026-09-19 (NJH): BallPredCache has no goal opening — it reflects the ball off
        // the back wall at y = +/-(BACK_WALL_Y - BALL_RADIUS) ~= +/-5027 everywhere, so the
        // old `sl.pos.y >= BACK_WALL_Y` test never fired and this returned false for every
        // trajectory. The first slice with hitBackboard set (sticky flag) is the goal-plane
        // ARRIVAL; judge x/z there. Reject if it bounced on the ground first or the arrival
        // is at the wrong end. Mirrors AirDribbleBumpConversionReward::ValidateGoalBoundTrajectory.
        const float goalSign = (player.team == Team::BLUE) ? 1.0f : -1.0f;
        for (int i = 0; i < _ballCache.numSlices; i++) {
            const auto& sl = _ballCache.slices[i];
            if (sl.hitGround) return false;   // bounced before reaching the goal plane
            if (!sl.hitBackboard) continue;   // hasn't reached a back wall yet
            if (sl.pos.y * goalSign <= 0.0f) return false;  // own-end back wall
            return fabsf(sl.pos.x) < MOUTH_W &&
                   sl.pos.z > BALL_R          &&
                   sl.pos.z < MOUTH_H_MAX;
        }
        return false;
    }

    // ========================================================================
    // SEQUENCE RESOLUTION HELPERS (Rev 4.0)
    // Graduated outcome scoring — not all good air dribbles score a goal, so
    // the travel-integral bonus is released scaled by how the sequence ended:
    // net progress toward the opponent's goal as the baseline, maxed out by a
    // goal-mouth-bound trajectory, floored by having beaten a defender.
    // ========================================================================

    // True when the ball is on the car's roof side (fix 2). Wheels/underside contact from below does not count.
    bool RoofContact(const Player& player, const GameState& state) const {
        Vec toBall = state.ball.pos - player.pos;
        float d = toBall.Length();
        if (d < 1e-3f) return false;
        return player.rotMat.up.Dot(toBall * (1.0f / d)) >= ROOF_CONTACT_MIN;
    }

    float ComputeOutcomeScore(const Player& player, const GameState& state, const SequenceState& seq) const {
        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        float endDist = (state.ball.pos - oppGoal).Length();
        float netProgress = RS_MAX(0.0f, seq.distanceToOpponentGoal - endDist);

        constexpr float PROGRESS_SCALE_FOR_OUTCOME = 3500.0f;  // UU of net progress for a "great" baseline (1.0)
        float outcomeScore = RS_CLAMP(netProgress / PROGRESS_SCALE_FOR_OUTCOME, 0.0f, 1.0f);

        if (IsHeadingToGoalMouth(player))
            outcomeScore = 1.0f;                       // goal-bound trajectory always maxes the score
        if (seq.bestOutplayMul >= 1.45f)                // matches OUTPLAY_BONUS in ComputeOutplayMultiplier
            outcomeScore = RS_MAX(outcomeScore, 0.6f);  // beating a defender floors the score at 0.6

        return outcomeScore;
    }

    // Releases the banked ball-travel-distance bonus, scaled by outcome quality, plus
    // (only when bypassing an already-exhausted cap on the scoring touch) a flat bonus.
    // Called from every sequence-ending path so outcome scoring is computed identically
    // regardless of whether it ended via opponent touch, disconnect, or a scoring touch.
    float ResolveSequence(const Player& player, const GameState& state, SequenceState& seq, bool wasCapBypass) const {
        if (seq.touchesSeen < MIN_TOUCHES_TO_PAY) {   // fix 1: an unconfirmed sequence (one pop) releases nothing
            seq.travelIntegralAccum = 0.0f;
            return 0.0f;
        }
        float outcomeScore = ComputeOutcomeScore(player, state, seq);
        constexpr float TRAVEL_INTEGRAL_NORM = 1.91e-7f;
        constexpr float TRAVEL_BONUS_SCALE   = 8.0f;
        constexpr float TRAVEL_BONUS_MAX     = 2.0f;
        float travelBonus = RS_CLAMP(
            seq.travelIntegralAccum * TRAVEL_INTEGRAL_NORM * TRAVEL_BONUS_SCALE,
            0.0f, TRAVEL_BONUS_MAX);
        seq.travelIntegralAccum = 0.0f;

        float released = travelBonus * outcomeScore + (wasCapBypass ? SCORING_TOUCH_BONUS : 0.0f);
        seq.accumReward += released;
        return released;
    }

    // ========================================================================
    // HORIZONTAL VELOCITY CAP HELPER
    // ========================================================================

    // Soft penalty when the ball's horizontal (XY) speed exceeds 2300 uu/s —
    // at that speed the car can't keep up and the air dribble collapses.
    // Waived when the ball is within 3000 UU of the opposing goal (hard shot intent).
    float GetHorizVelPenaltyMul(const Player& player, const GameState& state) const {
        constexpr float HORIZ_VEL_LIMIT = 2300.0f;  // penalty ramp starts here
        constexpr float HORIZ_VEL_MAX   = 3000.0f;  // 0.5x floor at this speed
        constexpr float NEAR_GOAL_DIST  = 3000.0f;  // uu from goal_back — waive penalty
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

    // ========================================================================
    // OFFENSIVE MODE (based on AirDribbleAccuracyReward)
    // ========================================================================

    float EvaluateOffensive(const Player& player, const GameState& state, SequenceState& seq) const {
        // Constants for offensive mode
        // Rev 2.1: reduced per-touch reward, increased duration/touch limits
        // to encourage sustained multi-touch sequences over single-touch spikes.
        const float PROGRESS_SCALE = 0.007f;      // was 0.0105 — 33% less per unit of progress
        const float PER_TOUCH_MAX = 4.5f;         // was 7.0 — lower per-touch ceiling
        const float SEQ_REWARD_CAP = 65.0f;       // unchanged — same total ceiling, just more touches to reach it
        const float MAX_SEQ_DURATION = 18.0f;     // was 13.0 — 5s longer
        const int MAX_TOUCHES = 65;               // was 40 — 25 more touches allowed
        const float MIN_PROGRESS_UU = 8.0f;       // was 12.0 — softer touches qualify
        const float DECAY_EXPONENT = 0.35f;       // was 0.5 — slower decay, later touches still pay
        const float ENTRY_BONUS = 0.45f;          // was 0.65 — reduced to match lower per-touch scale
        const float SECOND_TOUCH_BONUS = 0.65f;   // was 1.0 — reduced to match
        const float MIN_DIR_MULTIPLIER = 0.20f;  // tightened: stronger penalty for off-goal touches
        const float GOOD_DIR_THRESHOLD = 0.65f;  // tightened: cos⁻¹(0.65)≈49° required for top tier
        const float ROUGH_TOUCH_THRESHOLD = 1200.0f;
        const float ROUGH_TOUCH_PENALTY = 0.7f;
        const float SCORING_ZONE_MIN = 250.0f;
        const float SCORING_ZONE_MAX = 630.0f;
        const float SCORING_ZONE_BONUS = 1.3f;
        const float MAX_BALL_Z_FOR_GOAL = 640.0f;
        const float HEIGHT_CONSTRAINT_PENALTY = 0.5f;

        offensiveAttempts.fetch_add(1);

        // Check sequence constraints
        if (seq.timeElapsed > MAX_SEQ_DURATION || seq.touchCount >= MAX_TOUCHES) {
            seq.exhausted = true;
            return 0.0f;
        }

        // Calculate progress toward opponent goal
        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        float prevDist = (state.prev->ball.pos - oppGoal).Length();
        float curDist = (state.ball.pos - oppGoal).Length();
        float progress = RS_MAX(0.0f, prevDist - curDist);

        if (progress < MIN_PROGRESS_UU) {
            return 0.0f;
        }

        // Base reward
        float perTouch = RS_MIN(PER_TOUCH_MAX, progress * PROGRESS_SCALE);
        float usedFraction = RS_CLAMP(seq.accumReward / SEQ_REWARD_CAP, 0.0f, 1.0f);
        float decay = powf(1.0f - usedFraction, DECAY_EXPONENT);
        float remainingCap = RS_MAX(0.0f, SEQ_REWARD_CAP - seq.accumReward);
        float rewardBeforeDir = RS_MIN(perTouch * decay, remainingCap);

        // Directional multiplier — aim at goal mouth center (Z≈321), not the back floor,
        // so high-arc dribbles are correctly aligned toward the actual opening.
        constexpr float GOAL_MOUTH_Z = 321.4f;  // 642.775 / 2
        Vec oppGoalCenter = (player.team == Team::BLUE)
            ? Vec(0.0f,  CommonValues::BACK_WALL_Y, GOAL_MOUTH_Z)
            : Vec(0.0f, -CommonValues::BACK_WALL_Y, GOAL_MOUTH_Z);
        Vec ballToGoal = (oppGoalCenter - state.ball.pos).Normalized();
        Vec ballVelDir = state.ball.vel.Normalized();
        float alignment = ballVelDir.Dot(ballToGoal);
        float dirMul = (alignment < 0.0f) ? MIN_DIR_MULTIPLIER * 0.4f :
                       (alignment < GOOD_DIR_THRESHOLD) ?
                       MIN_DIR_MULTIPLIER + (1.0f - MIN_DIR_MULTIPLIER) * (alignment / GOOD_DIR_THRESHOLD) * 0.8f :
                       0.8f + 0.2f * ((alignment - GOOD_DIR_THRESHOLD) / (1.0f - GOOD_DIR_THRESHOLD));

        float reward = rewardBeforeDir * dirMul;

        // Entry/second-touch bonuses (touchCount = qualifying touches rewarded so far;
        // consecutiveAirTouches was already incremented before Evaluate* is called, so
        // checking it against 0 never fired — touchCount is the correct gate here)
        if (seq.touchCount == 0) {
            reward += ENTRY_BONUS * RS_MIN(1.0f, dirMul);
        } else if (seq.touchCount == 1) {
            reward += SECOND_TOUCH_BONUS * RS_MIN(1.0f, dirMul);
        }

        // Height penalties in finishing zone
        if (state.ball.pos.z > MAX_BALL_Z_FOR_GOAL) {
            reward *= HEIGHT_CONSTRAINT_PENALTY;
        }

        // Scoring zone bonus
        if (state.ball.pos.z >= SCORING_ZONE_MIN && state.ball.pos.z <= SCORING_ZONE_MAX) {
            reward *= SCORING_ZONE_BONUS;
        }

        // First touch quality check
        if (!seq.firstTouchProcessed && state.prev) {
            float prevSpeed = state.prev->ball.vel.Length();
            float currSpeed = state.ball.vel.Length();
            float velocityChange = currSpeed - prevSpeed;
            if (velocityChange > ROUGH_TOUCH_THRESHOLD) {
                reward *= ROUGH_TOUCH_PENALTY;
            }
            seq.firstTouchProcessed = true;
        }

        // Trajectory quality multiplier: boost touches that predict ball approaching goal.
        // Combines Y-approach (Euclidean distance reduction) with X-centering (converging
        // toward goal centerline). 0.7x drifting away/wide, 1.0x neutral, up to 1.5x on target.
        {
            const float TRAJ_CHECK_TIME = 0.75f;    // seconds ahead to sample
            const float TRAJ_SCALE      = 1200.0f;  // UU of approach for full factor
            const float TRAJ_MIN_FACTOR = -0.3f;
            const float TRAJ_MAX_FACTOR =  0.5f;
            if (_ballCache.valid) {
                Vec futurePos    = _ballCache.AtTime(TRAJ_CHECK_TIME).pos;
                float futureDist = (futurePos - oppGoal).Length();
                float improvement = curDist - futureDist;  // positive = ball moving toward goal
                float trajFactor = RS_CLAMP(improvement / TRAJ_SCALE, TRAJ_MIN_FACTOR, TRAJ_MAX_FACTOR);

                // X-centering: reward trajectory converging toward goal center (X=0),
                // penalize drifting toward the corner. Capped at ±0.1 to not dominate.
                float curLateral    = fabsf(state.ball.pos.x);
                float futureLateral = fabsf(futurePos.x);
                float lateralFactor = RS_CLAMP((curLateral - futureLateral) / 1000.0f, -0.1f, 0.1f);
                trajFactor = RS_CLAMP(trajFactor + lateralFactor, TRAJ_MIN_FACTOR, TRAJ_MAX_FACTOR);

                reward *= (1.0f + trajFactor);
            }
        }

        // (Boost efficiency gate removed — boostAtStart was set at first-touch frame,
        //  making boostConsumed = 0 on touch 1, so the gate always killed the first touch.
        //  Quality is already filtered by directional multiplier + BallPred trajectory.)

        // Outplay multiplier: when an opponent is actively challenging, reward redirecting
        // the ball past them. Neutral (1.0x) when no challenger is present. Tracks the best
        // multiplier seen this sequence for ComputeOutcomeScore's outplay-based outcome floor.
        float outplayMul = ComputeOutplayMultiplier(player, state);
        seq.bestOutplayMul = RS_MAX(seq.bestOutplayMul, outplayMul);
        reward *= outplayMul;

        // Boost conservation: soft penalty for holding fewer than 20 boost (bot needs it for
        // recovery after the dribble ends). Waived if trajectory is already heading into the goal.
        if (player.boost < 20.0f && !IsHeadingToGoalMouth(player)) {
            float boostFactor = RS_CLAMP(player.boost / 20.0f, 0.5f, 1.0f);
            reward *= boostFactor;
        }

        // Goal mouth bonus: if BallPred predicts the ball crossing the goal plane through the
        // actual opening, apply a big bonus. Reuses IsHeadingToGoalMouth() (also used by
        // GetReward()'s scoring-touch resolution) instead of duplicating the BallPred slice scan.
        if (IsHeadingToGoalMouth(player)) {
            constexpr float GOAL_BONUS = 3.0f;   // was 1.5f — strong on-target signal
            reward *= GOAL_BONUS;
        }

        reward *= GetInitialVelocityBonus(seq.initialBallVelocity, seq.distanceToOpponentGoal);

        if (reward > EPS_REWARD_SKIP) {
            seq.accumReward += reward;
            seq.touchCount++;
            offensiveCompletions.fetch_add(1);
            if (seq.accumReward >= SEQ_REWARD_CAP - 1e-3f) {
                seq.exhausted = true;
            }
            return reward;
        }
        return 0.0f;
    }

public:
    virtual void PreStep(const GameState& state) override {
        _ballCache.Update(state);

        // Opponent-touch end-trigger (Rev 4.0; fix 3, Rev 6.0): flag any active sequence whose owner is on the OTHER team of ANY car
        // that touched the ball this step. Resolved (payout + erase) in the owner's next GetReward call, since PreStep can't return a reward.
        for (auto& [ownerId, seq] : sequenceStates) {
            const Player* owner = nullptr;
            for (const auto& p : state.players)
                if (p.carId == ownerId) { owner = &p; break; }
            if (!owner) continue;
            for (const auto& toucher : state.players) {
                if (!toucher.ballTouchedStep || toucher.carId == ownerId) continue;
                if (toucher.team != owner->team) { seq.endedByOpponentTouch = true; break; }
            }
        }

        // Ball travel integral: accumulate height × step_distance once per frame
        // for all active sequences. PreStep runs once per frame (not per player),
        // so this avoids per-player inflation in 2v2/3v3.
        // Adapted from BallTravelReward (rlgym-tools, do_integral=True mode):
        // https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/ball_travel_reward.py
        if (_prevBallPosValid && state.ball.pos.z > minBallHeight) {
            float step_dist = (state.ball.pos - _prevBallPos).Length();
            float avg_z     = (state.ball.pos.z + _prevBallPos.z) * 0.5f;
            float step_integral = step_dist * avg_z;
            if (step_integral > 0.0f) {
                for (auto& [id, seq] : sequenceStates)
                    if (!seq.exhausted)
                        seq.travelIntegralAccum += step_integral;
            }
        }
        _prevBallPos      = state.ball.pos;
        _prevBallPosValid = true;
    }

    virtual void Reset(const GameState& initialState) override {
        sequenceStates.clear();
        offensiveAttempts = 0;
        offensiveCompletions = 0;
        _prevBallPos      = {};
        _prevBallPosValid = false;
        _wasFlipping.clear();
        _lastJumpFromWall.clear();
        pendingPreFlip.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        uint32_t id = player.carId;
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        Vec  carFromBall  = player.pos - state.ball.pos;
        float carBallDist = carFromBall.Length();

        // ====================================================================
        // Wall-jump-origin tracking (pre-flip mechanic, Rev 4.0) — runs every
        // step, independent of whether a sequence is currently open. isOnGround
        // is pure wheel-contact and stays true while driving on a wall, so a
        // horizontal up-vector (matches WallDashInputReward's on-wall check) is
        // what actually distinguishes a wall/backboard jump from a ground jump.
        // ====================================================================
        if (state.prev) {
            const Player& prevPlayer = state.prev->players[player.index];
            if (prevPlayer.isOnGround && !player.isOnGround) {
                _lastJumpFromWall[id] = fabsf(prevPlayer.rotMat.up.z) < WALL_UP_Z_THRESHOLD;
            }
            if (player.isOnGround) {
                _lastJumpFromWall[id] = false;  // landed — next flip is a new excursion
            }
        }

        bool wasFlipping = _wasFlipping[id];
        _wasFlipping[id] = player.isFlipping;

        bool hasActiveSequence = sequenceStates.find(id) != sequenceStates.end();

        // ====================================================================
        // Pre-flip setup mechanic (Rev 4.0) — only applies when no sequence is
        // open yet. A gentle diagonal flip off a wall/backboard jump on a slow,
        // distant, airborne ball opens a follow-through window; a good chase
        // (still below the ball, moving with it) opens a real sequence.
        // ====================================================================
        if (!hasActiveSequence) {
            auto pendIt = pendingPreFlip.find(id);
            if (pendIt != pendingPreFlip.end()) {
                PendingPreFlip& pend = pendIt->second;
                pend.timeRemaining -= dt;

                bool underBall = (carBallDist > 0.1f) && ((carFromBall.z / carBallDist) < -IDEAL_UNDERSIDE_Z);
                bool velAligned = false;
                if (player.vel.Length() > 50.0f && state.ball.vel.Length() > 50.0f) {
                    velAligned = player.vel.Normalized().Dot(state.ball.vel.Normalized()) > PREFLIP_VEL_ALIGN_DOT;
                }

                if (underBall && velAligned) {
                    pendingPreFlip.erase(pendIt);
                    sequenceStates[id] = SequenceState();
                    SequenceState& newSeq = sequenceStates[id];
                    newSeq.startHeight            = state.ball.pos.z;
                    newSeq.boostAtStart           = player.boost;
                    newSeq.playerPosAtStart       = player.pos;
                    newSeq.initialBallVelocity    = state.ball.vel.Length();
                    newSeq.distanceToOpponentGoal = GetDistanceToOpponentGoal(player, state);
                    newSeq.touchesSeen            = 1;   // the pre-flip touch is the opening touch
                    return PREFLIP_SETUP_BONUS;
                }
                if (pend.timeRemaining <= 0.0f) {
                    pendingPreFlip.erase(pendIt);
                }
            } else if (!wasFlipping && player.isFlipping && player.ballTouchedStep && state.prev
                       && _lastJumpFromWall[id]) {
                Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
                bool ballAirborne = state.ball.pos.z >= minBallHeight;
                bool ballSlow      = state.ball.vel.Length() < PREFLIP_MAX_BALL_SPEED;
                bool ballFar       = (state.ball.pos - oppGoal).Length() >= PREFLIP_MIN_GOAL_DIST;
                float pitch = player.prevAction.pitch, yaw = player.prevAction.yaw;
                bool diagonalFlip = (pitch > 0.5f && fabsf(yaw) > 0.2f && fabsf(yaw) < 0.8f);
                float hitForce = (state.ball.vel - state.prev->ball.vel).Length();
                bool gentleTouch = (hitForce >= PREFLIP_MIN_HIT_FORCE && hitForce <= PREFLIP_MAX_HIT_FORCE);

                if (ballAirborne && ballSlow && ballFar && diagonalFlip && gentleTouch) {
                    pendingPreFlip[id] = PendingPreFlip{ PREFLIP_WINDOW_SECONDS };
                }
            }
        }

        // ====================================================================
        // Generic entry (restored) — Rev 4.0's refactor accidentally deleted
        // the only path that could open a sequence from an ordinary underside
        // touch, leaving the pre-flip wall-jump mechanic as the sole entry
        // point. This restores the general case (ground pops, aerial touches,
        // any non-wall-jump setup) as an additional path alongside pre-flip,
        // using the same IDEAL_UNDERSIDE_Z bar the reward used before Rev 4.0.
        // ====================================================================
        if (!hasActiveSequence && player.ballTouchedStep && state.prev
            && !player.isOnGround && state.ball.pos.z >= minBallHeight) {
            bool entryAngleOk = (carBallDist > 0.1f) && ((carFromBall.z / carBallDist) < -IDEAL_UNDERSIDE_Z);
            if (entryAngleOk && RoofContact(player, state)) {
                sequenceStates[id] = SequenceState();
                SequenceState& newSeq = sequenceStates[id];
                newSeq.startHeight            = state.ball.pos.z;
                newSeq.boostAtStart           = player.boost;
                newSeq.playerPosAtStart       = player.pos;
                newSeq.initialBallVelocity    = state.ball.vel.Length();
                newSeq.distanceToOpponentGoal = GetDistanceToOpponentGoal(player, state);
                newSeq.touchesSeen            = 1;
                return 0.0f;   // the opening touch pays nothing (fix 1)
            }
        }

        hasActiveSequence = sequenceStates.find(id) != sequenceStates.end();  // pre-flip or generic entry may have just opened one
        if (!hasActiveSequence) {
            return 0.0f;
        }

        SequenceState& seq = sequenceStates.at(id);

        // ====================================================================
        // Opponent-touch resolution (Rev 4.0) — resolve+erase before anything
        // else if PreStep flagged this sequence as defended-away last frame.
        // ====================================================================
        if (seq.endedByOpponentTouch) {
            float released = ResolveSequence(player, state, seq, /*wasCapBypass=*/false);
            sequenceStates.erase(id);
            return (released > EPS_REWARD_SKIP) ? released : 0.0f;
        }

        seq.timeElapsed += dt;

        // ====================================================================
        // Retuned gate (Rev 4.0): continuous angle taper, minimal (distance-
        // only) erase condition — no time-based grace window needed. Old 70°
        // hard cutoff is now just the "full quality" line (IDEAL_UNDERSIDE_Z);
        // a marginal-angle touch pays less instead of wiping the sequence.
        // ====================================================================
        bool angleOk = (carBallDist > 0.1f) && ((carFromBall.z / carBallDist) < -HARD_FAIL_UNDERSIDE_Z);
        bool disconnected = carBallDist > MAX_CONTROL_DIST;

        if (player.isOnGround || state.ball.pos.z < minBallHeight || !angleOk || disconnected) {
            float released = ResolveSequence(player, state, seq, /*wasCapBypass=*/false);
            sequenceStates.erase(id);
            return (released > EPS_REWARD_SKIP) ? released : 0.0f;
        }

        if (!player.ballTouchedStep || !state.prev) {
            return 0.0f;
        }
        // A touch off the roof side (wheels / underside bump) does not count (fix 2); the sequence itself stays open.
        if (!RoofContact(player, state)) {
            return 0.0f;
        }
        seq.touchesSeen++;

        // Underside contact quality: 0 at the new wide edge, 1 at/beyond the old 70° "ideal" line
        float undersideNormZ   = carFromBall.z / carBallDist;
        float undersideQuality = RS_CLAMP(
            (-undersideNormZ - HARD_FAIL_UNDERSIDE_Z) / (IDEAL_UNDERSIDE_Z - HARD_FAIL_UNDERSIDE_Z),
            0.0f, 1.0f);
        float undersideMul = 0.2f + 0.8f * undersideQuality;

        // Behind-ball horizontal alignment (Rev 5.0) — on the first two touches only
        // (the pop that sets the dribble's height + direction), also reward contact
        // on the side of the ball opposite the intended travel direction. Hitting
        // low AND slightly behind is what converts height into forward distance
        // instead of a straight-up juggle.
        float behindMul = 1.0f;
        if (seq.touchCount <= 1) {
            Vec oppGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
            float toGoalX = oppGoal.x - state.ball.pos.x;
            float toGoalY = oppGoal.y - state.ball.pos.y;
            float toGoalLen = sqrtf(toGoalX * toGoalX + toGoalY * toGoalY);
            float offsetLen = sqrtf(carFromBall.x * carFromBall.x + carFromBall.y * carFromBall.y);
            if (toGoalLen > 1.0f && offsetLen > 1.0f) {
                // Negative dot = car offset points opposite the travel direction (behind the ball) — what we want.
                float dot = (carFromBall.x * toGoalX + carFromBall.y * toGoalY) / (offsetLen * toGoalLen);
                float behindQuality = RS_CLAMP((-dot + BEHIND_ALIGN_TOLERANCE) / (1.0f + BEHIND_ALIGN_TOLERANCE), 0.0f, 1.0f);
                behindMul = BEHIND_MUL_MIN + (BEHIND_MUL_MAX - BEHIND_MUL_MIN) * behindQuality;
            }
        }

        // ====================================================================
        // Normal touch scoring + goal-mouth resolution (Rev 4.0: reworked from
        // a flat per-touch travel bonus into the outcome-gated ResolveSequence)
        // ====================================================================
        bool isScoringTouch = IsHeadingToGoalMouth(player);

        float reward = 0.0f;
        if (!seq.exhausted) {
            reward = EvaluateOffensive(player, state, seq);
            if (reward > EPS_REWARD_SKIP) {
                // Underside quality multiplier — rewards more vertical (beneath-ball) approach
                reward *= undersideMul;
                // Behind-ball quality multiplier — rewards contact opposite the travel direction on early touches
                reward *= behindMul;
                // Horizontal speed cap: penalize ball horiz speed > 2300 uu/s (car can't keep up)
                reward *= GetHorizVelPenaltyMul(player, state);
            }
        }

        if (isScoringTouch && !seq.scoringTouchPaid && seq.touchesSeen >= MIN_TOUCHES_TO_PAY) {
            seq.scoringTouchPaid = true;
            reward += ResolveSequence(player, state, seq, /*wasCapBypass=*/seq.exhausted);
            // Deliberately NOT erased here — IsHeadingToGoalMouth is a BallPred prediction,
            // not a guaranteed goal; the ball can still be intercepted before crossing, so
            // the sequence stays alive for a possible later opponent-touch/disconnect resolution.
        }

        return (reward > EPS_REWARD_SKIP) ? reward : 0.0f;
    }

    bool HasActiveSequence(uint32_t carId) const { return sequenceStates.find(carId) != sequenceStates.end(); }

    // Metrics accessors for W&B monitoring
    uint64_t GetOffensiveAttempts() const { return offensiveAttempts; }
    uint64_t GetOffensiveCompletions() const { return offensiveCompletions; }
};

} // namespace RLGC
