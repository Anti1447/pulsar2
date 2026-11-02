#pragma once
#include "Reward.h"
#include <cmath>

namespace RLGC {

// ============================================================================
// AIR DRIBBLE UTILITY FUNCTIONS - Shared between multiple air dribble rewards
// ============================================================================
// 2026-02-09 | NJH
// ============================================================================

namespace AirDribbleUtils {

	/// <summary>
	/// Calculate contact angle relative to ball position (0 = directly below, π = directly above)
	/// </summary>
	inline float CalculateContactAngle(const Vec& carPos, const Vec& ballPos, const Vec& contactNormal) {
		Vec toBall = (ballPos - carPos).Normalized();
		// Use contactNormal to determine angle on ball surface
		// 0 radians = 6 o'clock (directly below)
		// π/2 radians = 3 or 9 o'clock (side)
		// π radians = 12 o'clock (directly above)

		if (contactNormal.Length() < 0.001f) return 0.0f;  // Safety check

		Vec normalDir = contactNormal.Normalized();
		// Map to angle: dot product gives us position on hemisphere
		float dotWithToBall = toBall.Dot(normalDir);
		return acos(RS_CLAMP(dotWithToBall, -1.0f, 1.0f));
	}

	/// <summary>
	/// Get optimal contact angle for field position (goal-side = more directly below, own-side = wider angle)
	/// </summary>
	inline float GetOptimalContactAngleForPosition(Team playerTeam, const Vec& ballPos) {
		// Determine position relative to goals
		// Rev 1.1: was measuring distance to OWN goal while the branches/comments below
		// assumed distance to the OPPONENT's goal (e.g. "distToOwnGoal < 5000 // Close to
		// opponent goal"), inverting the desired angle in both the attacking and defensive
		// thirds. Fixed to measure distance to the opponent's goal, matching the intent.
		Vec oppGoal = (playerTeam == Team::BLUE) ?
			CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

		float distToOppGoal = (ballPos - oppGoal).Length();

		// At opponent goal: prefer 6 o'clock (directly below) = 0 radians
		// At midfield: prefer 5-6 o'clock or 6-7 o'clock = ~0.26-0.52 radians (15-30°)
		// At own goal: prefer 4-5 o'clock or 7-8 o'clock = ~0.7-1.0 radians (40-60°)

		if (distToOppGoal < 5000.0f) {  // Close to opponent goal
			return 0.0f;  // 6 o'clock - directly below
		} else if (distToOppGoal < 8000.0f) {  // Midfield
			return 0.35f;  // ~20° off vertical (between 5-6 and 6-7 o'clock)
		} else {  // Own goal area
			return 0.7f;  // ~40° off vertical (between 4-5 and 7-8 o'clock)
		}
	}

	/// <summary>
	/// Calculate reward factor based on contact angle accuracy (Gaussian-like scaling)
	/// </summary>
	inline float CalculateContactAngleFactor(float desiredAngle, float actualAngle, float toleranceRadians) {
		float angleDiff = fabs(actualAngle - desiredAngle);

		// Gaussian: peak at 0, drops off based on tolerance
		// exp(-(x²)/(2σ²))
		float sigma = toleranceRadians;
		float exponent = -(angleDiff * angleDiff) / (2.0f * sigma * sigma);
		return exp(exponent);
	}

	/// <summary>
	/// Calculate velocity difference (car_vel - ball_vel) magnitude
	/// Lower value = softer contact (ideal for dribbles)
	/// </summary>
	inline float CalculateVelocityDifference(const Vec& carVelocity, const Vec& ballVelocity) {
		Vec velDiff = carVelocity - ballVelocity;
		return velDiff.Length();
	}

	/// <summary>
	/// Calculate softness bonus based on velocity difference
	/// 1.0 when perfect softness, scales down quadratically
	/// </summary>
	inline float CalculateSoftnessBonus(float velocityDifference, float maxSoftnessThreshold) {
		if (velocityDifference <= 0.0f) return 1.0f;  // Perfect soft touch

		// Quadratic scaling: (1 - (x / max)²)
		float ratio = RS_CLAMP(velocityDifference / maxSoftnessThreshold, 0.0f, 1.0f);
		return 1.0f - (ratio * ratio);
	}

	/// <summary>
	/// Calculate if ball's Z-velocity increased after touch (ball accelerating upward)
	/// Returns positive value if gaining height, 0 or negative if losing height
	/// </summary>
	inline float CalculateBallZVelocityGain(float ballPrevZ, float ballCurrZ, float ballPrevVelZ, float ballCurrVelZ) {
		// Current Z-velocity minus previous Z-velocity
		// Positive = acceleration upward (good for lift)
		float zVelGain = ballCurrVelZ - ballPrevVelZ;

		// Also consider actual height change
		float heightGain = ballCurrZ - ballPrevZ;

		// Combine both metrics: prefer actual height gain + upward acceleration
		return (heightGain + zVelGain) * 0.5f;
	}

}  // namespace AirDribbleUtils

}  // namespace RLGC
