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

// Ported from RLGymBot_v3 (CompleteMechanicsRewards.h). Logic unchanged unless noted.

namespace RLGC {

// CatchBallOnCarReward
// Rewards soft "catch" touches that slow a fast ball and keep it close & in
// front of the car, promoting possession instead of booming it away.
// 2025-12-08 - Rev 2 - Added positional & speed-shaping. (NJH)
// 2025-12-14 - Rev (tuned) - emphasize catches that end up "on top" of the car:
//   * tighter on-top geometry (height + horiz radius)
//   * larger bonus when ball is effectively carried (low relative velocity)
//   * more weight to slowdown while retaining alignment & speed penalties
class CatchBallOnCarReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep)
            return 0.0f;
        if (!state.prev)
            return 0.0f;

        // Previous and current ball speeds
        float prevBallSpeed = state.prev->ball.vel.Length();
        float ballSpeed = state.ball.vel.Length();

        // Only treat as "catch" if ball was moving reasonably fast (but allow somewhat lower)
        if (prevBallSpeed < 600.0f)
            return 0.0f;

        // Slowdown factor: bigger slowdown = better catch (0..1)
        float slowdown = RS_CLAMP((prevBallSpeed - ballSpeed) / prevBallSpeed, 0.0f, 1.0f);
        if (slowdown <= 0.0f)
            return 0.0f;

        // Ball relative to car
        Vec rel = state.ball.pos - player.pos;
        float horizDist = Vec(rel.x, rel.y, 0).Length();
        float heightAboveCar = rel.z;

        // Must be roughly in front of the car
        Vec forward = player.rotMat.forward;
        float frontAlign = forward.Dot(rel.Normalized()); // 1 = directly in front
        if (frontAlign < 0.4f)
            return 0.0f; // require stronger frontal alignment than before

        // Signed forward projection of the ball relative to car centre.
        // Positive  = ball is over the hood/front half.
        // Negative  = ball is over the trunk/rear half.
        // ~60 UU is roughly half the Octane hitbox length.
        float forwardComp = forward.Dot(rel);

        // On-top: tight geometry AND ball must be on the front half of the roof
        bool isOnTop = (heightAboveCar > 50.0f && heightAboveCar < 140.0f
                     && horizDist < 100.0f && forwardComp > 0.0f);

        // Near-top: slightly looser — allow up to 60 UU behind centre (mid-roof) but not trunk
        bool isNearTop = (heightAboveCar > 30.0f && heightAboveCar < 200.0f
                       && horizDist < 220.0f && forwardComp > -60.0f);

        // Penalize if ball leaves too fast even after "catch"
        float baseCap = 1200.0f;
        float speedPenaltyFactor = 1.0f;
        if (ballSpeed > baseCap) {
            speedPenaltyFactor = RS_CLAMP(1.0f - (ballSpeed - baseCap) / 1000.0f, 0.0f, 1.0f);
            if (speedPenaltyFactor <= 0.0f) return 0.0f;
        }

        // Alignment factor: prefer catches directly in front
        float alignFactor = 0.6f + 0.4f * RS_CLAMP(frontAlign, 0.0f, 1.0f); // [0.6..1.0]

        // Relative velocity between ball and car: small => ball is effectively "on" the car
        Vec relVel = state.ball.vel - player.vel;
        float relSpeed = relVel.Length();

        // Front-half scale: linearly ramps from 0.5x at car centre (forwardComp=0)
        // to 1.5x at ~120 UU ahead (well over the hood). Rear-half is already gated
        // out by isOnTop/isNearTop, but clamp to [0.5, 1.5] for safety.
        float frontHalfScale = RS_CLAMP(0.5f + forwardComp / 120.0f, 0.5f, 1.5f);

        // On-top carry bonus if relative speed is small, scaled by front-half position
        float carryBonus = 0.0f;
        if (isOnTop && relSpeed < 250.0f) {
            carryBonus = 4.0f * frontHalfScale; // up to 6.0 for hood catches, 2.0 at centre
        }
        else if (isNearTop && relSpeed < 350.0f) {
            carryBonus = 2.0f * frontHalfScale; // up to 3.0 for front near-top
        }

        // Base catch reward: emphasize slowdown but scale by alignment and speed penalty
        float baseReward = 5.0f * slowdown * alignFactor * speedPenaltyFactor; // up to ~5

        // Extra small bonus for being on-top even if slowdown modest
        float topProximityBonus = isOnTop ? 1.0f : (isNearTop ? 0.4f : 0.0f);

        float reward = baseReward + carryBonus + topProximityBonus;

        // Final clamp: allow bigger payouts for exceptional on-top catches but bounded
        float maxReward = isOnTop ? 15.0f : 8.0f;
        return RS_CLAMP(reward, 0.0f, maxReward);
    }
};

} // namespace RLGC
