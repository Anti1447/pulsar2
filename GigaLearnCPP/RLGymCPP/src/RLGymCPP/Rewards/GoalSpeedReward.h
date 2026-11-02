#pragma once
// ============================================================================
// GoalSpeedReward.h
//
// Rev 1.0 - 2026-08-28 (NJH)
//
// PURPOSE:
//   Rewards the individual player who scored, scaled by how fast the ball was
//   moving when it crossed the line. Faster goals pay more, via an x^2 curve
//   normalized between two configurable kph caps. Supplementary bonus signal
//   layered on top of the flat GoalReward (CommonRewards.h) — not a
//   replacement for it.
//
// CREDIT LOGIC (the important design decision):
//   Uses the engine's player.eventState.goal flag rather than tracking touches
//   ourselves. Verified against RocketSim's GameEventTracker
//   (RocketSim/src/Sim/GameEventTracker/GameEventTracker.h/.cpp): the scorer
//   callback that sets eventState.goal only fires if a car touched the ball
//   within config.goalMaxTouchTime (default 4.0f seconds, unmodified in this
//   project — EnvSet.cpp constructs it as `new GameEventTracker({})`). So a
//   stale, untouched-in-a-while own-goal-adjacent deflection correctly credits
//   nobody, with no extra staleness tracking needed here.
//
// SPEED CURVE:
//   reward = (clamp(speedKph, lowerCapKph, speedCapKph)^2 - lowerCapKph^2)
//          / (speedCapKph^2 - lowerCapKph^2)
//   - At or below lowerCapKph: 0.0
//   - At or above speedCapKph: 1.0
//   - In between: x^2 ramp, so the top of the speed range is worth
//     disproportionately more than the middle.
//
// W&B: GoalSpeed/AvgGoalSpeedKph, GoalSpeed/AvgReward via GetAndResetMetrics()
// ============================================================================

#include "Reward.h"
#include <atomic>
#include "../Math.h"       // RS_CLAMP (transitively, via RocketSim/Framework.h)

namespace RLGC {

// Ball speed in kph (uu/s * 0.036). Ported from v3's CommonRewards.h, which pulsar2 does not have.
// If another ported header needs it, include this one rather than redefining it.
inline float BallSpeedKph(const GameState& state) {
    return state.ball.vel.Length() * 0.036f;
}

class GoalSpeedReward : public Reward {
public:
    struct Config {
        // Goal speed (kph) at or above which the reward saturates to 1.0.
        float speedCapKph = 140.0f;

        // Goal speed (kph) at or below which the reward saturates to 0.0.
        float lowerCapKph = 80.0f;
    };

    Config config;

    GoalSpeedReward() : config() {}
    explicit GoalSpeedReward(const Config& cfg) : config(cfg) {}

private:
    inline static std::atomic<int>   s_fireCount{ 0 };
    inline static std::atomic<float> s_rewardSum{ 0.0f };
    inline static std::atomic<float> s_speedKphSum{ 0.0f }; // sum of RAW (unclamped) goal speeds

public:
    struct Metrics { float avgReward = 0.0f; float avgGoalSpeedKph = 0.0f; };
    static Metrics GetAndResetMetrics() {
        int   count   = s_fireCount.exchange(0);
        float sum     = s_rewardSum.exchange(0.0f);
        float spdSum  = s_speedKphSum.exchange(0.0f);
        return { count > 0 ? sum / count : 0.0f, count > 0 ? spdSum / count : 0.0f };
    }

    virtual void Reset(const GameState& /*initialState*/) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool /*isFinal*/) override {
        if (!state.goalScored) return 0.0f;
        if (!player.eventState.goal) return 0.0f; // not the credited scorer

        float speedKph = BallSpeedKph(state);
        float clamped  = RS_CLAMP(speedKph, config.lowerCapKph, config.speedCapKph);

        float lo2 = config.lowerCapKph * config.lowerCapKph;
        float hi2 = config.speedCapKph * config.speedCapKph;
        float reward = (hi2 > lo2) ? (clamped * clamped - lo2) / (hi2 - lo2) : 0.0f;

        s_fireCount.fetch_add(1);
        float prev = s_rewardSum.load();
        while (!s_rewardSum.compare_exchange_weak(prev, prev + reward)) {}
        prev = s_speedKphSum.load();
        while (!s_speedKphSum.compare_exchange_weak(prev, prev + speedKph)) {} // raw, unclamped

        return reward;
    }
};

} // namespace RLGC
