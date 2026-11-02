#pragma once
#include "Reward.h"
#include "../Gamestates/GameState.h"
#include "../CommonValues.h"

namespace RLGC {

// ============================================================================
// TeammateSpacingPenalty (2026-10-22 | Rev 1.0)
// ============================================================================
// Continuous individual penalty for crowding teammates. Applies every step.
// The distance threshold is PER GAME MODE (constructor args):
//   1v1: n/a (no teammates, returns 0)   2v2: 1000uu   3v3: 1500uu
// Mode is detected from the roster: max(cars per team) = 1 / 2 / 3+ .
//
// For each living teammate at 3D distance d < T:
//   p = 1 - d / T      (0 at the threshold, 1 when overlapping)
// Result = -clamp(sum of p over teammates, 0, 1). Output range [-1, 0]; the
// WeightedReward weight sets the magnitude. Summing means being close to BOTH
// teammates in 3v3 hurts more than being close to one.
//
// Gates: self demoed -> 0; teammate demoed is skipped; kickoff (ball near
// centre, vel < 100) -> 0, since kickoff spawns are not a positioning choice.
// NOT ZeroSum: individual shaping (both crowding cars are penalized).
//
// Tension to watch: a legitimate double-commit / pass / backboard follow-up
// puts two cars close together; the penalty is small there (weight is low) but
// it will also pressure the supporting car away from the ball carrier.
//
// W&B: per-reward mean for TeammateSpacingPenalty. Healthy: small negative
// mean, active on a minority of steps (~5-25%). Suspicious: always ~0
// (thresholds too tight) or large negative (bots stacking, e.g. both chasing).
// ============================================================================

class TeammateSpacingPenalty : public Reward {
public:
    float threshold2v2, threshold3v3; // uu, penalty starts when closer than this

    TeammateSpacingPenalty(float threshold2v2 = 1000.0f, float threshold3v3 = 1500.0f)
        : threshold2v2(threshold2v2), threshold3v3(threshold3v3) {}

    float ThresholdFor(const GameState& state) const {
        int cnt[2] = {0, 0};
        for (const Player& p : state.players)
            cnt[(int)p.team]++;
        int teamSize = cnt[0] > cnt[1] ? cnt[0] : cnt[1];
        if (teamSize <= 1) return 0.0f;
        if (teamSize == 2) return threshold2v2;
        return threshold3v3;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool /*isFinal*/) override {
        if (player.isDemoed) return 0.0f;

        // Skip kickoff: ball near center with near-zero velocity
        if (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 100.0f)
            return 0.0f;

        float T = ThresholdFor(state);
        if (T <= 0.0f) return 0.0f;

        float total = 0.0f;
        for (const Player& mate : state.players) {
            if (mate.carId == player.carId || mate.team != player.team || mate.isDemoed)
                continue;
            float d = (mate.pos - player.pos).Length();
            if (d < T)
                total += 1.0f - d / T;
        }
        return -RS_MIN(total, 1.0f);
    }
};

} // namespace RLGC
