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

// Ported from RLGymBot_v3 (MultiModalRewards.h: PassingSetupReward + ReceivingPassReward; BallPred::Cache -> BallPredCache (engine prediction, 2.0s horizon)). Logic unchanged unless noted.

namespace RLGC {

    // File-scope in v3's MultiModalRewards.h; shared by PassingSetupReward and ReceivingPassReward.
    static constexpr float MIN_PASS_SPEED_UU_S    = 400.0f; // shared pass-attempt speed floor
    static constexpr float MIN_PASS_DISTANCE_UU   = 800.0f; // shared minimum passer/receiver spacing

    class PassingSetupReward : public Reward {
    public:
        struct Metrics {
            int    touchesFired    = 0;   // touches that banked positive pass-setup credit
            double rewardSum       = 0.0; // sum of positive reward issued (rewardSum/touchesFired = mean value)
            int    zoneBonusActive = 0;   // touches where the zone-accuracy bonus was > 0 (LOW finding: check its real activation rate)
            int    cooldownBlocked = 0;   // touches that cleared every other gate but were suppressed by the F2 anti-farm cooldown
        };

    private:
        BallPredCache _ballCache;
        PerPlayerCooldown _cooldown; // anti-farm: min interval between credited touches (F2)
        static constexpr float PASS_SETUP_COOLDOWN_SEC = 0.5f;

        // Zone-accuracy bonus lead distance, ahead of the teammate toward goal (Nathan,
        // 2026-09-14): bounded so the "optimal zone" never requires a pass to travel
        // deep into the opponent's side, regardless of where on the field the teammate is.
        static constexpr float ZONE_LEAD_MIN_UU = 300.0f;
        static constexpr float ZONE_LEAD_MAX_UU = 1500.0f;

        // Metrics — thread-safe atomics (shared across all instances / all parallel envs).
        // Static because RewardConfig.cpp holds this behind a ZeroSumReward/WeightedReward
        // wrapper chain with no retained pointer -- instance members wouldn't be reachable
        // from ExampleMain.cpp (see AirDribbleSetupReward's 2026-09-13 fix for the same issue).
        inline static std::atomic<int>       s_touchesFired    { 0 };
        inline static std::atomic<long long> s_rewardMilli     { 0 };
        inline static std::atomic<int>       s_zoneBonusActive { 0 };
        inline static std::atomic<int>       s_cooldownBlocked { 0 };

        bool IsTeammatePosition(const Vec& pos, const Player& player, const GameState& state, float proximityRadius = 1500.0f) const {
            // Check if any teammate is close to this position
            for (const auto& teammate : state.players) {
                if (teammate.team != player.team) continue;
                if (teammate.carId == player.carId) continue;
                if (teammate.isDemoed) continue;

                float distToPos = (teammate.pos - pos).Length();
                if (distToPos < proximityRadius) {
                    return true;
                }
            }
            return false;
        }

        // Does any living, non-demoed teammate exist for this player?
        bool HasLivingTeammate(const Player& player, const GameState& state) const {
            for (const auto& teammate : state.players) {
                if (teammate.team != player.team) continue;
                if (teammate.carId == player.carId) continue;
                if (teammate.isDemoed) continue;
                return true;
            }
            return false;
        }

        // Is the ball heading toward any living, non-demoed teammate right now?
        bool IsBallHeadingTowardTeammate(const Vec& ballVelNorm, const Player& player, const GameState& state) const {
            for (const auto& teammate : state.players) {
                if (teammate.team != player.team) continue;
                if (teammate.carId == player.carId) continue;
                if (teammate.isDemoed) continue;

                Vec toTeammate = (teammate.pos - state.ball.pos).Normalized();
                if (ballVelNorm.Dot(toTeammate) > 0.3f) return true;
            }
            return false;
        }

        float GetTeammateDistToPos(const Vec& pos, const Player& player, const GameState& state) const {
            float minDist = 1e6f;
            for (const auto& teammate : state.players) {
                if (teammate.team != player.team) continue;
                if (teammate.carId == player.carId) continue;
                if (teammate.isDemoed) continue;

                float dist = (teammate.pos - pos).Length();
                minDist = RS_MIN(minDist, dist);
            }
            return minDist;
        }

        float GetOpponentDistToPos(const Vec& pos, const Player& player, const GameState& state) const {
            float minDist = 1e6f;
            for (const auto& opponent : state.players) {
                if (opponent.team == player.team) continue;
                if (opponent.isDemoed) continue;

                float dist = (opponent.pos - pos).Length();
                minDist = RS_MIN(minDist, dist);
            }
            return minDist;
        }

        // NEW: Calculate optimal pass zone between teammate and goal
        //
        // Rev (2026-09-14, Nathan + Claude): the zone used to be placed at
        // (distToGoal * optimalDistanceFactor), i.e. a FRACTION (0.4-1.0) of the
        // teammate's full remaining distance to the opponent's goal. For a teammate
        // anywhere outside the attacking third that meant the "optimal zone" landed
        // deep in the opponent's half regardless of tactical context -- e.g. a
        // teammate 3050 UU from goal got a zone 2592 UU ahead of them, almost on the
        // goal line itself (traced in the 2026-09-14 scenario review, S1/S6 -- the
        // bonus scored 0 in both because no realistic short/lead pass reaches that
        // far). Nathan: a pass shouldn't need to go deep into the opponent's side to
        // register zone credit. Replaced with a bounded LEAD distance ahead of the
        // teammate (ZONE_LEAD_MIN_UU-ZONE_LEAD_MAX_UU), still scaled by the same
        // alignment/boost blend, but relative to the teammate's own position instead
        // of the absolute distance to goal -- so a lead pass in a team's own half
        // scores identically to the same tactical pass deep in the attacking third.
        Vec GetOptimalPassZone(const Vec& teammatePpos, const Vec& goalPos, const Vec& teamVel, float boost) const {
            // Direction from teammate toward goal
            Vec towardGoal = (goalPos - teammatePpos).Normalized();

            // Teammate velocity direction (where they're moving)
            Vec movementDir = teamVel.Length() > 100.0f ? teamVel.Normalized() : towardGoal;

            // Blend: If teammate moving toward goal, pass can be further ahead
            // If teammate moving sideways/backward, pass should be closer
            float alignmentToGoal = RS_MAX(0.0f, movementDir.Dot(towardGoal));  // 0-1

            // Boost factor: High boost (100) = pass can be further ahead
            // Low boost (0) = pass should be closer to teammate (easier reception)
            float boostFactor = boost / 100.0f;  // 0-1

            // Lead distance ahead of the teammate -- bounded, not a fraction of the
            // full field distance to goal. High alignment + high boost -> lead further
            // (toward ZONE_LEAD_MAX_UU); low alignment + low boost -> lead less
            // (toward ZONE_LEAD_MIN_UU).
            float leadBlend = (alignmentToGoal * 0.5f) + (boostFactor * 0.5f);  // 0-1
            float leadDistance = ZONE_LEAD_MIN_UU + leadBlend * (ZONE_LEAD_MAX_UU - ZONE_LEAD_MIN_UU);

            // Never place the zone past the goal itself for a teammate already close to it.
            float distToGoal = (goalPos - teammatePpos).Length();
            leadDistance = RS_MIN(leadDistance, distToGoal);

            return teammatePpos + towardGoal * leadDistance;
        }

        // NEW: Evaluate how well pass zone matches actual pass
        float EvaluatePassZoneAccuracy(const Vec& predictedBallPos, const Vec& optimalZone,
                                       const Vec& teamPos, const Vec& goalPos) const {
            // How close is predicted ball to optimal zone?
            float distToOptimal = (predictedBallPos - optimalZone).Length();

            // Radius of acceptable zone: 400 units (larger zone = more forgiving)
            float zoneRadius = 400.0f;

            if (distToOptimal <= zoneRadius) {
                // Perfect zone hit: scale from 1.0 (at center) to 0.3 (at edge)
                return 1.0f - (distToOptimal / zoneRadius) * 0.7f;
            } else {
                // Outside zone: still small bonus if not too far
                if (distToOptimal <= zoneRadius * 2.0f) {
                    return 0.3f * (1.0f - ((distToOptimal - zoneRadius) / zoneRadius) * 0.5f);
                }
                return 0.0f;
            }
        }

    public:
        PassingSetupReward() {}

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.touchesFired    = s_touchesFired.exchange(0);
            long long rm      = s_rewardMilli.exchange(0);
            m.rewardSum       = double(rm) / 1000.0;
            m.zoneBonusActive = s_zoneBonusActive.exchange(0);
            m.cooldownBlocked = s_cooldownBlocked.exchange(0);
            return m;
        }

        virtual void PreStep(const GameState& state) override {
            _ballCache.Update(state);
            _cooldown.TickAll(StepDt(state)); // bulk-tick every step so cooldown reflects real elapsed time (CLAUDE.md 7C)
        }

        virtual void Reset(const GameState& initialState) override {
            _cooldown.Reset();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            // Only reward if WE just touched the ball (setting up the pass)
            if (!player.ballTouchedStep) return 0.0f;

            // No living teammate to pass to -- this is just an ordinary shot/clear,
            // not a pass setup of any kind. Previously only the primary reward tier
            // checked this, so 1v1 (or a demoed sole teammate) could still bank
            // secondary/trajectory/zone credit for a plain shot.
            if (!HasLivingTeammate(player, state)) return 0.0f;

            // Check ball velocity direction
            Vec ballVel = state.ball.vel;
            if (ballVel.Length() < MIN_PASS_SPEED_UU_S) return 0.0f;  // Ball too slow to be a pass

            // Anti-farm (F3): a teammate standing right on top of the passer isn't a real
            // pass target. Gated on distance at the moment of touch, not the 1.5s predicted
            // landing point, so it can't be dodged by aiming somewhere prediction reads as far.
            if (GetTeammateDistToPos(state.ball.pos, player, state) < MIN_PASS_DISTANCE_UU) return 0.0f;

            Vec ballVelNorm = ballVel.Normalized();

            Vec theirGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;

            // Compute goal direction from ball position, not player position.
            // Ball velocity is relative to ball, so the reference point should be ball too.
            Vec towardTheirGoal = (theirGoal - state.ball.pos).Normalized();

            // Use the pre-computed ball prediction cache (populated in PreStep).
            // Scoped to the first 2s only, matching the ground-bounce check below --
            // avoids misclassifying an ordinary pass whose far-future (>2s, long
            // after any real teammate would've received it) trajectory happens to
            // eventually clip a wall.
            // Port guard: v3's cache was always valid. Here a missing prediction (obs builder without
            // ballPredSlices, or a state with no arena) would make AtTime() return a stationary-ball
            // fallback and silently change what this pays, so pay nothing instead.
            if (!_ballCache.valid) return 0.0f;

            bool isSurfacePass = _ballCache.HitSurfaceBy(2.0f);

            // Check if ball is moving generally toward their goal OR toward a living
            // teammate. Only penalize when it's heading toward neither -- a
            // deliberate backward/lateral reset pass to a teammate is good,
            // disciplined play and should not be penalized just because it isn't
            // goalward.
            float goalwardComponent = ballVelNorm.Dot(towardTheirGoal);
            bool towardTeammate = IsBallHeadingTowardTeammate(ballVelNorm, player, state);
            if (goalwardComponent < 0.2f && !isSurfacePass && !towardTeammate) {
                // Ball moving backward, no surface redirect, and not toward any teammate either
                return -0.2f;
            }

            // Anti-farm (F2): cap how often a single passer can bank positive pass-setup
            // credit. Checked AFTER the direction gate above so a genuinely aimless hit is
            // still always penalized -- cooldown only suppresses the positive-credit path.
            if (_cooldown.IsOnCooldown(player.carId)) {
                s_cooldownBlocked.fetch_add(1);
                return 0.0f;
            }

            // Use the slice at ~1.5 seconds as the "landing zone" for pass evaluation.
            // This is close enough to where a typical pass will reach a teammate.
            Vec predictedBallPos = _ballCache.AtTime(1.5f).pos;

            // Check if teammates are better positioned than opponents at ball destination
            float tmDist = GetTeammateDistToPos(predictedBallPos, player, state);
            float oppDist = GetOpponentDistToPos(predictedBallPos, player, state);

            float reward = 0.0f;

            // PRIMARY REWARD: Ball trajectory favors teammates. Continuous in the
            // margin (oppDist - tmDist) rather than a hard binary -- reproduces the
            // old 2.0/1.0/0.0 tiers exactly at margin=500/0/<=-500 and interpolates
            // smoothly between/beyond, so a coin-flip pass into a crowd scores
            // visibly less than a comfortably open one.
            if (tmDist < 1e6f && oppDist < 1e6f) {
                float margin = oppDist - tmDist;
                reward += 2.0f * RS_CLAMP((margin + 500.0f) / 1000.0f, 0.0f, 1.0f);
            }

            // TRAJECTORY QUALITY BONUS: reward clean passes and intentional surface redirects.
            if (isSurfacePass) {
                // Intentional wall/ceiling/backboard redirect ending near a teammate
                if (tmDist < 2000.0f) {
                    reward += 1.0f;  // Surface-redirect pass with receiver in range
                }
            } else {
                // Direct / lob pass quality: count ground bounces within the first 2s only.
                // The full _ballCache.groundBounces spans 4s — bounces after reception are irrelevant.
                int earlyGroundBounces = 0;
                for (int i = 0; i < _ballCache.numSlices; i++) {
                    if (_ballCache.slices[i].t > 2.0f) break;
                    if (_ballCache.slices[i].hitGround &&
                        (i == 0 || !_ballCache.slices[i - 1].hitGround)) {
                        earlyGroundBounces++;
                    }
                }
                if (earlyGroundBounces == 0) {
                    reward += 1.5f;  // Clean lob with no bounces - excellent!
                } else if (earlyGroundBounces == 1) {
                    reward += 0.8f;  // One ground bounce - acceptable
                }
                // earlyGroundBounces >= 2: chaotic, no bonus
            }

            // Evaluate pass zone accuracy
            float zoneAccuracyBonus = 0.0f;
            for (const auto& teammate : state.players) {
                if (teammate.team != player.team || teammate.carId == player.carId) continue;
                if (teammate.isDemoed) continue;

                Vec opponentGoal = theirGoal;

                Vec optimalZone = GetOptimalPassZone(teammate.pos, opponentGoal,
                                                     teammate.vel, teammate.boost);

                float zoneScore = EvaluatePassZoneAccuracy(predictedBallPos, optimalZone,
                                                           teammate.pos, opponentGoal);

                float bonus = 0.5f * zoneScore;

                // Weight by teammate quality (favor passes to teammates with more boost --
                // they can do more with the ball once received). The old passer's-own-boost
                // multiplier was removed here (Rev 3.0): rewarding the passer for having LOW
                // boost is a backwards incentive, not a real tactical signal.
                float boostWeight = 1.0f + (teammate.boost / 100.0f) * 0.2f;  // 1.0-1.2x
                bonus *= boostWeight;

                zoneAccuracyBonus = RS_MAX(zoneAccuracyBonus, bonus);
            }

            if (zoneAccuracyBonus > 0.0f) {
                reward += zoneAccuracyBonus;
                s_zoneBonusActive.fetch_add(1);
            }

            // Only start the cooldown when credit was actually earned (CLAUDE.md 7C common-bug
            // table: setting it unconditionally would also block the next legitimate touch
            // after a step that happened to score 0, e.g. no teammate closer than the opponent).
            if (reward > 0.0f) {
                _cooldown.Trigger(player.carId, PASS_SETUP_COOLDOWN_SEC);
                s_touchesFired.fetch_add(1);
                s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
            }

            return reward;
        }
    };

    // ============================================================================
    // RECEIVING PASS REWARD - Reward receiving a good pass from teammate
    // ============================================================================
    // This reward encourages:
    // 1. Positioning to receive passes (being in teammate passing lanes)
    // 2. Touching the ball when it's already moving toward you (receiving)
    // 3. Maintaining good position relative to ball trajectory
    //
    // Strategy:
    // - Track ball velocity and which team member moved it last
    // - If teammate moved ball toward us, reward our positioning/touch
    // - Reward based on how cleanly we receive (ball speed alignment)
    // - Bonus for converting receive into a shot or dribble setup
    //
    // Reward structure:
    // - +1.5: Touching ball that teammate passed toward you
    // - +0.5: Good positioning relative to incoming pass trajectory
    // - +2.0: Successfully converting receive into immediate shot/play
    // - -0.3: Missing a pass opportunity (ball flies past without touch)
    //
    // Rev 1.0 (2025-01-21, NJH)
    // Rev 2.0 (2026-08-04, NJH + Claude) - Scenario-review fix pass (see
    // Reward_Change_Log.md for the full 10-scenario investigation):
    //   - Replaced the buggy single-slot `lastBallTouchBy[0]` tracking
    //     (declared as a map, but only ever read/wrote the constant key 0,
    //     and was never cleared except at Reset()) with a proper PreStep
    //     pass that arms `recentPassToMe` ONCE, at the actual moment of a
    //     teammate's touch, keyed correctly per receiving player.
    //   - An opponent touching the ball now clears the receiver's armed
    //     state -- a pass that gets intercepted no longer counts as "still
    //     expecting a pass" if the ball later drifts back toward the
    //     original target.
    //   - `recentPassToMe`/`passReceiveTime` no longer re-arm on every
    //     qualifying step (previously any step where the ball's velocity
    //     happened to point roughly at the player would reset the window) --
    //     "time since pass" now measures real elapsed time since the actual
    //     touch.
    //   - REWARD 1 / REWARD 2 / PENALTY point values and thresholds are
    //     unchanged -- only the arming mechanism was fixed.
    //
    // Rev 3.0 (2026-09-14, NJH + Claude) - Farming fixes from the
    // 2026-09-14 scenario review (see Reward_Change_Log.md):
    //   - Fixed the PENALTY block (finding F1, CRITICAL): `recentPassToMe`
    //     was only cleared when the receiver had ALSO drifted >2000 UU from
    //     the ball, so a receiver who camped near a live ball without
    //     touching it never got reset and kept collecting REWARD 1's
    //     positioning credit every step, indefinitely (~15-24/sec). The
    //     reset now always fires once the miss window elapses; only the
    //     -0.3 penalty itself stays conditional on distance.
    //   - Arming now shares PassingSetupReward's MIN_PASS_SPEED_UU_S (400)
    //     instead of a lower, independent 200 floor (finding S5) -- a touch
    //     too slow to count as a pass attempt on the passer side can no
    //     longer arm the receiver's expectation either.
    //   - Added the same MIN_PASS_DISTANCE_UU gate as PassingSetupReward
    //     (finding F3) -- a teammate standing right next to the toucher no
    //     longer arms as a pass target, closing the close-range give-and-go
    //     volley that stacked both classes' rewards on every touch.
    class ReceivingPassReward : public Reward {
    public:
        struct Metrics {
            int armEvents      = 0; // teammate-arm assignments (one per touch per qualifying teammate)
            int multiArmEvents = 0; // touches that simultaneously armed 2+ teammates (S7 -- bystander credit)
            int reward1Frames  = 0; // steps REWARD 1 (inbound positioning) fired
            int receptions     = 0; // REWARD 2 fire count (clean catches)
            int misses         = 0; // PENALTY fire count (aimed pass never touched)
        };

    private:
        mutable std::unordered_map<uint32_t, float> passReceiveTime;
        mutable std::unordered_map<uint32_t, bool> recentPassToMe;
        mutable std::unordered_map<uint32_t, bool> surfacePassActive;  // Pass via wall/ceiling/backboard
        mutable std::unordered_map<uint32_t, float> elapsedTime;
        mutable std::unordered_map<uint32_t, float> reward1AccumThisArm; // cumulative REWARD1 paid since this arm

        // REWARD1 is a continuous per-step positioning shape with no touch required; capping its
        // cumulative payout per arm-window keeps an actual catch (>= REWARD2's base 1.5) always
        // worth at least as much as pure positioning-without-catching (balance finding, 2026-09-14
        // scenario review -- traced S1 at ~9.75 uncapped vs. REWARD2's 1.5-4.5, a ~6.5:1 skew).
        static constexpr float MAX_REWARD1_PER_ARM = 1.5f;

        // Proximity thresholds for surface detection (matching AdvancedPlayRewards.h patterns)
        static constexpr float WALL_NEAR_THRESH = 300.0f;
        static constexpr float CEIL_NEAR_THRESH = 200.0f;
        static constexpr float BACK_NEAR_THRESH = 400.0f;
        static constexpr float BACK_MIN_Z       = 300.0f;

        // Metrics — thread-safe atomics (shared across all instances / all parallel envs).
        // Static because RewardConfig.cpp holds this behind a ZeroSumReward/WeightedReward
        // wrapper chain with no retained pointer -- instance members wouldn't be reachable
        // from ExampleMain.cpp (see AirDribbleSetupReward's 2026-09-13 fix for the same issue).
        inline static std::atomic<int> s_armEvents      { 0 };
        inline static std::atomic<int> s_multiArmEvents { 0 };
        inline static std::atomic<int> s_reward1Frames  { 0 };
        inline static std::atomic<int> s_receptions     { 0 };
        inline static std::atomic<int> s_misses         { 0 };

        bool IsBallNearSurface(const Vec& ballPos) const {
            bool nearSideWall   = std::abs(ballPos.x) > (CommonValues::SIDE_WALL_X - WALL_NEAR_THRESH);
            bool nearCeiling    = ballPos.z > (CommonValues::CEILING_Z - CEIL_NEAR_THRESH);
            bool nearBackboard  = std::abs(ballPos.y) > (CommonValues::BACK_WALL_Y - BACK_NEAR_THRESH)
                                  && ballPos.z > BACK_MIN_Z;
            return nearSideWall || nearCeiling || nearBackboard;
        }

    public:
        ReceivingPassReward() {}

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.armEvents      = s_armEvents.exchange(0);
            m.multiArmEvents = s_multiArmEvents.exchange(0);
            m.reward1Frames  = s_reward1Frames.exchange(0);
            m.receptions     = s_receptions.exchange(0);
            m.misses         = s_misses.exchange(0);
            return m;
        }

        // Arms/clears recentPassToMe exactly once per step, at the moment of an
        // actual touch -- not recomputed (and potentially re-armed on stale data)
        // inside every player's own GetReward call.
        virtual void PreStep(const GameState& state) override {
            float dt = StepDt(state);
            for (const auto& p : state.players) {
                elapsedTime[p.carId] += dt;
            }

            for (const auto& p : state.players) {
                if (!p.ballTouchedStep) continue;

                // Counts how many teammates this single touch arms -- >=2 is the S7
                // "bystander credit" case (a pass aimed at one receiver simultaneously
                // arms a second teammate standing nearby).
                int armedThisTouch = 0;

                for (const auto& other : state.players) {
                    if (other.carId == p.carId) continue;
                    uint32_t id = other.carId;

                    if (other.team == p.team) {
                        // p just touched the ball -- potential pass to teammate `other`.
                        if (other.isDemoed) continue;

                        // Anti-farm (F3): a teammate standing right on top of the toucher
                        // isn't a real pass target -- mirrors PassingSetupReward's own gate.
                        float distToOther = (other.pos - state.ball.pos).Length();
                        if (distToOther < MIN_PASS_DISTANCE_UU) continue;

                        Vec ballVel = state.ball.vel;
                        // Shared with PassingSetupReward's pass-attempt floor (finding S5).
                        if (ballVel.Length() <= MIN_PASS_SPEED_UU_S) continue;

                        Vec toOther = (other.pos - state.ball.pos).Normalized();
                        float towardComponent = ballVel.Normalized().Dot(toOther);

                        // For surface-bounce passes the ball may be heading toward a wall/ceiling,
                        // not directly at `other` yet. Relax the threshold when ball is near a surface.
                        bool nearSurface = IsBallNearSurface(state.ball.pos);
                        float dirThreshold = nearSurface ? 0.1f : 0.3f;

                        if (towardComponent > dirThreshold) {
                            recentPassToMe[id] = true;
                            surfacePassActive[id] = nearSurface;
                            passReceiveTime[id] = elapsedTime[id];
                            reward1AccumThisArm[id] = 0.0f; // fresh arm -- reset the REWARD1 cap tracker
                            armedThisTouch++;
                        }
                    } else {
                        // p is an opponent of `other` and just touched the ball -- any pass
                        // `other` was expecting has been intercepted.
                        recentPassToMe[id] = false;
                        surfacePassActive[id] = false;
                    }
                }

                if (armedThisTouch > 0) {
                    s_armEvents.fetch_add(armedThisTouch);
                    if (armedThisTouch >= 2) s_multiArmEvents.fetch_add(1);
                }
            }
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float reward = 0.0f;

            // REWARD 1: Positioning relative to incoming pass
            if (recentPassToMe[id]) {
                Vec ballVel = state.ball.vel;
                if (ballVel.Length() > 200.0f) {
                    Vec theirGoal = (player.team == Team::BLUE)
                        ? CommonValues::ORANGE_GOAL_CENTER
                        : CommonValues::BLUE_GOAL_CENTER;
                    Vec towardGoal = (theirGoal - player.pos).Normalized();

                    Vec ballVelNorm = ballVel.Normalized();
                    float goalwardComponent = ballVelNorm.Dot(towardGoal);

                    // Reward receiver who is moving TOWARD the ball (approach alignment),
                    // not just receivers moving in the same direction as the ball (old behavior
                    // penalized players correctly sprinting INTO an incoming pass).
                    Vec toBall = (state.ball.pos - player.pos).Normalized();
                    float approachAlignment = player.vel.Length() > 100.0f
                        ? player.vel.Normalized().Dot(toBall)
                        : 1.0f;  // stationary receiver = fine (they'll flip/dodge to it)

                    if (goalwardComponent > 0.3f && approachAlignment > 0.1f) {
                        // Anti-imbalance: cap cumulative REWARD1 this arm-window at
                        // MAX_REWARD1_PER_ARM (balance finding, 2026-09-14 review) -- pure
                        // positioning-without-catching can no longer out-earn a real catch.
                        float positioningValue = 0.5f + (player.boost / 100.0f) * 0.3f;
                        float& accum = reward1AccumThisArm[id];
                        float credited = RS_MIN(positioningValue, RS_MAX(0.0f, MAX_REWARD1_PER_ARM - accum));

                        if (credited > 0.0f) {
                            reward += credited;
                            accum += credited;
                            s_reward1Frames.fetch_add(1);
                        }
                    }
                }
            }

            // REWARD 2: Receiving the pass
            if (player.ballTouchedStep && recentPassToMe[id]) {
                float timeSincePass = elapsedTime[id] - passReceiveTime[id];

                // Surface-bounce passes take longer to arrive; allow a wider window
                float receiveWindow = surfacePassActive[id] ? 2.5f : 1.0f;

                if (timeSincePass < receiveWindow) {
                    reward += 1.5f;  // Clean reception
                    s_receptions.fetch_add(1);

                    // Extra bonus for successfully receiving a surface-bounce pass
                    if (surfacePassActive[id]) {
                        reward += 0.5f;
                    }

                    // BONUS: Converting receive into offense (threshold lowered 2000→1400 UU/s;
                    // most good first-touch shots are 1300-1800 UU/s, 2000 was rarely reached)
                    Vec ballVel = state.ball.vel;
                    if (ballVel.Length() > 1400.0f) {
                        float boostBonus = (player.boost / 100.0f) * 0.5f;
                        reward += 2.0f + boostBonus;
                    }

                    recentPassToMe[id] = false;
                    surfacePassActive[id] = false;
                }
            }

            // PENALTY: Missing a pass opportunity
            if (recentPassToMe[id]) {
                float timeSincePass = elapsedTime[id] - passReceiveTime[id];

                // Surface passes need more time before we declare a miss.
                // Direct pass window raised 0.5→1.0s: a ball at 1000 UU/s takes ~1.5s to
                // travel 1500 UU — 0.5s was declaring misses before the ball even arrived.
                float missWindow = surfacePassActive[id] ? 2.0f : 1.0f;

                if (timeSincePass > missWindow) {
                    float distToBall = (player.pos - state.ball.pos).Length();

                    // Always clear the armed state once the window has elapsed -- previously
                    // this reset was nested inside the distToBall>2000 branch, so a receiver
                    // who camped near a live ball without touching it never reset and kept
                    // collecting REWARD 1's positioning credit indefinitely (finding F1,
                    // CRITICAL -- 2026-09-14 scenario review). Only the -0.3 penalty itself
                    // stays conditional on distance.
                    if (distToBall > 2000.0f) {
                        reward -= 0.3f;
                        s_misses.fetch_add(1);
                    }
                    recentPassToMe[id] = false;
                    surfacePassActive[id] = false;
                }
            }

            return reward;
        }

        virtual void Reset(const GameState& initialState) override {
            passReceiveTime.clear();
            recentPassToMe.clear();
            surfacePassActive.clear();
            elapsedTime.clear();
            reward1AccumThisArm.clear();
        }
    };

} // namespace RLGC
