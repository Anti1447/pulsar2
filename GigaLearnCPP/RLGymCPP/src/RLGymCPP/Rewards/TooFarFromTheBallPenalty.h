#pragma once
#include "Reward.h"
#include "../Gamestates/GameState.h"
#include "../CommonValues.h"

namespace RLGC {

// ============================================================================
// TooFarFromTheBallPenalty (2026-10-20 | Rev 1.0)
// ============================================================================
// Individual penalty (never shared with the team) for being too far from the
// ball. The distance threshold is configurable PER GAME MODE:
//   1v1: 3000uu   2v2: 4000uu   3v3: 5000uu   (constructor args)
//
// Mode is detected from the roster: max(cars per team) = 1 / 2 / 3+ .
//
// Smooth ramp (linear) centered on the threshold T with half-width W (500uu):
//   d = 3D distance car -> ball
//   d <= T - W : 0
//   d >= T + W : -1.0
//   between    : linear, exactly -0.5 at d = T
// Output range [-1, 0]; the WeightedReward weight sets the magnitude.
//
// Gates: demoed -> 0; kickoff (ball near center, vel < 100) -> 0, since kickoff
// spawns are 3000-4600uu from the ball and are not a positioning choice.
// NOT ZeroSum: individual shaping.
//
// Tension to watch: this directly opposes net-camping (good) but also pushes a
// lone defender out of net in 1v1 when the ball is deep in the opposite corner.
// The 3000uu 1v1 threshold is ~58% of field length, so the ball must be far
// away before it bites.
//
// W&B: per-reward mean for TooFarFromTheBallPenalty. Healthy: small negative
// mean, a minority of steps active (a few % to ~25%). Suspicious: always ~0
// (thresholds too loose) or large negative in 3v3 (rotation squeezes the
// back player past 5000uu - raise the 3v3 threshold).
// ============================================================================

class TooFarFromTheBallPenalty : public Reward {
public:
    float threshold1v1, threshold2v2, threshold3v3; // uu, centre of the ramp
    float rampHalfWidth;                            // uu, ramp spans [T - W, T + W]

    TooFarFromTheBallPenalty(float threshold1v1 = 3000.0f,
                             float threshold2v2 = 4000.0f,
                             float threshold3v3 = 5000.0f,
                             float rampHalfWidth = 500.0f)
        : threshold1v1(threshold1v1), threshold2v2(threshold2v2),
          threshold3v3(threshold3v3), rampHalfWidth(rampHalfWidth) {}

    float ThresholdFor(const GameState& state) const {
        int cnt[2] = {0, 0};
        for (const Player& p : state.players)
            cnt[(int)p.team]++;
        int teamSize = cnt[0] > cnt[1] ? cnt[0] : cnt[1];
        if (teamSize <= 1) return threshold1v1;
        if (teamSize == 2) return threshold2v2;
        return threshold3v3;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool /*isFinal*/) override {
        if (player.isDemoed) return 0.0f;

        // Skip kickoff: ball near center with near-zero velocity
        if (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 100.0f)
            return 0.0f;

        float T = ThresholdFor(state);
        float W = RS_MAX(rampHalfWidth, 1.0f);
        float d = (player.pos - state.ball.pos).Length();

        float t = (d - (T - W)) / (2.0f * W);
        if (t <= 0.0f) return 0.0f;
        return -RS_MIN(t, 1.0f);
    }
};

} // namespace RLGC
