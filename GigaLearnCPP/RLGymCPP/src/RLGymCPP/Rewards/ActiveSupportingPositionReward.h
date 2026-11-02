#pragma once
#include "Reward.h"
#include "../CommonValues.h"
#include "../Math.h"
#include "RewardMath.h"
#include <atomic>
#include <cfloat>
#include <cmath>

// Ported from RLGymBot_v3 (PositioningRewards.h). Logic unchanged unless noted.

namespace RLGC {

// ============================================================================
// ActiveSupportingPositionReward (v3) - 3-Slot Pass-to-Score Positioning
// 2026-05-01 | NJH
//
// Redesigned from v2 (single cross-field target) to a 3-slot system that
// models actual pass-to-score positions. The 2nd man is rewarded for holding
// any of three optimal scoring positions given where the ball is:
//
//   Slot 1 - Far Post:      near far goal post (~700 UU off-center, ~500 UU out)
//   Slot 2 - Penalty Spot:  dead center, ~1000 UU in front of goal
//   Slot 3 - Wide Diagonal: opposite side from ball, upfield ~600 UU from ball
//
// Reward = BASE_SCALE × max(proximity_1, proximity_2, proximity_3)
// Gates: 2v2+, team possession, MIDDLE role, ball in opponent's half.
// ============================================================================
class ActiveSupportingPositionReward : public Reward {
private:
    // Slot geometry (UU)
    static constexpr float SLOT1_X        = 700.0f;     // far post x offset from center
    static constexpr float SLOT1_Y_BACK   = 500.0f;     // UU in front of opponent goal line
    static constexpr float SLOT2_Y_BACK   = 1000.0f;    // central penalty spot UU from goal
    static constexpr float SLOT3_X        = 2200.0f;    // wide far-side x offset
    static constexpr float SLOT3_Y_UP     = 600.0f;     // UU upfield from ball

    static constexpr float TARGET_FALLOFF  = 3000.0f;
    static constexpr float CENTER_BALL_EPS = 500.0f;

    // Overlap penalty (same-side stacking near ball carrier)
    static constexpr float OVERLAP_LATERAL_MAX = 1500.0f;
    static constexpr float OVERLAP_PENALTY     = 0.25f;
    static constexpr float OVERLAP_Y_WINDOW    = 2000.0f;

    // Gates
    static constexpr int   MIN_GAME_SIZE     = 4;
    static constexpr float POSSESSION_RADIUS = 1500.0f;

    static constexpr float BASE_SCALE = 0.4f;

    // NOTE: metrics are class-level (inline static) — shared across ALL instances.
    // Never wire more than one ActiveSupportingPositionReward per phase or W&B values double.
    inline static std::atomic<long long> s_rewardMilli{0};
    inline static std::atomic<long long> s_penaltyMilli{0};
    inline static std::atomic<int> s_supportFrames{0};
    inline static std::atomic<int> s_slot1Frames{0};
    inline static std::atomic<int> s_slot2Frames{0};
    inline static std::atomic<int> s_slot3Frames{0};
    inline static std::atomic<int> s_overlapFrames{0};

    enum class SimpleRole { CLOSEST, MIDDLE, FURTHEST };

    SimpleRole DetermineRole(const Player& player, const GameState& state) const {
        float distToBall = (player.pos - state.ball.pos).Length();
        float closest1   = FLT_MAX, closest2 = FLT_MAX;
        int   liveFound  = 0;
        for (const auto& p : state.players) {
            if (p.team != player.team || p.carId == player.carId || p.isDemoed) continue;
            float d = (p.pos - state.ball.pos).Length();
            if (d < closest1) { closest2 = closest1; closest1 = d; }
            else if (d < closest2) { closest2 = d; }
            ++liveFound;
        }
        if (liveFound == 0) return SimpleRole::CLOSEST;
        if (distToBall < closest1) return SimpleRole::CLOSEST;
        if (liveFound >= 2 && distToBall > closest2) return SimpleRole::FURTHEST;
        return SimpleRole::MIDDLE;
    }

    bool DoesTeamHavePossession(const Player& player, const GameState& state) const {
        float closestDist = 1e10f;
        Team closestTeam = Team::BLUE;
        for (const auto& p : state.players) {
            if (p.isDemoed) continue;
            float dist = (p.pos - state.ball.pos).Length();
            if (dist < closestDist) { closestDist = dist; closestTeam = p.team; }
        }
        return closestTeam == player.team && closestDist < POSSESSION_RADIUS;
    }

public:
    struct Metrics {
        double totalReward  = 0.0;
        double totalPenalty = 0.0;
        int supportFrames   = 0;
        int slot1Frames     = 0;
        int slot2Frames     = 0;
        int slot3Frames     = 0;
        int overlapFrames   = 0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.totalReward   = double(s_rewardMilli.exchange(0))  / 1000.0;
        m.totalPenalty  = double(s_penaltyMilli.exchange(0)) / 1000.0;
        m.supportFrames = s_supportFrames.exchange(0);
        m.slot1Frames   = s_slot1Frames.exchange(0);
        m.slot2Frames   = s_slot2Frames.exchange(0);
        m.slot3Frames   = s_slot3Frames.exchange(0);
        m.overlapFrames = s_overlapFrames.exchange(0);
        return m;
    }

    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (static_cast<int>(state.players.size()) < MIN_GAME_SIZE) return 0.0f;
        if (!DoesTeamHavePossession(player, state)) return 0.0f;
        if (DetermineRole(player, state) != SimpleRole::MIDDLE) return 0.0f;
        if (!RewardMath::IsInOpponentHalf(state.ball.pos, player.team)) return 0.0f;

        const float oppGoalY   = (player.team == Team::BLUE) ? 5120.0f : -5120.0f;
        const float attackSign = (player.team == Team::BLUE) ? 1.0f : -1.0f;
        const float ballX      = state.ball.pos.x;
        const float ballY      = state.ball.pos.y;

        // Far-side sign: opposite from ball; ties broken by player's current x side
        float farSign;
        if (std::fabs(ballX) < CENTER_BALL_EPS) {
            farSign = (player.pos.x >= 0.0f) ? -1.0f : 1.0f;
        } else {
            farSign = (ballX > 0.0f) ? -1.0f : 1.0f;
        }

        // Slot 1: Far Post — near far goal post, just in front of goal line
        float s1x = RS_CLAMP(farSign * SLOT1_X, -3800.0f, 3800.0f);
        float s1y = RS_CLAMP(oppGoalY - attackSign * SLOT1_Y_BACK, -4900.0f, 4900.0f);

        // Slot 2: Central Penalty Spot — dead center, ~1000 UU from goal
        float s2x = 0.0f;
        float s2y = RS_CLAMP(oppGoalY - attackSign * SLOT2_Y_BACK, -4900.0f, 4900.0f);

        // Slot 3: Wide Far-Side Diagonal — opposite side, upfield from ball.
        // Clamp Y so slot stays ≥1500 UU from opponent goal, preventing collapse
        // into the same region as Slots 1 & 2 when ball is deep in opponent half.
        float s3x    = RS_CLAMP(farSign * SLOT3_X, -3800.0f, 3800.0f);
        float s3yMax = (player.team == Team::BLUE) ? (oppGoalY - 1500.0f) :  4900.0f;
        float s3yMin = (player.team == Team::BLUE) ? -4900.0f             : (oppGoalY + 1500.0f);
        float s3y    = RS_CLAMP(ballY + attackSign * SLOT3_Y_UP, s3yMin, s3yMax);

        // 2D proximity to each slot (ignore Z)
        auto prox = [&](float tx, float ty) {
            float dx = player.pos.x - tx;
            float dy = player.pos.y - ty;
            return RS_CLAMP(1.0f - std::sqrt(dx * dx + dy * dy) / TARGET_FALLOFF, 0.0f, 1.0f);
        };

        float p1 = prox(s1x, s1y);
        float p2 = prox(s2x, s2y);
        float p3 = prox(s3x, s3y);

        float bestScore;
        int bestSlot;
        if (p1 >= p2 && p1 >= p3) { bestScore = p1; bestSlot = 1; }
        else if (p2 >= p3)         { bestScore = p2; bestSlot = 2; }
        else                        { bestScore = p3; bestSlot = 3; }

        float reward = BASE_SCALE * bestScore;

        // Overlap penalty: same-side AND close to ball carrier (stacking is still bad).
        // Guard on bestScore > 0: don't apply a pure penalty when base reward is also zero.
        float overlap = 0.0f;
        if (bestScore > 0.0f) {
            const bool sameSide         = (ballX * player.pos.x > 0.0f) && std::fabs(ballX) > CENTER_BALL_EPS;
            const float lateralGap      = std::fabs(player.pos.x - ballX);
            const float longitudinalGap = std::fabs(player.pos.y - ballY);

            if (sameSide && lateralGap < OVERLAP_LATERAL_MAX && longitudinalGap < OVERLAP_Y_WINDOW) {
                float strength = 1.0f - (lateralGap / OVERLAP_LATERAL_MAX);
                overlap = -OVERLAP_PENALTY * strength;
                s_overlapFrames.fetch_add(1);
                s_penaltyMilli.fetch_add(static_cast<long long>(std::fabs(overlap) * 1000.0f));
            }
        }

        // Metrics
        s_supportFrames.fetch_add(1);
        if (bestSlot == 1)      s_slot1Frames.fetch_add(1);
        else if (bestSlot == 2) s_slot2Frames.fetch_add(1);
        else                    s_slot3Frames.fetch_add(1);
        if (reward > 0.0f) s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

        return reward + overlap;
    }
};

} // namespace RLGC
