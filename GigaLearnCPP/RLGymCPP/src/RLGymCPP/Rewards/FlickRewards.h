#pragma once
#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include "RewardMath.h"
#include "RewardTiming.h"
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <map>
#include <unordered_map>

// Ported from RLGymBot_v3 (CompleteMechanicsRewards.h: FlickReward + AdvancedFlickReward). Logic unchanged unless noted.

namespace RLGC {

// 2025-12-20 | Rev 4.0 | NJH
// - MASSIVELY increased acceleration sensitivity
// - Added directional bonuses (upward + forward acceleration)
// - Rewards "saucy" flicks with high combined acceleration vectors
// 2026-10-15 | Rev 5.0 | NJH - rebuilt from the Dodge Torque Lab research
// - FIX: the trigger used to demand the ball be OUTSIDE the carry zone on the touch step. The ball
//   moves only ~30 uu in the 4-tick step that contains the contact, so it was almost always still
//   inside the zone and the reward silently never fired. Carry time is now read from the previous
//   step's state (before this step's update).
// - A real dodge is required (isFlipping now or last step); non-dodge shoves no longer pay.
// - Scale is ball dv in uu/s, not dv/dt. The old top tiers (dv 4667 / 6667 uu/s) were unreachable;
//   the lab's best flicks exit at ~800 (normal) to ~1150-1500 (musty) uu/s.
// - Geometry gates widened to cover lab flick setups (musty: ball 140-150 uu ahead; normal: 110-140 above).
// - Dodge-torque modifiers (pre-contact state): contact-surface swing speed n.(w x r) and horizontal
//   ball-vs-car velocity match. Swing ~0 when the dodge is pressed on the contact step (lab: kick
//   ejects the ball, spin adds nothing), up to ~300 uu/s with full spin on the swinging end.
class FlickReward : public Reward {
private:
    mutable std::map<uint32_t, float> ballOnCarTime;
    float minOnCarTime = 0.25f;

    // Geometry gates (world-frame height, so car is assumed roughly level while carrying)
    float minHeightAboveCar = 40.0f;
    float maxHeightAboveCar = 170.0f;
    float maxHorizDist = 160.0f;

    // Ball velocity change on the contact step (uu/s)
    float minDv = 400.0f;
    float goodDv = 1000.0f;
    float greatDv = 1600.0f;
    float insaneDv = 2400.0f;

    float maxFlickReward = 25.0f;

    // Dominus hitbox (CAR_CONFIG_DOMINUS in CarConfig.cpp, the Arena::AddCar default); local frame: forward, right, up.
    // Half-extents = size/2 (130.427, 85.7799, 33.8); offset is hitbox centre from the car origin.
    static constexpr float HB_OFF_X = 9.0f, HB_OFF_Z = 15.75f;
    static constexpr float HB_HALF_X = 65.2135f, HB_HALF_Y = 42.88995f, HB_HALF_Z = 16.9f;

    // Speed (uu/s) at which the contact point on the car surface is rotating INTO the ball.
    // > 0 means the dodge spin is swinging that end of the car toward the ball (lab: n . (w x r)).
    static float SurfaceSwingSpeed(const Player& pre, const Vec& ballPos) {
        Vec rel = ballPos - pre.pos;
        float lf = rel.Dot(pre.rotMat.forward);
        float lr = rel.Dot(pre.rotMat.right);
        float lu = rel.Dot(pre.rotMat.up);

        float px = RS_CLAMP(lf - HB_OFF_X, -HB_HALF_X, HB_HALF_X) + HB_OFF_X;
        float py = RS_CLAMP(lr, -HB_HALF_Y, HB_HALF_Y);
        float pz = RS_CLAMP(lu - HB_OFF_Z, -HB_HALF_Z, HB_HALF_Z) + HB_OFF_Z;

        Vec n = pre.rotMat.forward * (lf - px) + pre.rotMat.right * (lr - py) + pre.rotMat.up * (lu - pz);
        float nLen = n.Length();
        n = (nLen > 1e-3f) ? n / nLen : pre.rotMat.up;

        Vec r = pre.rotMat.forward * px + pre.rotMat.right * py + pre.rotMat.up * pz;
        return n.Dot(pre.angVel.Cross(r));
    }

public:
    virtual void Reset(const GameState& initialState) override {
        ballOnCarTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();

        bool ballOnCar =
            (heightAboveCar > minHeightAboveCar && heightAboveCar < maxHeightAboveCar && horizDist < maxHorizDist);

        auto& tOnCar = ballOnCarTime[player.carId];
        const float carriedFor = tOnCar; // carry time as of the previous step's state
        if (ballOnCar && player.isOnGround) {
            tOnCar += dt;
        }
        else {
            if (!ballOnCar) tOnCar = 0.0f;
        }

        // Flick trigger: touch while dodging, after a real carry
        if (player.ballTouchedStep && carriedFor >= minOnCarTime && state.prev
            && player.index >= 0 && player.index < (int)state.prev->players.size()) {

            const Player& pre = state.prev->players[player.index];
            bool dodging = player.isFlipping || pre.isFlipping;

            Vec dv = state.ball.vel - state.prev->ball.vel;
            float dvLen = dv.Length();

            if (dodging && dvLen >= minDv) {
                // Base reward from ball velocity change (progressive scaling)
                float accelReward;
                if (dvLen >= insaneDv) {
                    accelReward = maxFlickReward;
                }
                else if (dvLen >= greatDv) {
                    float t = (dvLen - greatDv) / (insaneDv - greatDv);
                    accelReward = maxFlickReward * (0.75f + 0.25f * t); // 75-100%
                }
                else if (dvLen >= goodDv) {
                    float t = (dvLen - goodDv) / (greatDv - goodDv);
                    accelReward = maxFlickReward * (0.45f + 0.30f * t); // 45-75%
                }
                else {
                    float t = (dvLen - minDv) / (goodDv - minDv);
                    accelReward = maxFlickReward * (0.15f + 0.30f * t); // 15-45%
                }

                // Direction bonuses (power / pop / diagonal)
                float fwdDv = dv.Dot(player.rotMat.forward);
                float upDv = dv.z;
                float sauceMultiplier = 1.0f;
                sauceMultiplier += 0.4f * RS_CLAMP((fwdDv - 400.0f) / 800.0f, 0.0f, 1.0f);
                sauceMultiplier += 0.35f * RS_CLAMP((upDv - 300.0f) / 600.0f, 0.0f, 1.0f);
                if (fwdDv > 350.0f && upDv > 250.0f) sauceMultiplier += 0.3f;

                // Dodge-torque modifier: was the contact end swinging into the ball before contact?
                float swing = SurfaceSwingSpeed(pre, state.prev->ball.pos);
                float swingFactor = 0.85f + 0.30f * RS_CLAMP(swing / 300.0f, 0.0f, 1.0f); // 0.85-1.15

                // Carry quality: ball should be moving with the car horizontally before the dodge
                Vec relVel = state.prev->ball.vel - pre.vel;
                float hRel = Vec(relVel.x, relVel.y, 0).Length();
                float matchFactor = 0.7f + 0.3f * (1.0f - RS_CLAMP(hRel / 300.0f, 0.0f, 1.0f)); // 0.7-1.0

                float reward = accelReward * sauceMultiplier * swingFactor * matchFactor;

                // Speed gate (require actual speed increase)
                float dSpeed = state.ball.vel.Length() - state.prev->ball.vel.Length();
                float speedGate = RS_CLAMP(dSpeed / 600.0f, 0.0f, 1.0f);
                reward *= (0.25f + 0.75f * speedGate);

                tOnCar = 0.0f; // consume the carry; needs a fresh 0.25s to pay again
                return reward;
            }
        }

        // Dribble sustain shaping (minimal)
        if (ballOnCar && player.isOnGround && tOnCar > 0.05f) {
            return 0.12f;
        }

        return 0.0f;
    }
};

// 2025-01-04 | Advanced Flick Reward | NJH
// - Targets high-speed flicks (>2200 UU/s, ideally 2500+)
// - Rewards 45° flicks (diagonal), 90° flicks (sideways), and mixed angles
// - Heavy emphasis on ball speed and directional control
// - Penalizes low-speed flicks to force aggressive play

class AdvancedFlickReward : public Reward {
private:
    mutable std::map<uint32_t, float> ballOnCarTime;
    float minOnCarTime = 0.25f;

    // Geometry gates
    float minHeightAboveCar = 40.0f;
    float maxHeightAboveCar = 155.0f;
    float maxHorizDist = 120.0f;

    // Speed thresholds (UU/s)
    float minFlickSpeed = 1600.0f;      // Minimum viable flick
    float goodFlickSpeed = 2200.0f;     // Good flick target
    float greatFlickSpeed = 2500.0f;    // Great flick
    float insaneFlickSpeed = 3000.0f;   // Insane flick

    float maxFlickReward = 35.0f;       // Increased for high-speed emphasis

    // Angle targets (degrees from forward)
    // 45° = diagonal, 90° = sideways, 135° = back-diagonal
    float angleTolerance = 30.0f;       // ±30° tolerance around target angles

public:
    virtual void Reset(const GameState& initialState) override {
        ballOnCarTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();

        bool ballOnCar =
            (heightAboveCar > minHeightAboveCar && heightAboveCar < maxHeightAboveCar && horizDist < maxHorizDist);

        auto& tOnCar = ballOnCarTime[player.carId];
        if (ballOnCar && player.isOnGround) {
            tOnCar += dt;
        }
        else {
            if (!ballOnCar) tOnCar = 0.0f;
        }

        // Flick trigger
        if (player.ballTouchedStep && !ballOnCar && tOnCar >= minOnCarTime) {
            if (!state.prev) { tOnCar = 0.0f; return 0.0f; }

            // Ball velocity AFTER flick
            Vec ballVel = state.ball.vel;
            float ballSpeed = ballVel.Length();

            // ACCELERATION: Calculate velocity change imparted to ball (vector magnitude, not scalar diff)
            // Scalar (ballSpeed - prevBallSpeed) = 0 for side flicks at constant speed — use |Δvel| instead
            Vec dv = state.ball.vel - state.prev->ball.vel;
            float velocityChange = dv.Length();

            // SPEED GATE: Reward based on acceleration, not absolute final speed
            if (velocityChange < 200.0f) {
                // Weak flick - minimal acceleration
                tOnCar = 0.0f;
                return 0.0f; // No penalty, just no reward for weak acceleration
            }

            // Acceleration-based reward (prioritize how much speed was ADDED)
            float speedReward = 0.0f;
            const float minAccel = 200.0f;     // Minimum acceleration to reward
            const float goodAccel = 1000.0f;   // Good acceleration
            const float greatAccel = 1500.0f;  // Great acceleration
            const float insaneAccel = 2000.0f; // Insane acceleration

            if (velocityChange >= insaneAccel) {
                speedReward = maxFlickReward;
            }
            else if (velocityChange >= greatAccel) {
                float t = (velocityChange - greatAccel) / (insaneAccel - greatAccel);
                speedReward = maxFlickReward * (0.70f + 0.30f * t); // 70-100%
            }
            else if (velocityChange >= goodAccel) {
                float t = (velocityChange - goodAccel) / (greatAccel - goodAccel);
                speedReward = maxFlickReward * (0.45f + 0.25f * t); // 45-70%
            }
            else {
                // minAccel to goodAccel
                float t = (velocityChange - minAccel) / (goodAccel - minAccel);
                speedReward = maxFlickReward * (0.20f + 0.25f * t); // 20-45%
            }

            // DIRECTIONAL ANALYSIS
            Vec carForward = player.rotMat.forward;
            Vec carRight = player.rotMat.right;
            Vec upVector = Vec(0, 0, 1);

            // Normalize ball velocity direction
            Vec ballDir = (ballSpeed > 0.1f) ? ballVel / ballSpeed : carForward;

            // Calculate angles from car orientation
            float forwardComponent = ballDir.Dot(carForward);
            float rightComponent = ballDir.Dot(carRight);
            float upComponent = ballDir.Dot(upVector);

            // Angle from forward (0° = straight, 90° = sideways, 180° = backward)
            float horizontalAngle = std::atan2(rightComponent, forwardComponent) * 180.0f / 3.14159f;
            float verticalAngle = std::atan2(upComponent,
                std::sqrt(forwardComponent * forwardComponent + rightComponent * rightComponent)) * 180.0f / 3.14159f;

            // TARGET ANGLE REWARDS
            // Target angles: 45° (diagonal), 90° (sideways), 135° (back-diagonal)
            float angleBonus = 0.0f;

            auto CheckAngle = [&](float targetAngle) -> float {
                float diff = std::abs(std::abs(horizontalAngle) - std::abs(targetAngle));
                if (diff > 180.0f) diff = 360.0f - diff;

                if (diff < angleTolerance) {
                    return 1.0f - (diff / angleTolerance) * 0.3f; // 100% to 70%
                }
                return 0.0f;
                };

            // Reward for hitting target angles
            float angle45 = CheckAngle(45.0f);      // Diagonal
            float angle90 = CheckAngle(90.0f);      // Sideways
            float angle135 = CheckAngle(135.0f);    // Back-diagonal

            angleBonus = RS_MAX(RS_MAX(angle45, angle90), angle135);

            // Upward angle bonus (pop flicks)
            if (upComponent > 0.3f) {
                float popBonus = RS_CLAMP(upComponent, 0.0f, 1.0f) * 0.25f;
                angleBonus += popBonus; // Up to +25%
            }

            // Apply angle bonus to speed reward
            float directionMultiplier = 1.0f + angleBonus; // 1.0x to 1.25x+

            float reward = speedReward * directionMultiplier;

            // GOAL DIRECTION MULTIPLIER
            // Slightly higher scale than DribbleFlickRewards (0.30f) since AdvancedFlick targets harder shots
            Vec oppGoalPos = (player.team == Team::BLUE)
                ? Vec(0.0f, CommonValues::BACK_WALL_Y, CommonValues::BALL_RADIUS)
                : Vec(0.0f, -CommonValues::BACK_WALL_Y, CommonValues::BALL_RADIUS);
            Vec toGoal = (oppGoalPos - state.ball.pos).Normalized();
            float goalAlign = RS_MAX(0.0f, ballDir.Dot(toGoal));
            const float GOAL_DIR_SCALE = 0.40f;
            reward *= (1.0f + GOAL_DIR_SCALE * goalAlign);

            // OPPONENT EVASION MULTIPLIER
            // Rewards flicking ball away from opponents — the harder they are to reach it, the better
            float totalEvasion = 0.0f;
            int numOpps = 0;
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                Vec ballToOpp = (opp.pos - state.ball.pos).Normalized();
                float evasion = RS_MAX(0.0f, -ballDir.Dot(ballToOpp)); // positive = ball moving away from opp
                totalEvasion += evasion;
                numOpps++;
            }
            if (numOpps > 0) {
                float avgEvasion = totalEvasion / numOpps;
                const float EVASION_SCALE = 0.35f;
                reward *= (1.0f + EVASION_SCALE * avgEvasion);
            }

            // Consistency bonus (if ball is moving cleanly without spin)
            float angVelMag = state.ball.angVel.Length();
            if (angVelMag < 2.0f) {
                reward *= 1.15f; // +15% for clean flicks
            }

            tOnCar = 0.0f;
            return reward;
        }

        // Dribble sustain (minimal)
        if (ballOnCar && player.isOnGround && tOnCar > 0.05f) {
            return 0.05f; // Small reward to encourage ball possession
        }

        return 0.0f;
    }
};

} // namespace RLGC
