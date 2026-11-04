#pragma once
#include "Reward.h"
#include "RewardTrace.h"
#include "../Math.h"
#include "Events/PlayerDataEventReward.h"
#include "Events/DemoReward.h"
#include "Events/GoalReward.h"
#include "BallGoal/OpposedSaveReward.h"
#include "BallGoal/BallToGoalPotentialReward.h"
#include "Match/TimeCostReward.h"
#include "Match/TeamPressureReward.h"
#include "Player/CarEnergyPotentialReward.h"
#include "Player/GuardedPickupBoostReward.h"
#include "PlayerBall/BallProximityPotentialReward.h"
#include "PlayerBall/TouchAccelReward.h"
#include "PlayerBall/AerialTouchReward.h"
#include "PlayerBall/AirInterceptPotentialReward.h"
#include "PlayerBall/ConsecutiveAirTouchReward.h"
#include "PlayerBall/WallJumpToBallReward.h"
#include "PlayerBall/KickoffRaceReward.h"

namespace RLGC {

	typedef PlayerDataEventReward<&PlayerEventState::goal, false> PlayerGoalReward; // NOTE: Given only to the player who last touched the ball on the opposing team
	typedef PlayerDataEventReward<&PlayerEventState::assist, false> AssistReward;
	typedef PlayerDataEventReward<&PlayerEventState::shot, false> ShotReward;
	typedef PlayerDataEventReward<&PlayerEventState::shotPass, false> ShotPassReward;
	typedef PlayerDataEventReward<&PlayerEventState::save, false> SaveReward;
	typedef PlayerDataEventReward<&PlayerEventState::bump, false> BumpReward;
	typedef PlayerDataEventReward<&PlayerEventState::bumped, true> BumpedPenalty;
	typedef PlayerDataEventReward<&PlayerEventState::demoed, true> DemoedPenalty;

	// -1 to every player (both teams) on the step a NoTouchCondition(maxTime) with the same maxTime ends the episode.
	// Keeps its own copy of the condition's timer (same update, run in PreStep right after the terminal check) instead
	// of pointing at the condition, so it never outlives one that a viewer or eval rebuild deletes. A goal on the same
	// step wins: GoalReward pays it and this pays nothing.
	class NoTouchTimeoutPenalty : public Reward {
	public:
		float maxTime;
		float timeSinceTouch = 0;
		NoTouchTimeoutPenalty(float maxTime) : maxTime(maxTime) {}

		virtual void Reset(const GameState& initialState) {
			timeSinceTouch = 0;
		}

		virtual void PreStep(const GameState& state) {
			for (auto& player : state.players) {
				if (player.ballTouchedStep) {
					timeSinceTouch = 0;
					return;
				}
			}
			timeSinceTouch += state.deltaTime;
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			return (isFinal && !state.goalScored && timeSinceTouch >= maxTime) ? -1 : 0;
		}
	};

	// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/misc_rewards.py
	class VelocityReward : public Reward {
	public:
		bool isNegative;
		VelocityReward(bool isNegative = false) : isNegative(isNegative) {}
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			return player.vel.Length() / CommonValues::CAR_MAX_SPEED * (1 - 2 * isNegative);
		}
	};

	// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/ball_goal_rewards.py
	class VelocityBallToGoalReward : public Reward {
	public:
		bool ownGoal = false;
		VelocityBallToGoalReward(bool ownGoal = false) : ownGoal(ownGoal) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			bool targetOrangeGoal = player.team == Team::BLUE;
			if (ownGoal)
				targetOrangeGoal = !targetOrangeGoal;

			Vec targetPos = targetOrangeGoal ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
			
			Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
			return ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
		}
	};

	// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/player_ball_rewards.py
	class VelocityPlayerToBallReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			Vec dirToBall = (state.ball.pos - player.pos).Normalized();
			Vec normVel = player.vel / CommonValues::CAR_MAX_SPEED;
			return dirToBall.Dot(normVel);
		}
	};

	// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/player_ball_rewards.py
	class FaceBallReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			Vec dirToBall = (state.ball.pos - player.pos).Normalized();
			return player.rotMat.forward.Dot(dirToBall);
		}
	};

	class TouchBallReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			return player.ballTouchedStep;
		}
	};

	class SpeedReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			return player.vel.Length() / CommonValues::CAR_MAX_SPEED;
		}
	};

	class WavedashReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			if (!player.prev)
				return 0;

			if (player.isOnGround && (player.prev->isFlipping && !player.prev->isOnGround)) {
				return 1;
			} else {
				return 0;
			}
		}
	};

	class PickupBoostReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev)
				return 0;

			if (player.boost > player.prev->boost) {
				return sqrtf(player.boost / 100.f) - sqrtf(player.prev->boost / 100.f);
			} else {
				return 0;
			}
		}
	};

	// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/misc_rewards.py
	class SaveBoostReward : public Reward {
	public:
		float exponent;
		SaveBoostReward(float exponent = 0.5f) : exponent(exponent) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			return RS_CLAMP(powf(player.boost / 100, exponent), 0, 1);
		}
	};

	class AirReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			return !player.isOnGround;
		}
	};

	// Touch height (Nexto's touch_height), impulse-scaled: on touch, pays
	// (2 - onGround) * avg(ballZ, carZ)/CEILING_Z * min(1, |delta ballVel|/FULL_CREDIT_DELTA_V).
	// ballTouchedStep fires EVERY step of sustained contact, so without the impulse factor
	// this would be a carry annuity (wall pins / ceiling carries out-earn goals). A carry
	// only adds ~67 uu/s of ball speed per step (impulse factor ~0.07 -> pays ~0); a real
	// strike gets the full height credit. Airborne touches pay double.
	class TouchHeightReward : public Reward {
	public:
		constexpr static float FULL_CREDIT_DELTA_V = 1000; // uu/s of ball delta-v for full credit

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev || !player.ballTouchedStep)
				return 0;

			float heightFrac = 0.5f * (state.ball.pos.z + player.pos.z) / CommonValues::CEILING_Z;
			float airMult = player.isOnGround ? 1.0f : 2.0f;
			float impulseFrac = RS_MIN(1.0f, (state.ball.vel - state.prev->ball.vel).Length() / FULL_CREDIT_DELTA_V);
			return airMult * heightFrac * impulseFrac;
		}
	};

	class StrongTouchReward : public Reward {
	public:
		float minRewardedVel, maxRewardedVel;
		StrongTouchReward(float minSpeedKPH = 20, float maxSpeedKPH = 130) {
			minRewardedVel = RLGC::Math::KPHToVel(minSpeedKPH);
			maxRewardedVel = RLGC::Math::KPHToVel(maxSpeedKPH);
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			if (player.ballTouchedStep) {
				float hitForce = (state.ball.vel - state.prev->ball.vel).Length();
				if (hitForce < minRewardedVel)
					return 0;

				return RS_MIN(1, hitForce / maxRewardedVel);
			} else {
				return 0;
			}
		}
	};

	// ========================================================================
	// FRONTIER-9 additions (workflow-designed, red-team verified against the
	// actual GameEventTracker/Arena source before shipping).
	// ========================================================================

	// FLIP RESET (2026-07-20, user-directed: a ZERO-RATE mechanic - the bot never
	// enters the precursor state, so amplification can't help and it needs both
	// SEEDING (AirPlayState) and this gradient). A flip reset = restoring your flip
	// by contacting the ball with your WHEELS while airborne. There is no honest PBRS
	// potential for it (its value is purely instrumental - the flick AFTER the
	// reset), so it is a gated EVENT reward, farm-hardened four ways: (1) a GENUINE
	// reset only - the engine's own HasFlipReset() must transition false->true via a
	// ball touch this step; (2) roof pointing at the ball (rotMat.up . toBall > 0.3),
	// i.e. wheels-first contact, not a nose poke; (3) ball genuinely high; (4) ~1s
	// cooldown so a tight juggle can't rack it up. Zero-sum wrapped like every event
	// term. The residual farm (ceiling-ball juggling for resets) advances the ball
	// nowhere, so B2G/Goal/TimeCost dominate its gradient once scoring exists.
	// SCAFFOLD weight - anneal once mechanic_census shows a stable flip-reset rate.
	class FlipResetReward : public Reward {
	public:
		constexpr static float BALL_MIN_Z = 500;             // genuinely aerial
		constexpr static float BALL_FULL_Z = 1400;
		constexpr static float ROOF_TO_BALL_MIN = 0.3f;      // wheels-toward-ball geometry
		constexpr static int   COOLDOWN_STEPS = 15;          // ~1s at 15Hz (tickSkip 8)
		std::vector<int> sincePay; // per player.index; per-arena instance, safe

		virtual void Reset(const GameState& initialState) override {
			sincePay.assign(initialState.players.size(), COOLDOWN_STEPS + 1);
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if ((size_t)player.index >= sincePay.size())
				sincePay.resize(player.index + 1, COOLDOWN_STEPS + 1);
			int& sp = sincePay[player.index];
			sp++;

			if (!state.prev || !player.prev)
				return 0;
			// The reset EVENT: touched the ball, airborne, flip restored THIS step
			// (transition), not the whole airborne-reset window.
			if (!player.ballTouchedStep || player.isOnGround)
				return 0;
			if (!player.HasFlipReset() || player.prev->HasFlipReset())
				return 0;
			if (sp <= COOLDOWN_STEPS)
				return 0;

			Vec toBall = state.ball.pos - player.pos;
			float d = toBall.Length();
			if (d < 1e-3f)
				return 0;
			// Roof toward ball = wheels made the contact (the reset geometry)
			if (player.rotMat.up.Dot(toBall * (1.f / d)) < ROOF_TO_BALL_MIN)
				return 0;

			float ballFrac = RS_CLAMP((state.ball.pos.z - BALL_MIN_Z) / (BALL_FULL_Z - BALL_MIN_Z), 0, 1);
			if (ballFrac <= 0)
				return 0;
			sp = 0;
			return ballFrac;
		}
	};

}