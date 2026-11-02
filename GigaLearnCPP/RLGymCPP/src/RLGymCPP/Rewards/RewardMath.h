#pragma once
// ============================================================================
// RewardMath.h
// 2025-12-18 | Rev 2.0 | NJH
// - Major revision for accurate goal detection geometry
// - Fixed goal mouth AABB to account for ball radius (center must be inside posts)
// - Corrected intersection logic to use proper goal line plane
// - Added separate "on target" vs "will score" checks
// - Improved trajectory prediction with gravity
// - All margin adjustments now properly documented
// ============================================================================
// 2025-12-18 | Rev 1.3 | NJH
// - Fix IsClosestToBall() to use Player::pos (Player has no phys member in this codebase)


#include "../CommonValues.h"
#include "../Gamestates/GameState.h" // needed standalone; v3 relied on include order

namespace RLGC {

    // ------------------------------------------------------------------------
    // Basic vector helpers
    // ------------------------------------------------------------------------
    static inline Vec SafeNorm(const Vec& v) {
        const float len = v.Length();
        if (len > 1e-6f) {
            return v * (1.0f / len);
        }
        return Vec(0.0f, 0.0f, 0.0f);
    }

    // ------------------------------------------------------------------------
    // RewardMath namespace
    // ------------------------------------------------------------------------
    namespace RewardMath {

        // Standard kickoff gate: ball still at/near center and roughly stationary.
        // Threshold matches the literal check duplicated across ~12 reward files
        // before this was extracted here.
        static inline bool IsKickoffActive(const Vec& ballPos, const Vec& ballVel) {
            return ballPos.Length() < 300.0f && ballVel.Length() < 200.0f;
        }

        // ====================================================================
        // Goal Mouth Geometry
        // ====================================================================

        struct GoalMouthAABB {
            float minX, maxX;      // horizontal bounds (accounting for ball radius)
            float minZ, maxZ;      // vertical bounds (accounting for ball radius)
            float goalLineY;       // front of goal (where ball enters)
            float backNetY;        // back of net plane
            float goalSign;        // +1 for orange (positive Y), -1 for blue (negative Y)
        };

        // Configuration: How much to shrink the goal opening for "on target" checks
        // 
        // CONSERVATIVE (strict): Only count shots clearly going in
        //   - Use positive margins (shrink the target)
        //   - Example: X_MARGIN = 100, Z_MARGIN = 50
        // 
        // PERMISSIVE (loose): Count shots near the goal
        //   - Use zero or negative margins (expand the target)
        //   - Example: X_MARGIN = 0, Z_MARGIN = 0
        //
        // BALL_RADIUS is always subtracted (ball center must be inside posts)
        constexpr float GOAL_MOUTH_X_MARGIN = CommonValues::BALL_RADIUS / 2.0f;   // additional X shrink beyond ball radius
        constexpr float GOAL_MOUTH_Z_MARGIN = CommonValues::BALL_RADIUS / 4.0f;   // additional Z shrink beyond ball radius

        // Precomputed goal mouth AABBs (world coordinates)
        // These represent where the BALL CENTER must be to score
        inline const GoalMouthAABB& OrangeGoalMouthAABB() {
            static const GoalMouthAABB aabb = {
                // Ball center must be inside posts (subtract ball radius from opening)
                -(CommonValues::GOAL_WIDTH_FROM_CENTER - CommonValues::BALL_RADIUS - GOAL_MOUTH_X_MARGIN),
                 (CommonValues::GOAL_WIDTH_FROM_CENTER - CommonValues::BALL_RADIUS - GOAL_MOUTH_X_MARGIN),

                 // Ball center must be above ground and below crossbar
                 CommonValues::BALL_RADIUS,  // can't go below floor + ball radius
                 (CommonValues::GOAL_HEIGHT - CommonValues::BALL_RADIUS - GOAL_MOUTH_Z_MARGIN),

                 +CommonValues::GOAL_LINE_Y,   // front of goal (y = +5120)
                 +CommonValues::BACK_NET_Y,    // back of goal (y = +6000)
                 +1.0f                          // positive Y direction
            };
            return aabb;
        }

        inline const GoalMouthAABB& BlueGoalMouthAABB() {
            static const GoalMouthAABB aabb = {
                -(CommonValues::GOAL_WIDTH_FROM_CENTER - CommonValues::BALL_RADIUS - GOAL_MOUTH_X_MARGIN),
                 (CommonValues::GOAL_WIDTH_FROM_CENTER - CommonValues::BALL_RADIUS - GOAL_MOUTH_X_MARGIN),

                CommonValues::BALL_RADIUS,
                (CommonValues::GOAL_HEIGHT - CommonValues::BALL_RADIUS - GOAL_MOUTH_Z_MARGIN),

                -CommonValues::GOAL_LINE_Y,   // front of goal (y = -5120)
                -CommonValues::BACK_NET_Y,    // back of goal (y = -6000)
                -1.0f                          // negative Y direction
            };
            return aabb;
        }

        // Select the opponent goal mouth box for a player
        inline const GoalMouthAABB& OpponentGoalMouthAABB(const Player& player) {
            // Blue (team 0) shoots toward +Y (Orange goal)
            // Orange (team 1) shoots toward -Y (Blue goal)
            return (player.team == Team::BLUE) ? OrangeGoalMouthAABB() : BlueGoalMouthAABB();
        }

        // ====================================================================
        // Goal Intersection Detection
        // ====================================================================

        // Check if ball trajectory intersects the opponent goal mouth
        // 
        // Returns true if the ball will pass through the goal opening rectangle
        // within the specified time horizon.
        //
        // Parameters:
        //   player: The player whose opponent goal to check
        //   state: Current game state (ball position and velocity)
        //   tMaxSec: Maximum time horizon to check (seconds)
        //   requireBackNet: If true, ball must reach back net (guarantees goal)
        //                   If false, only checks goal line crossing (may hit post/bar)
        //   useGravity: If true, accounts for gravity in trajectory (more accurate)
        //               If false, assumes straight-line motion (faster but less accurate)
        //
        // Example usage:
        //   - "On target" check: requireBackNet=false, useGravity=true
        //   - "Will score" check: requireBackNet=true, useGravity=true
        //   - Fast approximation: requireBackNet=false, useGravity=false
        inline bool BallIntersectsOpponentGoalMouth(
            const Player& player,
            const GameState& state,
            float tMaxSec = 2.0f,
            bool requireBackNet = false,    // Changed default to false (more permissive)
            bool useGravity = true
        ) {
            const GoalMouthAABB& box = OpponentGoalMouthAABB(player);

            const Vec p0 = state.ball.pos;
            const Vec v0 = state.ball.vel;

            // Early exit: Ball must be moving toward opponent goal
            // Use goalSign to determine required direction (+1 = positive Y, -1 = negative Y)
            const float vyTowardGoal = box.goalSign * v0.y;
            if (vyTowardGoal <= 100.0f) return false;  // threshold to filter noise/backwards

            // Calculate time to reach goal line plane
            const float tGoalLine = (box.goalLineY - p0.y) / v0.y;

            // Validate time to goal line
            if (tGoalLine <= 0.0f || tGoalLine > tMaxSec) return false;

            // Optional: Calculate time to reach back net (for "guaranteed goal" check)
            float tBackNet = tGoalLine;
            if (requireBackNet) {
                tBackNet = (box.backNetY - p0.y) / v0.y;

                // Back net must be reached after goal line and within time horizon
                if (tBackNet <= tGoalLine || tBackNet > tMaxSec) return false;
            }

            // Setup trajectory function with optional gravity
            const float gZ = useGravity ? CommonValues::BALL_GRAVITY : 0.0f;

            auto PositionAt = [&](float t) -> Vec {
                return Vec(
                    p0.x + v0.x * t,
                    p0.y + v0.y * t,
                    p0.z + v0.z * t + 0.5f * gZ * t * t
                );
                };

            // Check if position is inside goal mouth AABB (between the posts)
            auto IsInsideGoalMouth = [&](const Vec& pos) -> bool {
                if (pos.x < box.minX || pos.x > box.maxX) return false;
                if (pos.z < box.minZ || pos.z > box.maxZ) return false;
                return true;
                };

            // Check position at goal line crossing
            const Vec posAtGoalLine = PositionAt(tGoalLine);
            if (!IsInsideGoalMouth(posAtGoalLine)) return false;

            // If requiring back net, also check position at back net crossing
            if (requireBackNet) {
                const Vec posAtBackNet = PositionAt(tBackNet);
                if (!IsInsideGoalMouth(posAtBackNet)) return false;
            }

            return true;
        }

        // Goal-plane crossing of the ball's ballistic path (gravity, no drag/walls),
        // with the crossing time and position. Same math and same mouth AABB as
        // BallIntersectsOpponentGoalMouth (requireBackNet=false), but also returns
        // WHERE the ball crosses — needed by callers that gate on crossing height.
        //   inMouth : path crosses the opponent goal plane within tMaxSec, inside the mouth
        //   t / pos : crossing time and position (valid whenever the ball is moving toward
        //             the goal plane at all; inMouth may still be false)
        struct GoalCrossing {
            bool  inMouth = false;
            float t       = 0.0f;
            Vec   pos     = Vec(0, 0, 0);
        };

        inline GoalCrossing BallGoalCrossing(
            const Player& player,
            const GameState& state,
            float tMaxSec = 2.0f,
            bool useGravity = true
        ) {
            GoalCrossing out;
            const GoalMouthAABB& box = OpponentGoalMouthAABB(player);

            const Vec p0 = state.ball.pos;
            const Vec v0 = state.ball.vel;

            if (box.goalSign * v0.y <= 100.0f) return out;

            const float tGoalLine = (box.goalLineY - p0.y) / v0.y;
            if (tGoalLine <= 0.0f) return out;

            const float gZ = useGravity ? CommonValues::BALL_GRAVITY : 0.0f;
            out.t   = tGoalLine;
            out.pos = Vec(
                p0.x + v0.x * tGoalLine,
                p0.y + v0.y * tGoalLine,
                p0.z + v0.z * tGoalLine + 0.5f * gZ * tGoalLine * tGoalLine
            );

            if (tGoalLine > tMaxSec) return out;
            out.inMouth = out.pos.x >= box.minX && out.pos.x <= box.maxX &&
                          out.pos.z >= box.minZ && out.pos.z <= box.maxZ;
            return out;
        }

        // ====================================================================
        // Additional Helper Functions
        // ====================================================================

        // Check if ball is currently inside a goal (already scored)
        inline bool BallIsInGoal(const GameState& state, Team goalTeam) {
            const float ballY = state.ball.pos.y;
            const float ballX = state.ball.pos.x;
            const float ballZ = state.ball.pos.z;

            if (goalTeam == Team::ORANGE) {
                // Orange goal is at positive Y
                if (ballY < CommonValues::GOAL_LINE_Y) return false;
                if (ballY > CommonValues::BACK_NET_Y) return false;
            }
            else {
                // Blue goal is at negative Y
                if (ballY > -CommonValues::GOAL_LINE_Y) return false;
                if (ballY < -CommonValues::BACK_NET_Y) return false;
            }

            // Check horizontal bounds
            if (ballX < -CommonValues::GOAL_WIDTH_FROM_CENTER) return false;
            if (ballX > CommonValues::GOAL_WIDTH_FROM_CENTER) return false;

            // Check vertical bounds
            if (ballZ < CommonValues::BALL_RADIUS) return false;
            if (ballZ > CommonValues::GOAL_HEIGHT) return false;

            return true;
        }

        // Get distance from ball to center of opponent goal
        inline float DistanceToOpponentGoal(const Player& player, const GameState& state) {
            const Vec goalCenter = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;

            return (state.ball.pos - goalCenter).Length();
        }

        // Get 2D angle between ball velocity and direction to opponent goal
        // Returns angle in radians (0 = directly toward goal, PI = directly away)
        // NOTE: Z is ignored — this cannot distinguish on-target from over-the-crossbar.
        // Use BallIntersectsOpponentGoalMouth for accuracy-sensitive checks.
        inline float AngleToOpponentGoal(const Player& player, const GameState& state) {
            const Vec goalCenter = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;

            const Vec toGoal = SafeNorm(Vec(
                goalCenter.x - state.ball.pos.x,
                goalCenter.y - state.ball.pos.y,
                0.0f  // ignore Z for 2D angle
            ));

            const Vec ballDir = SafeNorm(Vec(state.ball.vel.x, state.ball.vel.y, 0.0f));

            // Dot product gives cos(angle)
            float dotProd = toGoal.x * ballDir.x + toGoal.y * ballDir.y;
            dotProd = std::max(-1.0f, std::min(1.0f, dotProd));  // clamp for acos

            return acosf(dotProd);
        }

        // True if player is closer to the ball than every living opponent.
        // Does NOT check teammates — a teammate may be closer and this still returns true.
        inline bool IsCloserToBallThanAllOpponents(const Player& player, const GameState& state) {
            const float myDist = (player.pos - state.ball.pos).Length();
            for (const Player& other : state.players) {
                if (other.carId == player.carId) continue;
                if (other.team == player.team) continue;
                if (other.isDemoed) continue;
                if ((other.pos - state.ball.pos).Length() < myDist) return false;
            }
            return true;
        }

        // True if player is the closest living player to the ball (any team).
        inline bool IsClosestPlayerToBall(const Player& player, const GameState& state) {
            const float myDist = (player.pos - state.ball.pos).Length();
            for (const Player& other : state.players) {
                if (other.carId == player.carId) continue;
                if (other.isDemoed) continue;
                if ((other.pos - state.ball.pos).Length() < myDist) return false;
            }
            return true;
        }

        // Deprecated: only checks opponents, not teammates. Use IsCloserToBallThanAllOpponents.
        inline bool IsClosestToBall(const Player& player, const GameState& state) {
            return IsCloserToBallThanAllOpponents(player, state);
        }

        // Check if player is in opponent's half of the field
        // Blue (team 0) attacks toward +Y (Orange goal), so opponent half is Y > 0
        // Orange (team 1) attacks toward -Y (Blue goal), so opponent half is Y < 0
        inline bool IsInOpponentHalf(const Player& player) {
            return (player.team == Team::BLUE) ? (player.pos.y > 0.0f) : (player.pos.y < 0.0f);
        }

        // Check if position is in opponent's half for a given team
        inline bool IsInOpponentHalf(const Vec& pos, Team team) {
            return (team == Team::BLUE) ? (pos.y > 0.0f) : (pos.y < 0.0f);
        }

        // Check if player is in own half of the field
        // Blue (team 0) defends at negative Y, Orange (team 1) defends at positive Y
        inline bool IsInOwnHalf(const Player& player) {
            return (player.team == Team::BLUE) ? (player.pos.y < 0.0f) : (player.pos.y > 0.0f);
        }

        // Check if position is in own half for a given team
        inline bool IsInOwnHalf(const Vec& pos, Team team) {
            return (team == Team::BLUE) ? (pos.y < 0.0f) : (pos.y > 0.0f);
        }

    } // namespace RewardMath

} // namespace RLGC