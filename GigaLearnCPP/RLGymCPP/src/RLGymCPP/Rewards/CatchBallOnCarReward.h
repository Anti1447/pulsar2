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
// 2026-11-08 - Rev 3 (audit docs/reward_scenarios/CatchBallOnCarReward_2026-11-08.md): anti-farm gates, pay shape unchanged.
//   Audit finding: the touch flag is true on every step of a carry and the old test was `slowdown > 0`, so a carry that
//   decelerated by 2 uu/s paid 5.34 per step (~80 raw/s). Three gates now make a pay mean "a separate, real catch":
//   1. RISING EDGE: the player must not have touched the ball on the previous step.
//   2. REAL SLOWDOWN: the ball must lose >= 25% of its speed AND >= 300 uu/s in the step.
//   3. INCOMING BALL: the ball must have been >= 250 uu from the car on the previous step (not already on it).
//   Plus a COOLDOWN: after a paid catch, no pay for 1.5 s, and not until the ball has been >= 300 uu from the car
//   since (re-arm). The cooldown only starts on a PAID catch (a gated-out touch does not burn it).
// 2026-11-08 - Rev 4 (audit fixes 4 + 5): geometry and clears.
//   4. CAR-FRAME GEOMETRY: the ball must end in DribbleMastery's carry band (up 100..230, fwd -70..150, |lat| <= 75) in the
//      CAR's frame. The world-frame `frontAlign >= 0.4` gate (which paid 0 over the roof centre and 6.9 at the hood edge) is gone;
//      position now only scales the pay smoothly (quality = centering x forward band, as in DribbleMastery).
//   5. NO CLEAR/BLOCK PAYOUT: the near-top branch and the ball-speed penalty are gone. The ball must leave the touch moving
//      with the car (relative speed <= 350) and not faster than it (<= car speed + 250), so a ball sent away does not pay.
// 2026-11-08 (night) - Rev 5 (re-audit: the reward was dead, Possession Catch Rate ~1e-6): gates moved to the CAR-RELATIVE frame.
//   A. incoming speed and slowdown are measured on ball-minus-car velocity (a ball already moving with the car is not "sped up" any more):
//      previous relative speed >= 400 uu/s and the touch removes >= 35% of it.
//   B. the `previous distance >= 250 uu` gate is gone (needs ~1800 uu/s closing speed at 15 Hz; rejected every gentle catch). The rising
//      edge + relative incoming speed say "new contact"; the 1.5 s cooldown and the 300 uu re-arm remain the farm defence.
//   C. exit relative speed <= 500 (== the carry gate), carry bonus 4 under 250, 2 from 250 to 500. Car speed + 250 cap kept.
//   Pay: 5 x slowdown x (0.6..1.0) + carry bonus 4 (rel < 250) or 2 (250..350), each x (0.5..1.0), + 1.0; cap 8 (was 15 / 8).
class CatchBallOnCarReward : public Reward {
public:
    static constexpr float MIN_INCOMING_REL_SPEED = 400.0f; // |ball - car| velocity on the previous step (Rev 5; was world ball speed 600)
    static constexpr float MIN_SLOWDOWN_FRAC = 0.35f;   // fraction of the RELATIVE incoming speed the touch must remove (Rev 5; was 0.25 world)
    static constexpr float COOLDOWN_SEC      = 1.5f;
    static constexpr float REARM_DIST        = 300.0f;  // ball must have been this far from the car since the last paid catch
    // Rev 4 on-car geometry (car frame; == DribbleMasteryReward's carry band and Telemetry/CarryCatchTelemetry.h)
    static constexpr float CATCH_MIN_UP = 100.0f, CATCH_MAX_UP = 230.0f;
    static constexpr float CATCH_MIN_FWD = -70.0f, CATCH_MAX_FWD = 150.0f;
    static constexpr float CATCH_MAX_LAT = 75.0f;
    static constexpr float CATCH_MAX_REL_SPEED = 500.0f;    // ball leaves the touch moving with the car
    static constexpr float CATCH_MAX_EXCESS_SPEED = 250.0f; // ... and not faster than the car by more than this
    static constexpr float MAX_REWARD = 8.0f;

private:
    PerPlayerCooldown cooldown;
    std::unordered_map<uint32_t, bool> needsRearm;

public:
    virtual void Reset(const GameState& initialState) override {
        cooldown.Reset();
        needsRearm.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        const uint32_t id = player.carId;
        // Tick every step (before any early return) so the cooldown expires on schedule.
        const bool coolingDown = cooldown.TickAndCheck(id, StepDt(state));
        {
            auto it = needsRearm.find(id);
            if (it != needsRearm.end() && it->second && (state.ball.pos - player.pos).Length() >= REARM_DIST)
                it->second = false;
        }

        if (!player.ballTouchedStep)
            return 0.0f;
        if (!state.prev)
            return 0.0f;
        // 1. rising edge: a continuing contact (carry, push) is not a new catch
        if (player.prev && player.prev->ballTouchedStep)
            return 0.0f;
        if (coolingDown)
            return 0.0f;
        { auto it = needsRearm.find(id); if (it != needsRearm.end() && it->second) return 0.0f; }

        // Rev 5: everything in the car-relative frame (previous step's car and ball velocities vs this step's).
        const Vec prevCarVel = player.prev ? player.prev->vel : player.vel;
        const float prevRel = (state.prev->ball.vel - prevCarVel).Length();
        const float ballSpeed = state.ball.vel.Length();
        const float curRel = (state.ball.vel - player.vel).Length();

        // Only a ball that was really closing on the car can be caught
        if (prevRel < MIN_INCOMING_REL_SPEED)
            return 0.0f;
        // Slowdown factor: the share of the relative speed the touch removed (0..1)
        float slowdown = RS_CLAMP((prevRel - curRel) / prevRel, 0.0f, 1.0f);
        if (slowdown < MIN_SLOWDOWN_FRAC)
            return 0.0f;

        // ---- Rev 4 (audit fixes 4 + 5): car-frame geometry, possession-only pay ----
        // Ball position in the car's own frame (same band as DribbleMasteryReward's carry and the Carry/Catch telemetry).
        Vec rel = state.ball.pos - player.pos;
        float lf = rel.Dot(player.rotMat.forward);
        float lr = rel.Dot(player.rotMat.right);
        float lu = rel.Dot(player.rotMat.up);
        if (lu < CATCH_MIN_UP || lu > CATCH_MAX_UP ||
            lf < CATCH_MIN_FWD || lf > CATCH_MAX_FWD ||
            fabsf(lr) > CATCH_MAX_LAT)
            return 0.0f; // not on the car: a block, a clear or a deflection, not a catch

        // The ball must leave the touch moving WITH the car, and not faster than it.
        float relSpeed = curRel;
        if (relSpeed > CATCH_MAX_REL_SPEED)
            return 0.0f;
        if (ballSpeed > player.vel.Length() + CATCH_MAX_EXCESS_SPEED)
            return 0.0f;

        // Position quality 0..1 (DribbleMastery's centering x forward band): 1 over the roof centre / hood, falling toward the edges.
        float centering = 1.0f - RS_MIN(1.0f, fabsf(lr) / CATCH_MAX_LAT);
        float fwdQuality;
        if (lf >= -10.0f && lf <= 110.0f) fwdQuality = 1.0f;
        else if (lf < -10.0f)             fwdQuality = RS_CLAMP((lf - CATCH_MIN_FWD) / (-10.0f - CATCH_MIN_FWD), 0.0f, 1.0f);
        else                              fwdQuality = RS_CLAMP((CATCH_MAX_FWD - lf) / (CATCH_MAX_FWD - 110.0f), 0.0f, 1.0f);
        float quality = centering * fwdQuality;

        // Base catch reward: the slowdown, scaled by position (0.6 .. 1.0)
        float baseReward = 5.0f * slowdown * (0.6f + 0.4f * quality); // up to 5

        // Carry bonus: ball effectively stationary on the car
        float carryBonus = 0.0f;
        if (relSpeed < 250.0f)       carryBonus = 4.0f * (0.5f + 0.5f * quality);
        else                         carryBonus = 2.0f * (0.5f + 0.5f * quality);   // 250..500

        float topProximityBonus = 1.0f;

        float out = RS_CLAMP(baseReward + carryBonus + topProximityBonus, 0.0f, MAX_REWARD);
        if (out > 0.0f) {
            cooldown.Trigger(id, COOLDOWN_SEC);
            needsRearm[id] = true;
        }
        return out;
    }
};

} // namespace RLGC
