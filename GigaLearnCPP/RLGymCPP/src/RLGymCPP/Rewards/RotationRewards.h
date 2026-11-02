#pragma once
#include "Reward.h"
#include "../Framework.h"
#include "../CommonValues.h"
#include "RewardTiming.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

// Ported from RLGymBot_v3 RotationRewards.h: rotation infrastructure (RotationPhase/Config, RoleType, RoleDetection,
// InstantSpacingReward, RetreatAfterTouchReward, RoleAwarenessReward) + ProgressiveRotationReward.
// NOT ported yet (later in the v3 file): SecondManAdvance, PassingLaneSetup, SupportPressure, BoostAwareRotation, DefensivePositioningChallenge.

namespace RLGC {

// =========================================================================
// PPO-FRIENDLY ROTATION REWARDS - PROGRESSIVE LEARNING
// =========================================================================
//
// DESIGN PHILOSOPHY:
// - Short feedback loops (< 2 seconds for most rewards)
// - Event-based rewards for milestone achievements
// - Continuous shaping rewards for gradual improvement
// - Progressive difficulty scaling from Bronze → SSL
//
// LEARNING PROGRESSION:
// Bronze-Gold:     Basic spacing, don't triple commit
// Platinum:        Recognize when to retreat after touch
// Diamond:         Basic back-post concepts, simple role awareness
// Champion:        Proper rotation cycles, adaptive positioning
// GC-SSL:          Advanced flow, teammate prediction, speed maintenance
//
// =========================================================================

// Phase-specific configuration
enum class RotationPhase {
	BRONZE_SILVER,    // Phase 1-2: Basic spacing only
	GOLD,             // Phase 3: Introduce retreat after touch
	PLATINUM,         // Phase 4: Back-post awareness
	DIAMOND,          // Phase 5: Role cycling basics
	CHAMPION_PLUS     // Phase 6+: Full rotation system
};

struct RotationConfig {
	// Spacing thresholds (progressive)
	float minTeammateSpacing;
	float maxTeammateSpacing;
	float clusterPenaltyWeight;

	// Back-post rotation
	float backPostProximity;
	float rotationEventReward;
	float rotationShapingWeight;

	// Role cycling
	float minRoleTime;
	float maxRoleTime;
	bool enableRoleCycling;

	// Adaptive behavior
	bool enableAdaptiveRotation;

	// Spacing: whether to penalize players for being too far apart
	bool enableMaxSpacing;

	// Phase C: Second-man offensive support (NEW)
	bool enableSecondManAdvance = true;
	bool enablePassingLaneSetup = true;
	bool enableSupportPressure = true;

	static RotationConfig ForPhase(RotationPhase phase) {
		RotationConfig cfg;

		switch (phase) {
		case RotationPhase::BRONZE_SILVER:
			// Focus: Stop triple-committing, basic spacing
			cfg.minTeammateSpacing = 1200.0f;
			cfg.maxTeammateSpacing = 6000.0f;
			cfg.clusterPenaltyWeight = 0.5f;
			cfg.backPostProximity = 0.0f;
			cfg.rotationEventReward = 0.0f;
			cfg.rotationShapingWeight = 0.0f;
			cfg.minRoleTime = 0.0f;
			cfg.maxRoleTime = 999.0f;
			cfg.enableRoleCycling = false;
			cfg.enableAdaptiveRotation = false;
			cfg.enableMaxSpacing = false;
			break;

		case RotationPhase::GOLD:
			// Focus: Retreat after touching ball, better spacing
			cfg.minTeammateSpacing = 1500.0f;
			cfg.maxTeammateSpacing = 5500.0f;
			cfg.clusterPenaltyWeight = 0.8f;
			cfg.backPostProximity = 2000.0f;
			cfg.rotationEventReward = 0.3f;
			cfg.rotationShapingWeight = 0.1f;
			cfg.minRoleTime = 0.0f;
			cfg.maxRoleTime = 999.0f;
			cfg.enableRoleCycling = false;
			cfg.enableAdaptiveRotation = false;
			cfg.enableMaxSpacing = false;
			break;

		case RotationPhase::PLATINUM:
			// Focus: Proper back-post rotation, role awareness begins
			cfg.minTeammateSpacing = 1800.0f;
			cfg.maxTeammateSpacing = 5000.0f;
			cfg.clusterPenaltyWeight = 1.2f;
			cfg.backPostProximity = 1500.0f;
			cfg.rotationEventReward = 0.5f;
			cfg.rotationShapingWeight = 0.3f;
			cfg.minRoleTime = 0.5f;
			cfg.maxRoleTime = 12.0f;
			cfg.enableRoleCycling = true;
			cfg.enableAdaptiveRotation = false;
			cfg.enableMaxSpacing = true;
			break;

		case RotationPhase::DIAMOND:
			// Focus: Proper role cycling, faster rotations
			cfg.minTeammateSpacing = 2000.0f;
			cfg.maxTeammateSpacing = 4500.0f;
			cfg.clusterPenaltyWeight = 1.5f;
			cfg.backPostProximity = 1200.0f;
			cfg.rotationEventReward = 0.8f;
			cfg.rotationShapingWeight = 0.5f;
			cfg.minRoleTime = 1.0f;
			cfg.maxRoleTime = 10.0f;
			cfg.enableRoleCycling = true;
			cfg.enableAdaptiveRotation = true;
			cfg.enableMaxSpacing = true;
			break;

		case RotationPhase::CHAMPION_PLUS:
			// Focus: Perfect rotation, high-speed flow
			cfg.minTeammateSpacing = 2000.0f;
			cfg.maxTeammateSpacing = 4500.0f;
			cfg.clusterPenaltyWeight = 1.5f;
			cfg.backPostProximity = 1100.0f;
			cfg.rotationEventReward = 1.3f;
			cfg.rotationShapingWeight = 1.0f;
			cfg.minRoleTime = 1.0f;
			cfg.maxRoleTime = 9.0f;
			cfg.enableRoleCycling = true;
			cfg.enableAdaptiveRotation = true;
			cfg.enableMaxSpacing = true;
			break;
		}

		return cfg;
	}
};

// =========================================================================
// ROLE DETECTION HELPERS
// =========================================================================
// Defined here (before reward classes) so all classes that include this
// header can use DetermineRoleCommon without forward-declaration issues.
//
// Rev 2.0 (2026-07-31 | NJH): role is now GOALSIDE-GATED, not pure
// distance-to-ball. There are 4 roles, not 3:
//   FIRST         - closest to ball among GOALSIDE teammates (engaging/offense)
//   SECOND        - support, goalside, closer to home net (Y) than FIRST.
//                   Does not exist as a separate slot in 2v2 - folds into LAST.
//   LAST          - deepest of the goalside teammates; last line of defense.
//   TRANSITIONING - NOT goalside (ball is between the player and their own
//                   net - beaten / caught upfield). Should be minimized.
// While any teammate is TRANSITIONING, the remaining goalside teammate(s)
// condense into just FIRST/LAST (no SECOND) - this falls out naturally of
// ranking only among currently-goalside players, no special-casing needed.
//
// Goalside convention matches GoalsideReward.h: margin = ball.y - player.y
// (Blue; flipped for Orange), with the same 200uu offside deadzone so
// dribble-carry (car slightly ahead of the ball plane) doesn't spuriously
// flip a player to TRANSITIONING every step.

// Role type enum - single source of truth for role determination
enum class RoleType {
	FIRST,          // Closest to ball among goalside teammates
	SECOND,         // Goalside support, between FIRST and LAST (3v3 only)
	LAST,           // Deepest goalside teammate - last line of defense
	TRANSITIONING   // Not goalside - beaten / caught upfield
};

namespace RoleDetection {
	constexpr float OFFSIDE_DEADZONE = 200.0f;  // matches GoalsideReward.h

	// Positive margin = player is goalside (between ball and own net, Y-only)
	inline float GoalsideMargin(const Player& p, const Vec& ballPos) {
		return (p.team == Team::BLUE) ? (ballPos.y - p.pos.y) : (p.pos.y - ballPos.y);
	}

	inline bool IsGoalside(const Player& p, const Vec& ballPos) {
		return GoalsideMargin(p, ballPos) >= -OFFSIDE_DEADZONE;
	}
}

// Number of living teammates currently goalside (excludes self, demoed, and
// transitioning teammates). Used to know whether a SECOND slot exists right now.
inline int CountGoalsideTeammates(const Player& player, const GameState& state) {
	int count = 0;
	for (const auto& p : state.players) {
		if (p.team != player.team || p.carId == player.carId || p.isDemoed) continue;
		if (RoleDetection::IsGoalside(p, state.ball.pos)) count++;
	}
	return count;
}

// Unified role detection function - eliminates code duplication
// Computes velocity-adjusted distances and determines player's role relative to
// GOALSIDE teammates only. See Rev 2.0 note above.
inline RoleType DetermineRoleCommon(const Player& player, const GameState& state) {
	if (!RoleDetection::IsGoalside(player, state.ball.pos)) {
		return RoleType::TRANSITIONING;
	}

	float distToBall = (player.pos - state.ball.pos).Length();
	Vec ballVel = state.ball.vel;
	Vec toPlayer = (player.pos - state.ball.pos).Normalized();

	float velocityAlignment = 0.0f;
	if (ballVel.Length() > 100.0f) {
		velocityAlignment = ballVel.Normalized().Dot(toPlayer);
	}

	float adjustedDist = distToBall * (1.0f - velocityAlignment * 0.25f);

	int livingTeammateCount = 0;
	std::vector<float> adjustedGoalsideTeammateDists;
	for (const auto& p : state.players) {
		if (p.team != player.team || p.carId == player.carId || p.isDemoed) continue;
		livingTeammateCount++;

		if (!RoleDetection::IsGoalside(p, state.ball.pos)) continue;  // transitioning teammate excluded from ranking pool

		float dist = (p.pos - state.ball.pos).Length();
		Vec toTeammate = (p.pos - state.ball.pos).Normalized();
		float tmateAlignment = 0.0f;
		if (ballVel.Length() > 100.0f) {
			tmateAlignment = ballVel.Normalized().Dot(toTeammate);
		}
		float adjustedTmateDist = dist * (1.0f - tmateAlignment * 0.25f);
		adjustedGoalsideTeammateDists.push_back(adjustedTmateDist);
	}

	if (livingTeammateCount == 0) {
		// 1v1 (or no living teammates) - no rotation concept, preserve prior default
		return RoleType::FIRST;
	}

	if (adjustedGoalsideTeammateDists.empty()) {
		// Sole goalside player on a beaten team - net-cover priority
		return RoleType::LAST;
	}

	std::sort(adjustedGoalsideTeammateDists.begin(), adjustedGoalsideTeammateDists.end());

	if (adjustedDist < adjustedGoalsideTeammateDists[0]) {
		return RoleType::FIRST;
	}
	else if (adjustedGoalsideTeammateDists.size() >= 2 && adjustedDist > adjustedGoalsideTeammateDists[1]) {
		return RoleType::LAST;
	}
	else if (adjustedGoalsideTeammateDists.size() == 1) {
		// Only 2 goalside players total (2v2, or 3v3 with one teammate
		// transitioning) - condensed to FIRST/LAST, no SECOND slot exists.
		return RoleType::LAST;
	}
	else {
		return RoleType::SECOND;
	}
}

inline bool IsSecondManRole(const Player& player, const GameState& state) {
	return DetermineRoleCommon(player, state) == RoleType::SECOND;
}

// Returns the teammate currently playing FIRST man (goalside, closest to ball).
// Falls back to the closest-to-ball teammate if no teammate is currently FIRST
// (e.g. everyone transitioning) so callers still get a sensible reference point.
inline Player* GetFirstManTeammate(const Player& player, const GameState& state) {
	Player* firstMan = nullptr;
	Player* fallback = nullptr;
	float closestDistToBall = FLT_MAX;

	for (const auto& p : state.players) {
		if (p.carId == player.carId) continue;
		if (p.team != player.team) continue;

		if (DetermineRoleCommon(p, state) == RoleType::FIRST) {
			firstMan = const_cast<Player*>(&p);
		}

		float distToBall = (p.pos - state.ball.pos).Length();
		if (distToBall < closestDistToBall) {
			closestDistToBall = distToBall;
			fallback = const_cast<Player*>(&p);
		}
	}

	return firstMan ? firstMan : fallback;
}

// =========================================================================
// IMMEDIATE SPACING REWARD (PPO-FRIENDLY)
// =========================================================================
// Rewards/punishes INSTANTLY based on current spacing
// No memory, no delay - pure state-based reward
//
// Bronze-Gold: Just don't cluster
// Platinum+: Maintain optimal triangle spacing
//
// Rev 1.1 (2026-07-25 | NJH): four fixes from a code-review pass —
//   1. Kickoff gate added (same convention as UncoveredNetPenalty /
//      LastManOvercommitPenalty in this codebase). Standard 3v3 kickoff
//      formations put two "back" spawns 509-809 UU apart (see
//      CommonValues.h BLUE_KICKOFF_BACK_RIGHT/BACK_LEFT/FAR_BACK_CENTER) —
//      well under even Bronze's 1200 UU minimum — so without this gate the
//      reward fired a spacing penalty off pure spawn geometry every episode.
//   2. Distance is now horizontal-only (X/Y, .z zeroed before the distance
//      check). Previously a full 3D distance let vertical separation (e.g.
//      one player mid-aerial directly above a grounded teammate) count as
//      "good spacing" despite both occupying the same field position.
//   3. Good-spacing bonus scale raised 0.1f -> 0.2f. The bonus was smaller
//      than even the too-far penalty ceiling (weight*0.15), so in 3v3 a
//      player with one good pairing and one bad pairing (through no fault
//      of their own — a third teammate wandered off) averaged out negative.
//      0.2f clears the too-far ceiling (good+worst-far nets positive) while
//      staying well under the too-close ceiling (weight*1.0), so clustering
//      — the reward's primary target — still dominates the signal.
//      Averaging itself (reward / teammateCount) already produces the
//      desired "aggregate positioning across all teammates" behavior; the
//      bonus/penalty scale was the only thing making it read as unfair.
//   4. Demoed player exclusion: a demoed player no longer receives this
//      reward at all (returns 0 until respawn). Living teammates already
//      correctly ignored demoed teammates as reference points (existing
//      p.isDemoed check in the loop); if every teammate is demoed,
//      teammateCount stays 0 and the existing fallback already returns 0.

class InstantSpacingReward : public Reward {
private:
	RotationConfig config;

	static constexpr float GOOD_SPACING_SCALE = 0.2f;  // Rev 1.1: was 0.1f

public:
	InstantSpacingReward(RotationPhase phase)
		: config(RotationConfig::ForPhase(phase)) {
	}

	void Reset(const GameState& state) override {}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		if (player.isDemoed) return 0.0f;

		// Kickoff gate: ball parked at center, near-zero velocity
		if (state.ball.pos.Length() < 200.0f && state.ball.vel.Length() < 150.0f)
			return 0.0f;

		float reward = 0.0f;
		int teammateCount = 0;

		for (const auto& p : state.players) {
			if (p.team != player.team || p.carId == player.carId || p.isDemoed) continue;

			// Horizontal-only (X/Y) spacing — height differences don't count
			// as "spread out"; two players stacked vertically are still clustered.
			Vec a = player.pos; a.z = 0.0f;
			Vec b = p.pos;      b.z = 0.0f;
			float dist = (a - b).Length();

			// INSTANT PENALTY for being too close
			if (dist < config.minTeammateSpacing) {
				float ratio = dist / config.minTeammateSpacing;
				reward -= config.clusterPenaltyWeight * (1.0f - ratio);
			}
			// INSTANT PENALTY for being too far (only in phases that enforce max spacing)
			else if (dist > config.maxTeammateSpacing && config.enableMaxSpacing) {
				float excessDist = dist - config.maxTeammateSpacing;
				reward -= config.clusterPenaltyWeight * 0.15f * RS_CLAMP(excessDist / 2000.0f, 0.0f, 1.0f);
			}
			// INSTANT REWARD for good spacing — proportional to phase severity
			else {
				reward += config.clusterPenaltyWeight * GOOD_SPACING_SCALE;
			}

			teammateCount++;
		}

		return teammateCount > 0 ? reward / teammateCount : 0.0f;
	}

	void PreStep(const GameState& state) override {}
};

// =========================================================================
// RETREAT AFTER TOUCH REWARD (PPO-FRIENDLY)
// =========================================================================
// SHORT-TERM MEMORY: Remembers touch for 120 steps (1 second) max
//
// Event reward: +0.5 when you start retreating after touch (INSTANT)
// Shaping reward: Continuous reward for retreating (< 1s feedback)
//
// Gold: Learn to retreat at all
// Platinum+: Retreat to back-post specifically

class RetreatAfterTouchReward : public Reward {
private:
	struct TouchMemory {
		bool justTouched = false;
		float touchTime = 0.0f;
		bool hasGivenEventReward = false;
		bool isRetreating = false;
		bool hasGivenThirdManBonus = false;
	};

	std::unordered_map<uint32_t, TouchMemory> memory;
	RotationConfig config;
	float currentTime = 0.0f;

	static constexpr float MEMORY_DURATION   = 2.0f;    // seconds — extended for PPO credit assignment
	static constexpr float MAX_RETREAT_SPEED = 1200.0f; // u/s — fast retreat reference for speed scaling
	static constexpr float THIRD_MAN_BONUS   = 0.4f;    // one-time bonus for fully rotating to last man

	Vec GetBackPost(Team team, const Vec& ballPos) const {
		float goalY = (team == Team::BLUE) ?
			-CommonValues::GOAL_LINE_Y : CommonValues::GOAL_LINE_Y;

		// Back post is the post furthest from ball's X position
		float backPostX = (ballPos.x > 0) ?
			-CommonValues::GOAL_WIDTH_FROM_CENTER : CommonValues::GOAL_WIDTH_FROM_CENTER;

		return Vec(backPostX, goalY, 0.0f);
	}

	bool IsRetreating(const Player& player) const {
		// Retreating = moving toward own goal (encodes intent directly)
		Vec ownGoal = (player.team == Team::BLUE)
			? CommonValues::BLUE_GOAL_CENTER
			: CommonValues::ORANGE_GOAL_CENTER;

		Vec velocity = player.vel;
		if (velocity.Length() < 50.0f) return false;

		Vec toGoal = (ownGoal - player.pos).Normalized();
		return velocity.Normalized().Dot(toGoal) > 0.2f;
	}

public:
	RetreatAfterTouchReward(RotationPhase phase)
		: config(RotationConfig::ForPhase(phase)) {
	}

	void Reset(const GameState& state) override {
		memory.clear();
		currentTime = 0.0f;
	}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		if (config.backPostProximity < 1.0f) {
			return 0.0f;  // Not enabled in this phase
		}

		auto& mem = memory[player.carId];
		float reward = 0.0f;

		// Detect NEW touch
		if (player.ballTouchedStep && !mem.justTouched) {
			mem.justTouched = true;
			mem.touchTime = currentTime;
			mem.hasGivenEventReward = false;
			mem.isRetreating = false;
			mem.hasGivenThirdManBonus = false;
		}

		// Check if memory has expired
		if (mem.justTouched && (currentTime - mem.touchTime) > MEMORY_DURATION) {
			mem.justTouched = false;
			return 0.0f;
		}

		// Process retreat if we recently touched
		if (mem.justTouched) {
			bool retreating = IsRetreating(player);

			// EVENT REWARD: First frame of retreating (INSTANT feedback)
			if (retreating && !mem.isRetreating && !mem.hasGivenEventReward) {
				reward += config.rotationEventReward;
				mem.hasGivenEventReward = true;
			}

			mem.isRetreating = retreating;

			// SHAPING REWARD: Continuous reward for retreating — scales with speed
			if (retreating) {
				Vec ownGoal = (player.team == Team::BLUE)
					? CommonValues::BLUE_GOAL_CENTER
					: CommonValues::ORANGE_GOAL_CENTER;
				Vec toGoal = (ownGoal - player.pos).Normalized();
				float retreatSpeed = player.vel.Dot(toGoal);
				float speedScale = RS_CLAMP(retreatSpeed / MAX_RETREAT_SPEED, 0.5f, 1.5f);

				float ballGoalY = (player.team == Team::BLUE) ?
					state.ball.pos.y : -state.ball.pos.y;

				if (ballGoalY < 0) {
					Vec backPost = GetBackPost(player.team, state.ball.pos);
					float distToBackPost = (player.pos - backPost).Length();
					float proximityRatio = 1.0f - RS_CLAMP(distToBackPost / config.backPostProximity, 0.0f, 1.0f);
					reward += proximityRatio * config.rotationShapingWeight * speedScale;
				} else {
					float distFromBall = (player.pos - state.ball.pos).Length();
					float retreatRatio = RS_CLAMP(distFromBall / (config.backPostProximity * 1.5f), 0.0f, 1.0f);
					reward += (0.5f * retreatRatio) * config.rotationShapingWeight * speedScale;
				}
			}

			// THIRD MAN BONUS: one-time reward for fully completing rotation to last man (3v3 only)
			if (!mem.hasGivenThirdManBonus && state.players.size() >= 6) {
				RoleType role = DetermineRoleCommon(player, state);
				if (role == RoleType::LAST) {
					reward += THIRD_MAN_BONUS;
					mem.hasGivenThirdManBonus = true;
				}
			}
		}

		return reward;
	}

	void PreStep(const GameState& state) override {
		currentTime += StepDt(state);
	}
};

// =========================================================================
// PHASE C HELPER FUNCTIONS (NEW)
// =========================================================================
// Used by Phase C second-man support rewards to identify roles and relationships

// Named constants for Phase C spacing and positioning logic
namespace PhaseC {
	// Distance thresholds for second-man support
	static constexpr float FIRST_MAN_PRESSURED_DIST = 1500.0f;   // When first-man is "committed"
	static constexpr float SECOND_MAN_SUPPORT_RANGE_MIN = 1500.0f; // Minimum distance for support
	static constexpr float SECOND_MAN_SUPPORT_RANGE_MAX = 3500.0f; // Maximum distance for support
	static constexpr float GOAL_POST_BACK_DISTANCE = 2500.0f;    // "Back post" optimal position
	static constexpr float OPPONENT_CHALLENGE_DIST = 2000.0f;   // When opponent is challenging
}

// =========================================================================
// ROLE AWARENESS REWARD (PPO-FRIENDLY)
// =========================================================================
// Rev 2.0 (2026-07-31 | NJH): rebuilt around the goalside-gated 4-role model
// (see the ROLE DETECTION HELPERS block above). Enforces the actual rotation
// CYCLE, not just "any label change":
//   3v3: FIRST -> LAST -> SECOND -> FIRST (repeat)
//   2v2: FIRST -> LAST -> FIRST (repeat, SECOND never exists)
// Condenses automatically when a teammate is TRANSITIONING (beaten) —
// DetermineRoleCommon already excludes them from the ranking pool, so the
// remaining goalside player(s) naturally fall into FIRST/LAST only.
//
// Rev 3.0 (2026-08-27 | NJH): all 4 discrete one-time bonuses (the +0.3
// generic lateral-transition reward, the +0.4 retreat/advance cycle bonuses,
// and the +0.4 recovery bonus) were REMOVED and moved to the new standalone
// RoleCycleTransitionReward.h — that class now owns the discrete
// cycle-completion signal exclusively (extended through LAST->FIRST directly,
// with SECOND as an optional waypoint, rather than stopping at SECOND). This
// class now scores ONLY the 2 continuous per-step penalties below; the
// CycleStage/timeInRole bookkeeping is kept only because those 2 penalties
// still depend on it.
//
// Penalty: -0.08/step for stalling at SECOND instead of completing the
//   retreat to LAST, but ONLY while the ball is in our own half (staying
//   forward to support the attack is often correct otherwise)
// Penalty: -0.05/step for camping in FIRST/SECOND/LAST past maxRoleTime
//   (never while TRANSITIONING — that's GoalsideReward's signal, not ours)
//
// Platinum: Basic role awareness (very short times)
// Diamond+: Proper role cycling

class RoleAwarenessReward : public Reward {
private:
	enum class CycleStage {
		NONE,
		AWAITING_RETREAT,  // was FIRST, must reach LAST next
		AWAITING_ADVANCE   // reached LAST after FIRST, must reach SECOND next (3v3 only)
	};

	struct RoleMemory {
		RoleType lastRole = RoleType::TRANSITIONING;
		float timeInRole = 0.0f;

		// Event tracking for hybrid (time + event) role transitions
		bool lastFrameHadPossession = false;

		CycleStage cycleStage = CycleStage::NONE;
		float cycleStageTime = 0.0f;
	};

	std::unordered_map<uint32_t, RoleMemory> memory;
	RotationConfig config;

	static constexpr float RETREAT_WINDOW  = 5.0f;   // seconds to complete FIRST -> LAST
	static constexpr float RETREAT_PENALTY = 0.08f;  // per-step penalty for stalling at SECOND (own half only)
	static constexpr float ADVANCE_WINDOW  = 5.0f;   // seconds to complete LAST -> SECOND (3v3 only)
	// RETREAT_BONUS/ADVANCE_BONUS/RECOVERY_BONUS removed Rev 3.0 — see RoleCycleTransitionReward.h

	// Check for events that should trigger immediate role transition
	bool CheckEventTriggers(const Player& player, const GameState& state, RoleMemory& mem) const {
		// Event 1: Ball possession lost (player touched last frame, but doesn't now)
		bool currentPossession = player.ballTouchedStep;
		if (mem.lastFrameHadPossession && !currentPossession) {
			// Just lost possession - should rotate out
			return true;
		}

		// TODO: Event 2 (demo detection via velocity spike) was too noisy, disabled

		return false;  // No events triggered
	}

public:
	RoleAwarenessReward(RotationPhase phase)
		: config(RotationConfig::ForPhase(phase)) {
	}

	void Reset(const GameState& state) override {
		memory.clear();
	}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		if (!config.enableRoleCycling) {
			return 0.0f;
		}

		// Kickoff gate — matches InstantSpacingReward's convention. Role
		// churn during a kickoff scramble isn't real rotation.
		if (state.ball.pos.Length() < 200.0f && state.ball.vel.Length() < 150.0f) {
			return 0.0f;
		}

		auto& mem = memory[player.carId];
		float reward = 0.0f;
		float dt = StepDt(state);

		// Check for event-based role transition triggers
		bool eventTriggered = CheckEventTriggers(player, state, mem);
		if (eventTriggered) {
			// Force immediate role transition capability
			mem.timeInRole = 0.0f;
		}

		RoleType currentRole = DetermineRoleCommon(player, state);
		RoleType previousRole = mem.lastRole;  // previous frame's role — mem.lastRole is updated below

		// Ball in this player's own half? (same convention as RetreatAfterTouchReward)
		bool ballInOwnHalf = (player.team == Team::BLUE) ? (state.ball.pos.y < 0.0f) : (state.ball.pos.y > 0.0f);

		// Enter the cycle when leaving FIRST for another goalside role
		if (previousRole == RoleType::FIRST && currentRole != RoleType::FIRST && currentRole != RoleType::TRANSITIONING) {
			mem.cycleStage = CycleStage::AWAITING_RETREAT;
			mem.cycleStageTime = 0.0f;
		}

		if (currentRole == RoleType::TRANSITIONING) {
			// The retreat/advance cycle doesn't apply while out of position.
			mem.cycleStage = CycleStage::NONE;
			mem.cycleStageTime = 0.0f;
		}
		else if (mem.cycleStage == CycleStage::AWAITING_RETREAT) {
			mem.cycleStageTime += dt;

			if (currentRole == RoleType::LAST) {
				// RETREAT_BONUS removed Rev 3.0 — see RoleCycleTransitionReward.h
				// A SECOND slot only exists right now if >=2 teammates are goalside.
				mem.cycleStage = (CountGoalsideTeammates(player, state) >= 2)
					? CycleStage::AWAITING_ADVANCE
					: CycleStage::NONE;
				mem.cycleStageTime = 0.0f;
			}
			else if (currentRole == RoleType::FIRST) {
				// New first-man opportunity — clear without bonus
				mem.cycleStage = CycleStage::NONE;
				mem.cycleStageTime = 0.0f;
			}
			else if (mem.cycleStageTime > RETREAT_WINDOW) {
				// Timed out without completing the retreat — clear, no bonus
				mem.cycleStage = CycleStage::NONE;
				mem.cycleStageTime = 0.0f;
			}
			else if (ballInOwnHalf) {
				// Still at SECOND within the window while the ball is in our
				// own half — penalize cutting the rotation short. No penalty
				// when the ball is in the opponent's half (staying forward
				// to support the attack is often correct there).
				reward -= RETREAT_PENALTY;
			}
		}
		else if (mem.cycleStage == CycleStage::AWAITING_ADVANCE) {
			mem.cycleStageTime += dt;

			if (currentRole == RoleType::SECOND) {
				// ADVANCE_BONUS removed Rev 3.0 — see RoleCycleTransitionReward.h
				mem.cycleStage = CycleStage::NONE;
				mem.cycleStageTime = 0.0f;
			}
			else if (currentRole == RoleType::FIRST) {
				mem.cycleStage = CycleStage::NONE;
				mem.cycleStageTime = 0.0f;
			}
			else if (mem.cycleStageTime > ADVANCE_WINDOW) {
				mem.cycleStage = CycleStage::NONE;
				mem.cycleStageTime = 0.0f;
			}
			// No per-step penalty for staying at LAST — holding last-man
			// position is often correct (boost-constrained or deliberate),
			// and penalizing it would fight BoostAwareRotationReward.
		}

		// Note: this block previously paid RECOVERY_BONUS / a flat 0.3f transition
		// reward here — removed Rev 3.0 (see RoleCycleTransitionReward.h). The
		// lastRole/timeInRole bookkeeping below is still required input to the
		// surviving maxRoleTime camping penalty further down.
		if (currentRole != previousRole) {
			mem.lastRole = currentRole;
			mem.timeInRole = 0.0f;
		}
		else {
			mem.timeInRole += dt;
		}

		// INSTANT PENALTY for camping too long in FIRST/SECOND/LAST (never
		// while TRANSITIONING — that's scored by GoalsideReward, not here).
		if (currentRole != RoleType::TRANSITIONING && mem.timeInRole > config.maxRoleTime) {
			reward -= 0.05f;  // Small per-step penalty
		}

		// Track possession for next frame's event detection
		mem.lastFrameHadPossession = player.ballTouchedStep;

		return reward;
	}

	void PreStep(const GameState& state) override {}
};

// =========================================================================
// PROGRESSIVE ROTATION REWARD (MASTER CLASS)
// =========================================================================
// Combines all rewards with phase-appropriate weights
//
// Usage:
//   Bronze-Silver:  WeightedReward(new ProgressiveRotationReward(RotationPhase::BRONZE_SILVER), 5.0f)
//   Gold:           WeightedReward(new ProgressiveRotationReward(RotationPhase::GOLD), 8.0f)
//   Platinum:       WeightedReward(new ProgressiveRotationReward(RotationPhase::PLATINUM), 12.0f)
//   Diamond:        WeightedReward(new ProgressiveRotationReward(RotationPhase::DIAMOND), 15.0f)
//   Champion+:      WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 25.0f)

class ProgressiveRotationReward : public Reward {
private:
	InstantSpacingReward spacingReward;
	RetreatAfterTouchReward retreatReward;
	RoleAwarenessReward roleReward;

	RotationPhase phase;

public:
	ProgressiveRotationReward(RotationPhase phase_)
		: phase(phase_)
		, spacingReward(phase_)
		, retreatReward(phase_)
		, roleReward(phase_)
	{
	}

	void Reset(const GameState& state) override {
		spacingReward.Reset(state);
		retreatReward.Reset(state);
		roleReward.Reset(state);
	}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		float total = 0.0f;

		// Phase-specific weighting
		switch (phase) {
		case RotationPhase::BRONZE_SILVER:
			// Focus: Just spacing
			total += spacingReward.GetReward(player, state, isFinal) * 1.0f;
			break;

		case RotationPhase::GOLD:
			// Focus: Spacing + basic retreat
			total += spacingReward.GetReward(player, state, isFinal) * 0.7f;
			total += retreatReward.GetReward(player, state, isFinal) * 1.0f;
			break;

		case RotationPhase::PLATINUM:
			// Focus: All basics, introduce roles
			total += spacingReward.GetReward(player, state, isFinal) * 0.1f;
			total += retreatReward.GetReward(player, state, isFinal) * 0.6f;
			total += roleReward.GetReward(player, state, isFinal) * 0.3f;
			break;

		case RotationPhase::DIAMOND:
			// Focus: Balanced rotation system
			total += spacingReward.GetReward(player, state, isFinal) * 0.5f;
			total += retreatReward.GetReward(player, state, isFinal) * 0.8f;
			total += roleReward.GetReward(player, state, isFinal) * 0.8f;
			break;

		case RotationPhase::CHAMPION_PLUS:
			// Focus: Perfect execution
			total += spacingReward.GetReward(player, state, isFinal) * 0.4f;
			total += retreatReward.GetReward(player, state, isFinal) * 0.6f;
			total += roleReward.GetReward(player, state, isFinal) * 1.0f;
			break;
		}

		return total;
	}

	void PreStep(const GameState& state) override {
		spacingReward.PreStep(state);
		retreatReward.PreStep(state);
		roleReward.PreStep(state);
	}
};

} // namespace RLGC
