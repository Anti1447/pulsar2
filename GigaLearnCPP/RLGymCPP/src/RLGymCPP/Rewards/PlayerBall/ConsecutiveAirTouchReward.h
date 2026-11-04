#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"
#include "../PlayerBall/AerialTouchReward.h"
#include "../PlayerBall/AirInterceptPotentialReward.h"

namespace RLGC {

	// CONSECUTIVE AIR TOUCHES (2026-07-21, user-directed): rewards CHAINING several
	// airborne ball touches without landing in between - sustained aerial control
	// (aerial dribbling / juggling), the skill AerialTouchReward's single-strike
	// event cannot express. Built as an exact-telescoping PBRS so it is UNFARMABLE
	// BY CONSTRUCTION (the user's requirement), not by tuning: Phi is a saturating
	// function of a per-player consecutive-air-touch STREAK, r = gamma*Phi(s') -
	// Phi(s). Extending a chain raises Phi (positive, LOCAL credit for the extra
	// touch); the moment the player lands, the streak resets to 0 and the whole
	// chain is refunded (gamma*Phi(0) - Phi(n) = -Phi(n)), so a tap-land-tap grind
	// nets ~0 - the discounted sum over ANY trajectory telescopes to -Phi(start).
	// gamma MUST equal the learner's gaeGamma (stack rule) or it stops telescoping
	// against GAE. Wrap in ZeroSumReward: a linear map of a per-player potential is
	// still a telescoping potential, so this keeps the whole-stack zero-sum
	// invariant while staying exact PBRS. NEVER gate (positive-part gating breaks
	// telescoping).
	//
	// Phi(streak) = 1 - exp(-max(0, streak-1)/TAU): streak 0 AND 1 both score 0, so
	// a lone airborne touch pays nothing here (that is AerialTouchReward's job) and
	// credit begins on the 2nd consecutive air touch, saturating toward 1 so an
	// endless juggle cannot pay unboundedly (marginal credit shrinks each touch).
	// The streak counts this player's airborne (isOnGround==false) ballTouchedStep
	// touches and resets to 0 whenever the player is on the ground OR a wall (any
	// isOnGround - "consecutive AIR touches" ends the instant you are supported).
	// A demo/respawn is a teleport, not the player's action: the streak resets and
	// the step is not charged (mirrors the other potentials' demo guard - a
	// negligible, standard telescoping leak). Per-arena instance -> no cross-arena
	// state.
	//
	// 2026-11-03 audit fix: every touch is weighted by HOW HIGH THE BALL IS, with AerialTouchReward's own scale:
	// credit = clamp((ball.z - 150) / (1450 - 150), 0, 1) (0 at or below 150 uu, 1 at 1450 uu and above). The state is
	// now an EFFECTIVE streak, the running sum of that credit over the chain (a float, 0 on the ground), and
	// Phi(e) = 1 - exp(-max(0, e - 1) / TAU) as before, so the potential stays an exact function of state and the
	// telescoping proof is unchanged. A touch at 1450 uu or higher counts as 1 (the old behaviour); a touch with the ball
	// on the floor counts as 0 (before: any airborne contact counted, so a hop into a floor ball built a chain of 3-5
	// steps, +15..+34 weighted, refunded on landing; and with a plain 150 uu floor the hop-poke's own lift of the ball
	// still chained for 3 steps). Low touches now add almost nothing: the ball sits at z 112-178 in a hop-poke, so the
	// whole poke adds about 0.035 and pays 0.
	//
	// 2026-11-03 (MultiAirTouch audit, user-directed): three more MULTIPLIERS on a touch's credit and an OPPONENT-TURNOVER reset,
	// borrowed from MultiAirTouchReward, and a CONTROL-DISTANCE GATE on the potential. All of them keep the potential an exact
	// function of state (they scale what a touch adds, reset the state, or multiply Phi by a function of the current car-ball
	// distance), so telescoping holds.
	//   credit = ballHeight * softness * carHeight * horizontalSpeed
	//   softness   = 1 - clamp(|car vel - ball vel| / 500, 0, 1)^2   (a controlled, carried touch ~1; a hard whack ~0)
	//   carHeight  = clamp((car.z - 17) / (517 - 17), 0, 1)           (AirInterceptPotentialReward's car scale)
	//   horizontal = 1 -> 0.5 as the ball's horizontal speed goes 2300 -> 3000 uu/s (the car cannot keep up), waived within 3000 uu of
	//                the opponent goal (hard-shot intent); MultiAirTouch's rule
	//   turnover   : if an OPPONENT touches the ball on a step and this player does not, the chain ends (the streak resets, so the
	//                banked potential is charged back like a landing: a possession-lost cost). Detected from the per-step touch flags,
	//                NOT GameState::lastTouchCarID (that id is the highest player index on a same-step touch). A teammate's touch
	//                changes nothing, and a step both touch is a contested fight that keeps the chain.
	// Potential: Phi_total = Phi(e) * g(d), d = car-ball centre distance, g = 1 at d <= 150 (contact) falling linearly to 0 at
	// d >= 1100 (UnifiedAirDribbleReward's MAX_CONTROL_DIST). A chain is worth something only while the ball stays under control: a
	// ball that escapes pays a negative step, and two touches far apart no longer chain for free.
	class ConsecutiveAirTouchReward : public Reward {
	public:
		constexpr static float TAU = 2.0f; // touches past the first to reach ~63% saturation
		constexpr static float BALL_MIN_Z = AerialTouchReward::BALL_MIN_Z;   // credit 0 at or below this ball height
		constexpr static float BALL_FULL_Z = AerialTouchReward::BALL_FULL_Z; // credit 1 at or above this ball height
		constexpr static float CAR_MIN_Z = AirInterceptPotentialReward::CAR_MIN_Z;   // car height credit 0 at or below this
		constexpr static float CAR_FULL_Z = AirInterceptPotentialReward::CAR_FULL_Z; // car height credit 1 at or above this
		constexpr static float SOFT_MAX_DV = 500.0f;         // |car vel - ball vel| at which softness reaches 0 (MultiAirTouch's 500)
		constexpr static float HORIZ_VEL_LIMIT = 2300.0f;    // ball horizontal speed where the damping starts (MultiAirTouch's values)
		constexpr static float HORIZ_VEL_MAX = 3000.0f;      // ...and reaches HORIZ_MIN_MUL
		constexpr static float HORIZ_NEAR_GOAL = 3000.0f;    // damping is waived within this distance of the opponent goal back
		constexpr static float HORIZ_MIN_MUL = 0.5f;
		constexpr static float CONTROL_NEAR_DIST = 150.0f;   // car-ball centre distance of a contact: the control gate is 1 up to here
		constexpr static float CONTROL_MAX_DIST = 1100.0f;   // the gate is 0 from here (UnifiedAirDribbleReward::MAX_CONTROL_DIST)
		float gamma;
		ConsecutiveAirTouchReward(float gamma = 0.99f) : gamma(gamma) {}

		std::vector<float> streak; // per player.index: the EFFECTIVE streak (sum of ball-height credit of the chain's touches)

		// What one touch of a ball at height z adds to the chain (the ball-height part of the credit)
		static float TouchCredit(float ballZ) { return RS_CLAMP((ballZ - BALL_MIN_Z) / (BALL_FULL_Z - BALL_MIN_Z), 0.f, 1.f); }

		static float Softness(float velDiff) { float r = RS_CLAMP(velDiff / SOFT_MAX_DV, 0.f, 1.f); return 1.f - r * r; }
		static float CarHeightCredit(float carZ) { return RS_CLAMP((carZ - CAR_MIN_Z) / (CAR_FULL_Z - CAR_MIN_Z), 0.f, 1.f); }
		static float HorizontalSpeedMul(Team team, const Vec& ballPos, const Vec& ballVel) {
			Vec oppGoal = (team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
			if ((ballPos - oppGoal).Length() <= HORIZ_NEAR_GOAL)
				return 1.f;
			float horiz = sqrtf(ballVel.x * ballVel.x + ballVel.y * ballVel.y);
			if (horiz <= HORIZ_VEL_LIMIT)
				return 1.f;
			float t = RS_CLAMP((horiz - HORIZ_VEL_LIMIT) / (HORIZ_VEL_MAX - HORIZ_VEL_LIMIT), 0.f, 1.f);
			return 1.f - (1.f - HORIZ_MIN_MUL) * t;
		}

		// 1 while the ball is within contact range of the car, 0 once it is a control distance away
		static float ControlGate(float dist) { return RS_CLAMP((CONTROL_MAX_DIST - dist) / (CONTROL_MAX_DIST - CONTROL_NEAR_DIST), 0.f, 1.f); }

		// The full credit of one touch by `player` in `state`
		static float FullTouchCredit(const Player& player, const GameState& state) {
			return TouchCredit(state.ball.pos.z)
				* Softness((player.vel - state.ball.vel).Length())
				* CarHeightCredit(player.pos.z)
				* HorizontalSpeedMul(player.team, state.ball.pos, state.ball.vel);
		}

		// Did an alive player of the OTHER team touch the ball this step?
		static bool OpponentTouched(const Player& player, const GameState& state) {
			for (const Player& p : state.players)
				if (p.team != player.team && p.ballTouchedStep)
					return true;
			return false;
		}

		static float Phi(float streak) {
			float chained = streak - 1.f; // 0 and 1 -> no chain yet
			if (chained <= 0.f)
				return 0.f;
			return 1.f - expf(-chained / TAU);
		}

		virtual void Reset(const GameState& initialState) override {
			streak.assign(initialState.players.size(), 0.f);
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if ((size_t)player.index >= streak.size())
				streak.resize(player.index + 1, 0.f);
			float& s = streak[player.index];
			float prevStreak = s;

			// Demo/respawn teleport is not the player's action: drop the chain, don't charge it
			if (player.isDemoed || (player.prev && player.prev->isDemoed)) {
				s = 0;
				return 0;
			}

			if (player.isOnGround) {
				s = 0; // supported (ground OR wall): the airborne chain ends
			} else if (!player.ballTouchedStep && OpponentTouched(player, state)) {
				s = 0; // turnover: an opponent took the ball while this player did not touch it, the chain ends
			} else if (player.ballTouchedStep) {
				s = prevStreak + FullTouchCredit(player, state); // another airborne touch extends the chain by its credit
			}
			// else: airborne, no touch -> streak unchanged (Phi bleeds by (gamma-1)*Phi)

			// The potential is Phi(e) * g(d): the previous step's distance comes from the previous states (as AirIntercept does); with no
			// previous state it is taken equal to the current one
			float distNow = (state.ball.pos - player.pos).Length();
			float distPrev = (state.prev && player.prev) ? (state.prev->ball.pos - player.prev->pos).Length() : distNow;
			return gamma * Phi(s) * ControlGate(distNow) - Phi(prevStreak) * ControlGate(distPrev);
		}
	};
}
