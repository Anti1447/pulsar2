#pragma once
#include "../Reward.h"
#include "../RewardTiming.h"
#include "FlipResetGeometry.h"

namespace RLGC {

	// FlipResetSetupReward (2026-11-09, user-directed; design Q&A in docs/reward_scenarios/FlipResetShaping_2026-11-09.md).
	// Stage 1 of the flip-reset chain: controlling the ball ON THE GROUND (this player touched it within ~0.5 s and it is within 600 uu)
	// while it travels toward a wall / backboard / corner in the ATTACKING half. Individual one-sided shaping (wrap in ZeroSum).
	//  raw per step = SETUP_STEP_MAX x wallClosing(0..1: ball speed toward the nearest wall x nearness) x boostFactor(clamp(boost/50, 0.2, 1))
	//  Rev 2 (2026-11-09): ball speed >= 1000 into a side wall (>= 1000 far out down to 600 at the backboard: lower nearer the opponent goal), HARD cap 2300
	//  (a faster ball cannot be caught up to); speed-match bonus x(1 + 0.5 x matchFrac) when |car vel - ball vel| <= 200 (0 at 600+);
	//  and the LAUNCH touch (one-shot, raw 1.0 x boost factor x direction): the ball has rolled up the wall (2 s latch from a qualifying setup step),
	//  this car is ON the wall and touches it with both car and ball near the wall/backboard/corner. Direction: 1.0 if the predicted path reaches the
	//  opponent goal mouth, 0.5 if it enters (or, from the opponent half, stays in) the opponent half, 0 otherwise; 0 above 2300 uu/s. Spends the latch.
	//  Anti-farm: pays only while the ball CLOSES on the wall (hovering pays 0); a leaky bucket (max 1.0 raw, refill 0.1 raw/s) bounds the
	//  sustained income to 0.1 raw/s (1.0 weighted/s at weight 10) and the first 30 s of an episode to 4.0 raw.
	class FlipResetSetupReward : public Reward {
	public:
		constexpr static float SETUP_STEP_MAX = 0.05f;
		constexpr static float BUCKET_MAX = 1.0f;
		constexpr static float BUCKET_REFILL_PER_SEC = 0.1f;
		constexpr static float LAUNCH_MAX = 1.0f;
		constexpr static float LATCH_SEC = 2.0f;
		struct PS { int sinceTouch = 1000; float bucket = BUCKET_MAX; float latch = 0.f; };
		std::vector<PS> ps;

		virtual void Reset(const GameState& initialState) override { ps.assign(initialState.players.size(), PS()); }

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if ((size_t)player.index >= ps.size()) ps.resize(player.index + 1);
			PS& s = ps[player.index];
			s.bucket = RS_MIN(BUCKET_MAX, s.bucket + StepDt(state) * BUCKET_REFILL_PER_SEC);
			if (player.ballTouchedStep) s.sinceTouch = 0; else if (s.sinceTouch < 1000) s.sinceTouch++;

			const bool qualifies = FlipResetGeo::SetupQualifies(player, state, s.sinceTouch);
			if (qualifies) s.latch = LATCH_SEC; else s.latch = RS_MAX(0.f, s.latch - StepDt(state));

			// LAUNCH: the ball rolled up the wall and this car, on the wall, hits it
			if (!qualifies && player.ballTouchedStep && s.latch > 0.f && FlipResetGeo::LaunchGeometry(player, state.ball.pos)) {
				s.latch = 0.f;
				return LAUNCH_MAX * FlipResetGeo::LaunchDirFrac(player.team, state) * FlipResetGeo::BoostFactor(player.boost);
			}
			if (!qualifies)
				return 0.f;
			float raw = SETUP_STEP_MAX * FlipResetGeo::WallClosingFrac(player.team, state.ball.pos, state.ball.vel)
				* FlipResetGeo::BoostFactor(player.boost)
				* (1.f + FlipResetGeo::MATCH_BONUS * FlipResetGeo::SpeedMatchFrac(player, state.ball.vel));
			float pay = RS_MIN(raw, s.bucket);
			s.bucket -= pay;
			return pay;
		}
	};
}
