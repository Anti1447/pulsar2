#pragma once
#include "Reward.h"
#include "../CommonValues.h"
#include <cmath>

// Ported from RLGymBot_v3 (AdvancedKickoffRewards.h: namespace KickoffUtil; port AdvancedKickoffRewards.h later by including this file). Logic unchanged unless noted.

namespace RLGC {

    // Shared kickoff utilities
    namespace KickoffUtil {
        inline bool IsKickoff(const GameState& state) {
            return (state.ball.pos.Length() < 500.0f && state.ball.vel.Length() < 200.0f);
        }

        // Returns distance this player is from the ball at their spawn
        inline float FormationDistFromCenter(const Player& player) {
            return sqrtf(player.pos.x * player.pos.x + player.pos.y * player.pos.y);
        }

        // Is this player the closest to the ball on their team? (= kickoff taker)
        inline bool IsKickoffTaker(const Player& player, const GameState& state) {
            float myDist = (player.pos - state.ball.pos).Length();
            for (const auto& p : state.players) {
                if (p.team != player.team || p.carId == player.carId) continue;
                if ((p.pos - state.ball.pos).Length() < myDist) return false;
            }
            return true;
        }

        // Is this player the "second man" — the CLOSER of the non-taker teammates?
        // 2026-09-13: added to fix PredictiveSecondManReward assigning BOTH non-taker
        // teammates to the cheater role in 3v3 (it previously just checked
        // !IsKickoffTaker, which is true for two different players in 3v3). That let
        // it fight KickoffCornerBoostAssignmentReward over the farther teammate, who
        // is now excluded here and left free for corner-boost/net-coverage duty.
        // Trivially equivalent to !IsKickoffTaker in 2v2, where only one non-taker exists.
        inline bool IsKickoffSecondMan(const Player& player, const GameState& state) {
            if (IsKickoffTaker(player, state)) return false;

            float myDist = (player.pos - state.ball.pos).Length();
            for (const auto& p : state.players) {
                if (p.team != player.team || p.carId == player.carId) continue;
                if (IsKickoffTaker(p, state)) continue; // taker doesn't count as a rival
                if ((p.pos - state.ball.pos).Length() < myDist) return false;
            }
            return true;
        }

        // Team-aware goal helpers
        inline Vec OwnGoal(const Player& p) {
            return (p.team == Team::BLUE) ? CommonValues::BLUE_GOAL_CENTER
                                          : CommonValues::ORANGE_GOAL_CENTER;
        }
        inline Vec OppGoal(const Player& p) {
            return (p.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_CENTER
                                          : CommonValues::BLUE_GOAL_CENTER;
        }
        // Y-axis sign toward opponent goal
        inline float ForwardSign(const Player& p) {
            return (p.team == Team::BLUE) ? 1.0f : -1.0f;
        }
    }

} // namespace RLGC
