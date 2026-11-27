#pragma once
#include "CommonRewards.h"

#include <deque>
#include <unordered_map>

namespace RLGC {
	// Productive wavedash (2026-11-26, user-directed: "speed gained in any direction, slight bonus (25%) if towards the ball").
	// Paid on the landing step of a dodge (WavedashReward::Condition: floor, wall or ramp), for the speed the car has ABOVE the best
	// it already had in the last WINDOW_SECS before the dodge:
	//     gain  = |v_land| - sqrt(max(0, E_ref - 2 g z_land)),   E_ref = max over that window of (|v|^2 + 2 g z)
	//     value = clip(gain / FULL_GAIN, 0, 1) * (1 + BALL_BONUS * max(0, cos(velocity, car -> ball)))
	// Comparing energies (speed and height together) means falling pays nothing and climbing a wall is not penalised; comparing with
	// the window's best means braking and dashing back to the old speed pays nothing. No cooldown: a dash that adds speed is paid
	// however often it happens, and near the 2300 uu/s limit there is nothing left to gain.
	class WavedashGainReward : public Reward {
	public:
		static constexpr float FULL_GAIN = 500.f;   // uu/s of gain worth 1 (about one dodge impulse)
		static constexpr float BALL_BONUS = 0.25f;
		static constexpr float WINDOW_SECS = 1.0f;
		static constexpr float GRAVITY = 650.f;

		static float Energy(const Vec& vel, float z) { return vel.Dot(vel) + 2 * GRAVITY * z; }

		static float Value(float referenceEnergy, const Vec& pos, const Vec& vel, const Vec& ballPos) {
			const float expected = sqrtf(RS_MAX(0.f, referenceEnergy - 2 * GRAVITY * pos.z));
			const float speed = vel.Length();
			const float gain = RS_CLAMP((speed - expected) / FULL_GAIN, 0.f, 1.f);
			if (gain <= 0)
				return 0;
			const Vec toBall = ballPos - pos;
			const float distance = toBall.Length();
			const float aim = (distance > 1e-3f && speed > 1e-3f) ? RS_MAX(0.f, vel.Dot(toBall) / (distance * speed)) : 0.f;
			return gain * (1 + BALL_BONUS * aim);
		}

		virtual void Reset(const GameState& initialState) override { cars.clear(); }

		virtual void PreStep(const GameState& state) override {
			for (const Player& p : state.players) {
				Car& c = cars[p.carId];
				c.landed = false;
				if (p.isDemoed || !p.prev) { // a new life or a new episode: no history to compare with
					c = Car();
					continue;
				}
				const Player& before = *p.prev;
				const bool dodging = p.isFlipping && !p.isOnGround;
				const bool wasDodging = before.isFlipping && !before.isOnGround;
				if (!wasDodging) {
					// the state before this step is part of the run-up: remember it, and drop what is older than the window
					c.history.push_back({ c.clock, Energy(before.vel, before.pos.z) });
					while (!c.history.empty() && c.clock - c.history.front().first > WINDOW_SECS)
						c.history.pop_front();
					if (dodging || p.isFlipping) { // the dodge starts in this step: fix the reference before its impulse counts
						c.reference = 0;
						for (auto& h : c.history)
							c.reference = RS_MAX(c.reference, h.second);
						c.armed = true;
					}
				}
				c.clock += state.deltaTime;
				if (WavedashReward::Condition(p) && c.armed) {
					c.landed = true;
					c.armed = false;
				}
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			auto it = cars.find(player.carId);
			if (it == cars.end() || !it->second.landed)
				return 0;
			return Value(it->second.reference, player.pos, player.vel, state.ball.pos);
		}

	private:
		struct Car {
			float clock = 0, reference = 0;
			bool armed = false, landed = false;
			std::deque<std::pair<float, float>> history; // (time, energy) of recent states outside a dodge
		};
		std::unordered_map<uint32_t, Car> cars;
	};
}
