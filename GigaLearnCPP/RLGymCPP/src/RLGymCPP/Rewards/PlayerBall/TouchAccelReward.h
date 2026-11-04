#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"

namespace RLGC {

	// Mostly based on the classic Necto rewards. Reworked 2026-11-03 (audit findings 2 and 3, user-directed).
	// A touch pays the IMPULSE it gave the ball, limited by a per-player HIGH-WATER MARK of ball speed:
	//   pay = min( |v_now - v_prev| / M , max(0, S_now - H) ),   S = min(1, |v| / M)
	//   H   = the highest speed fraction this player's touches have left the ball at since the last reset;
	//         raised to S_now on every touch step of the player, and ZEROED when an opponent touches the ball.
	// - The mark makes it a ratchet-free budget: however many touches, the total paid between two opponent touches is at most 1.0
	//   (speed the ball loses between your touches is not paid again). A chain of rising speeds still telescopes to the final S.
	// - The impulse term (finding 3) lets a clear or return of a FAST ball pay: before, a head-on hit on a 2300 uu/s ball paid 0 because |v| did
	//   not rise. After an opponent touch H is 0, so the return is limited only by the impulse and by S_now.
	// - There is deliberately NO "the ball must speed up" gate (tried and removed the same day: the engine's head-on return of a 2300 uu/s ball leaves it
	//   at 1578 uu/s, slower than it came, and the gate paid it 0). A cushion that kills the ball pays only min(impulse, its small exit speed fraction),
	//   and the mark bounds the total anyway.
	// Direction is still ignored (BallToGoal and friends carry that). M = KPHToVel(110).
	class TouchAccelReward : public Reward {
	public:
		constexpr static float MAX_REWARDED_BALL_SPEED = RLGC::Math::KPHToVel(110);

		std::map<uint32_t, float> highWater; // by carId: speed fraction already paid for since the last opponent touch
		std::map<uint32_t, float> pay;       // by carId: this step's reward, computed in PreStep

		virtual void Reset(const GameState& initialState) override {
			highWater.clear();
			pay.clear();
		}

		virtual void PreStep(const GameState& state) override {
			pay.clear();

			// An opponent's touch ends this player's claim on the speed already paid for
			for (const Player& a : state.players) {
				if (!a.ballTouchedStep)
					continue;
				for (const Player& b : state.players)
					if (b.team != a.team)
						highWater[b.carId] = 0;
			}

			if (!state.prev)
				return;
			float curFrac = RS_MIN(1, state.ball.vel.Length() / MAX_REWARDED_BALL_SPEED);
			float impulseFrac = RS_MIN(1, (state.ball.vel - state.prev->ball.vel).Length() / MAX_REWARDED_BALL_SPEED);

			for (const Player& p : state.players) {
				if (!p.ballTouchedStep)
					continue;
				float& h = highWater[p.carId];
				if (curFrac > h)
					pay[p.carId] = RS_MIN(impulseFrac, curFrac - h);
				h = RS_MAX(h, curFrac);
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			auto it = pay.find(player.carId);
			return it == pay.end() ? 0.f : it->second;
		}
	};
}
