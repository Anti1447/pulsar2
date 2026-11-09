#pragma once
#include "FlickRewards.h"

// Ported from RLGymBot_v3 (AdvancedRewards.h: DribbleReward + DribbleMasteryReward). Logic unchanged unless noted.

namespace RLGC {

    
    // ============================================================================

    // ============================================================================
    // Rev 2 - 2025-12-15 (NJH): Make DribbleReward detect true "carry on top"
    //   - Require ball over roof area (small forward + lateral offsets in car-local frame)
    //   - Remove "ball in front" gating (that rewards pushing)
    //   - Add relative-velocity similarity (carrying vs bouncing/pushing)
    //   - Add upper height band to avoid rewarding pop-ups
    // ============================================================================

    class DribbleReward : public Reward {
    private:
        // --- "On top of car" geometry (tighter than before) ---
        float maxForwardOffset = 150.0f;       // ball center not too far in front of car center
        float minForwardOffset = -110.0f;       // allow slightly behind center (still on roof)
        float maxLateralOffset = 120.0f;       // ball should be near the center line
        float maxDistanceXY = 130.0f;          // overall horizontal radius (tight)

        // height band relative to car position
        float minBallHeightAboveCar = 55.0f;   // ball must be above roof-ish
        float maxBallHeightAboveCar = 220.0f;  // too high = juggling/pop, not dribble carry

        // carrying tends to have similar velocities
        float maxRelativeSpeed = 800.0f;       // |ballVel - carVel| must be under this
        float minCarSpeed = 500.0f;            // still require some movement

        float requiredSustainSec = 0.25f;
        float rewardScale = 1.0f;

        const float NO_PREV_SHAPE = 0.02f;

        mutable std::map<uint32_t, float> sustainedTime;

    public:
        explicit DribbleReward(
            float maxFwd = 150.0f,
            float minFwd = -110.0f,
            float maxLat = 120.0f,
            float maxXY = 130.0f,
            float minH = 55.0f,
            float maxH = 220.0f,
            float relSpd = 800.0f,
            float minSpd = 500.0f,
            float sustain = 0.25f,
            float scale = 1.0f)
            : maxForwardOffset(maxFwd)
            , minForwardOffset(minFwd)
            , maxLateralOffset(maxLat)
            , maxDistanceXY(maxXY)
            , minBallHeightAboveCar(minH)
            , maxBallHeightAboveCar(maxH)
            , maxRelativeSpeed(relSpd)
            , minCarSpeed(minSpd)
            , requiredSustainSec(sustain)
            , rewardScale(scale)
        {
        }

        virtual void Reset(const GameState& initialState) override {
            sustainedTime.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

            if (!player.isOnGround) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // Ball relative position in world
            Vec diff = state.ball.pos - player.pos;

            // Height band: must be above car but not too high
            float dz = diff.z;
            if (dz < minBallHeightAboveCar || dz > maxBallHeightAboveCar) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // Horizontal distance tight
            Vec diffXY = diff; diffXY.z = 0.0f;
            float distXY = diffXY.Length();
            if (distXY > maxDistanceXY) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // Car-local offsets (this is the key)
            float forwardOffset = player.rotMat.forward.Dot(diff);
            float lateralOffset = player.rotMat.right.Dot(diff);

            // If ball is too far forward -> likely pushing, not carrying
            if (forwardOffset > maxForwardOffset || forwardOffset < minForwardOffset) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            if (fabs(lateralOffset) > maxLateralOffset) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // Movement check
            float carSpeed = player.vel.Length();
            if (carSpeed < minCarSpeed) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // Carry check: ball and car velocities should be somewhat similar
            float relSpeed = (state.ball.vel - player.vel).Length();
            if (relSpeed > maxRelativeSpeed) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // If no previous state, give tiny shaping to get learning signal started
            if (!state.prev) {
                sustainedTime[id] += dt;
                return NO_PREV_SHAPE * rewardScale;
            }

            // Sustain accumulation
            sustainedTime[id] += dt;
            float sustain01 = RS_MIN(1.0f, sustainedTime[id] / requiredSustainSec);

            // Shaping: prefer centered ball + velocity match + slight forward (roof/hood)
            float centeredLat = 1.0f - RS_MIN(1.0f, fabs(lateralOffset) / maxLateralOffset);
            float centeredFwd = 1.0f;

            // Encourage ball near the roof center / slightly forward, but not "in front"
            // Map forwardOffset into a peak around ~40uu forward.
            float peak = 40.0f;
            float span = (maxForwardOffset - minForwardOffset) * 0.5f;
            float fwdErr = fabs(forwardOffset - peak);
            centeredFwd = 1.0f - RS_MIN(1.0f, fwdErr / RS_MAX(1.0f, span));

            float prox = 1.0f - RS_MIN(1.0f, distXY / maxDistanceXY);

            float rel01 = 1.0f - RS_MIN(1.0f, relSpeed / maxRelativeSpeed);

            // Speed bell-curve: peaks at 1.0x in [1300, 1600] UU/s, falls to 0.5x at both
            // extremes (0 stationary, 2300 supersonic). Replaces linear speed01 which
            // previously incentivized dribbling at max speed.
            static constexpr float DRIB_OPT_LOW  = 1300.0f;
            static constexpr float DRIB_OPT_HIGH = 1600.0f;
            float speedMult;
            if (carSpeed >= DRIB_OPT_LOW && carSpeed <= DRIB_OPT_HIGH) {
                speedMult = 1.0f;
            } else if (carSpeed < DRIB_OPT_LOW) {
                speedMult = 0.5f + 0.5f * (carSpeed / DRIB_OPT_LOW);
            } else {
                float excess = carSpeed - DRIB_OPT_HIGH;
                float range  = CommonValues::CAR_MAX_SPEED - DRIB_OPT_HIGH;
                speedMult = 0.5f + 0.5f * (1.0f - RS_CLAMP(excess / range, 0.0f, 1.0f));
            }
            speedMult = RS_CLAMP(speedMult, 0.5f, 1.0f);

            // Combine. Gate with sustain so you don't get instant payout.
            float base = prox * centeredLat * centeredFwd * rel01;
            float reward = base * (0.2f + 0.8f * sustain01) * speedMult;

            // Tiny floor for very short holds
            if (sustainedTime[id] < 0.06f) {
                reward *= (sustainedTime[id] / 0.06f);
            }

            return RS_CLAMP(reward * rewardScale, 0.0f, 4.0f);
        }
    };

    // ============================================================================
    // DribbleMasteryReward (Rev 4.0: ~3x lower pay, taper to ~0, pay-scaled relative-closing penalty; Rev 3.0: car-frame carry geometry, speed band 1700-2200, threat-weighted additive
    // opponent penalty, flick-ready bonus, opp-touch cooldown, long-carry taper)
    // - Only counts dribbles when ball is near arena ground (absolute Z)
    // - Rev 5.0 (2026-11-08, audit docs/reward_scenarios/DribbleMasteryReward_2026-11-08.md, findings 1 + circling):
    //     * direction factor has NO floor: a carry not moving toward the opponent net pays 0 (was 0.5x). Circling and
    //       own-goal-ward carries stop paying; full pay still needs ball-velocity . to-goal >= 0.4.
    //     * field-position factor: pay x (0.3 at our own goal back .. 1.0 within 2500 uu of the opponent goal back, linear
    //       in the ball's distance to the opponent goal back). A carry now pays more the closer it gets to a shot.
    //   Not changed (deferred audit fixes): the carry reset on any broken gate (hysteresis) and the quality-blind boost term.
    // - (Rev 4 and earlier) If dribble is NOT moving toward opponent's net, reduce reward by half
    // ============================================================================
    class DribbleMasteryReward : public Reward {
    private:
        // --- Car-frame carry geometry (Rev 3.0) ---
        // Measured in the car's own frame (forward/right/up) from the car origin, using the Dominus
        // hitbox (Arena::AddCar default; same constants as FlickReward): roof top is 15.75 + 16.9 =
        // 32.65 uu above the origin, so a ball resting on the roof has its centre ~124 uu up the car's
        // up axis (32.65 + ball radius 91.25). Dodge Torque Lab: flat cars sit lower, Octane ~8 uu
        // higher, so the band is deliberately loose rather than body-exact.
        float minCarUp        = 100.0f;   // ball centre this far up the car's up axis (rest ~124)
        float maxCarUp        = 230.0f;   // allow small pops
        float minCarFwd       = -70.0f;   // roof spans fwd -56..74 from origin
        float maxCarFwd       = 150.0f;   // hood carries
        float maxCarLat       = 75.0f;    // half-width is 43 + slop
        float maxRelSpeed     = 500.0f;   // |ballVel - carVel| gate: carry, not a bounce/push
        float minSpeed        = 800.0f;   // must be moving

        // absolute arena-ground threshold (only count dribbles when ball is low)
        float groundMaxZ = 600.0f;

        // flick-ready window (Dodge Torque Lab: normal flick wants ball 100-180 above CM, -20..120 ahead,
        // and near-zero relative velocity so the dodge kick/spin are the only thing moving the ball)
        float flickFwdLo = 0.0f,   flickFwdHi = 120.0f;
        float flickUpLo  = 110.0f, flickUpHi  = 200.0f;
        float flickRelSpeedMax = 150.0f;
        float flickBonusMult   = 0.25f;   // baseContribution *= (1 + this) while flick-ready

        float sustainRefSec = 2.f;
        float minActiveTime = 0.40f;   // < this: almost no shaping
        float entryBonus = 0.03f;
        // Rev 4.0: pay scale cut ~3x (scenario test 2026-10-16: a 12 s perfect carry paid ~1040 weighted vs GoalReward 500).
        float shapingCoeff = 0.08f;    // was 0.25 -> saturated shaping 0.08*4*1.25 = 0.4/step
        float rewardScale = 1.0f;

        // Farm guard: a carry that never ends pays ~1/step forever. Positive pay tapers to a floor.
        float taperStartSec = 6.0f;
        float taperEndSec   = 10.0f;
        float taperFloor    = 0.02f;   // ~zero: a carry that never ends stops paying

        // After an opponent touches the ball, a carry can't (re)start for this long.
        float oppTouchCooldownSec = 0.75f;

        // Rev 5.0 field-position factor: distance of the ball to the opponent goal back.
        float fieldMinMult = 0.3f;                               // at our own goal back
        float fieldFarDist = 2.0f * CommonValues::BACK_NET_Y;    // own goal back -> opponent goal back (12000)
        float fieldNearDist = 2500.0f;                           // at or inside this: full pay

        // Opponent pressure: ADDITIVE per-step penalty, measured opponent -> ball.
        float opponentDangerRange = 600.0f;
        float maxOpponentPenalty  = 0.5f;     // fixed part, at 0 UU with a fully-threatening opponent
        float opponentPayMult     = 2.0f;     // plus this x the step's positive pay, so an established carry is still net-negative under pressure
        float minThreatScale      = 0.6f;     // a stationary/not-facing opponent still costs 60%
        float threatClosingRef    = 1400.0f;  // uu/s of opponent closing speed ON THE BALL (relative to ball) for full threat

        mutable std::map<uint32_t, float> sustainedTime;
        mutable std::map<uint32_t, bool>  wasOnCar;
        PerPlayerCooldown oppTouchCooldown;

        void EndCarry(uint32_t id) const {
            sustainedTime.erase(id);
            wasOnCar[id] = false;
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            sustainedTime.clear();
            wasOnCar.clear();
            oppTouchCooldown.Reset();
        }

        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override {
            uint32_t id = player.carId;
            float    dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

            // Opponent touch -> start/refresh cooldown and kill the carry.
            for (const auto& opponent : state.players) {
                if (opponent.team != player.team && opponent.ballTouchedStep) {
                    oppTouchCooldown.Trigger(id, oppTouchCooldownSec);
                    break;
                }
            }
            if (oppTouchCooldown.TickAndCheck(id, dt)) { EndCarry(id); return 0.0f; }

            if (state.ball.pos.z > groundMaxZ || !player.isOnGround) { EndCarry(id); return 0.0f; }

            // ---- Car-frame ball position ----
            Vec rel = state.ball.pos - player.pos;
            float lf = rel.Dot(player.rotMat.forward);
            float lr = rel.Dot(player.rotMat.right);
            float lu = rel.Dot(player.rotMat.up);

            if (lu < minCarUp || lu > maxCarUp ||
                lf < minCarFwd || lf > maxCarFwd ||
                fabsf(lr) > maxCarLat) { EndCarry(id); return 0.0f; }

            float speed = player.vel.Length();
            if (speed < minSpeed) { EndCarry(id); return 0.0f; }

            // Carry, not a bounce/push: ball must be moving with the car.
            Vec relVel = state.ball.vel - player.vel;
            float relSpeed = relVel.Length();
            if (relSpeed > maxRelSpeed) { EndCarry(id); return 0.0f; }

            // ---- Opponent pressure (additive, threat-weighted) ----
            float opponentThreat = 0.0f;   // closeness x threat scale, 0..1
            for (const auto& opponent : state.players) {
                if (opponent.team == player.team || opponent.isDemoed) continue;
                Vec toBall = state.ball.pos - opponent.pos;
                float d = toBall.Length();
                if (d >= opponentDangerRange) continue;
                float closeness = 1.0f - d / opponentDangerRange;
                float closing = 0.0f, facing = 0.0f;
                if (d > 1e-3f) {
                    Vec dir = toBall / d;
                    closing = RS_CLAMP((opponent.vel - state.ball.vel).Dot(dir) / threatClosingRef, 0.0f, 1.0f);
                    facing  = RS_CLAMP(opponent.rotMat.forward.Dot(dir), 0.0f, 1.0f);
                }
                float threat = RS_CLAMP(0.6f * closing + 0.4f * facing, 0.0f, 1.0f);
                float scale = minThreatScale + (1.0f - minThreatScale) * threat;
                opponentThreat = RS_MAX(opponentThreat, closeness * scale);
            }

            // ---- Valid carry state ----
            float reward = 0.0f;
            if (!wasOnCar[id]) {
                reward += entryBonus;
                sustainedTime[id] = 0.0f;
            }
            wasOnCar[id] = true;

            sustainedTime[id] += dt;
            float t = sustainedTime[id];

            if (t < minActiveTime) {
                return (reward - opponentThreat * (maxOpponentPenalty + opponentPayMult * reward)) * rewardScale;
            }

            // ---- Shaping ----
            // Centering over the roof (lateral) and a forward band that favors centred-to-slightly-
            // forward balls, which is also where a flick wants it.
            float centering = 1.0f - RS_MIN(1.0f, fabsf(lr) / maxCarLat);
            float fwdQuality;
            if (lf >= -10.0f && lf <= 110.0f) fwdQuality = 1.0f;
            else if (lf < -10.0f) fwdQuality = RS_CLAMP((lf - minCarFwd) / (-10.0f - minCarFwd), 0.0f, 1.0f);
            else                  fwdQuality = RS_CLAMP((maxCarFwd - lf) / (maxCarFwd - 110.0f), 0.0f, 1.0f);

            // Carrying quality: low relative velocity (1 at rest relative to car, 0 at maxRelSpeed).
            float relFactor = 1.0f - RS_CLAMP(relSpeed / maxRelSpeed, 0.0f, 1.0f);

            // Speed band: 1.0x in [1700, 2200]; ramps 0.4x@800 -> 1.0x@1700; falls to 0.5x@2300.
            static constexpr float DRIB_OPT_LOW  = 1700.0f;
            static constexpr float DRIB_OPT_HIGH = CommonValues::SUPERSONIC_THRESHOLD;
            float speedMult;
            if (speed >= DRIB_OPT_LOW && speed <= DRIB_OPT_HIGH) {
                speedMult = 1.0f;
            } else if (speed < DRIB_OPT_LOW) {
                float f = (speed - minSpeed) / (DRIB_OPT_LOW - minSpeed);
                speedMult = 0.4f + 0.6f * RS_CLAMP(f, 0.0f, 1.0f);
            } else {
                float excess = speed - DRIB_OPT_HIGH;
                float range  = CommonValues::CAR_MAX_SPEED - DRIB_OPT_HIGH;
                speedMult = 0.5f + 0.5f * (1.0f - RS_CLAMP(excess / range, 0.0f, 1.0f));
            }
            speedMult = RS_CLAMP(speedMult, 0.4f, 1.0f);

            float base = centering * fwdQuality * (0.5f + 0.5f * relFactor) * speedMult;  // 0..1

            float sustainNorm = RS_MIN(2.0f, t / sustainRefSec);
            float durationFactor = sustainNorm * sustainNorm;
            float baseContribution = base * shapingCoeff * durationFactor;

            // Flick-ready window: tight geometry + near-zero relative velocity.
            bool flickReady = (lf >= flickFwdLo && lf <= flickFwdHi &&
                               lu >= flickUpLo && lu <= flickUpHi &&
                               relSpeed <= flickRelSpeedMax);
            if (flickReady) baseContribution *= (1.0f + flickBonusMult);

            float longDribbleBoostPerSec = 0.0f;
            if (t > 2.0f) longDribbleBoostPerSec = 1.0f;
            else if (t > 1.0f) longDribbleBoostPerSec = 0.5f;
            else if (t > 0.6f) longDribbleBoostPerSec = 0.2f;

            reward += baseContribution + longDribbleBoostPerSec * dt;

            // Farm guard: taper positive pay on endless carries.
            if (t > taperStartSec) {
                float u = RS_CLAMP((t - taperStartSec) / (taperEndSec - taperStartSec), 0.0f, 1.0f);
                reward *= 1.0f - (1.0f - taperFloor) * u;
            }

            // Direction: 0x (away/sideways) -> 1.0x (toward opponent net, dot >= 0.4) (Rev 5.0; was 0.5x -> 1.0x).
            Vec opponentGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
            Vec toOpp = opponentGoal - state.ball.pos;
            float toOppLen = toOpp.Length();
            float towardOpp = 0.0f;
            if (toOppLen > 1e-5f) {
                Vec toOppN = toOpp / toOppLen;
                float ballSpeed = state.ball.vel.Length();
                towardOpp = (ballSpeed > 1e-3f) ? state.ball.vel.Normalized().Dot(toOppN)
                                                : player.rotMat.forward.Dot(toOppN);
            }
            // Rev 5.0: no floor. Sideways/away pays 0, full pay from dot >= 0.4.
            float dirMultiplier = RS_CLAMP(towardOpp / 0.4f, 0.0f, 1.0f);
            reward *= dirMultiplier;

            // Rev 5.0: more pay the closer the ball is to the opponent net (0.3x at our own goal back -> 1.0x within 2500 uu).
            float fieldFrac = RS_CLAMP((fieldFarDist - toOppLen) / (fieldFarDist - fieldNearDist), 0.0f, 1.0f);
            reward *= fieldMinMult + (1.0f - fieldMinMult) * fieldFrac;

            // Penalty = fixed part + multiple of this step's (final, positive) pay, so it scales with
            // what the bot stands to lose and can push an established carry net-negative.
            reward -= opponentThreat * (maxOpponentPenalty + opponentPayMult * RS_MAX(0.0f, reward));
            return reward * rewardScale;
        }
    };

} // namespace RLGC
