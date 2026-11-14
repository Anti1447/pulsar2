#pragma once
#include "StateSetter.h"
#include "../Math.h"

namespace RLGC {
	// DemoLaneState (2026-11-13, user-directed; docs/reward_scenarios/DemoLane_2026-11-13.md).
	// Seeds the APPROACH to a demo so the situation is visited often enough to get a gradient. Reward alone cannot shape a state the bot never enters.
	//
	// One random team is the ATTACKER (attacks its usual direction: BLUE +y, ORANGE -y). One random opposing car is the VICTIM, standing goal-side of
	// a ground ball (the geometry OffensiveDemoReward pays most for: near the ball, inside the ball -> posts lane). One random attacker car is placed
	// BEHIND the victim, facing it, already moving toward it with fuel, so the supersonic demo run is available from the first step.
	// Every other car (teammates of either side, the second/third opponent) is placed far from the lane at rest.
	//
	// The reset is a fixed, non-adaptive part of the reset mix (no per-state feedback), so it is a constant exposure lever (constraint C1).
	// Invariants (tests/test_demo_lane.cpp): attacker 2000..2800 uu from the victim, moving at 1000..2000 uu/s within ~50 deg of the line to it, boost >= 60;
	// victim 500..1000 uu goal-side of the ball in y, ball at rest on the ground, >= 1000 uu from the attacker; everything in bounds; no overlapping cars.
	class DemoLaneState : public StateSetter {
	public:
		float minRun, maxRun;       // attacker -> victim distance
		float minSpeed, maxSpeed;   // attacker initial speed along the run
		float maxVictimSpeed;       // victim drifts at 0..this in a random direction

		DemoLaneState(float minRun = 2000, float maxRun = 2800, float minSpeed = 1000, float maxSpeed = 2000, float maxVictimSpeed = 600)
			: minRun(minRun), maxRun(maxRun), minSpeed(minSpeed), maxSpeed(maxSpeed), maxVictimSpeed(maxVictimSpeed) {}

		virtual void ResetArena(Arena* arena) {
			using RocketSim::Math::RandFloat;
			using RocketSim::Math::RandInt;

			arena->ResetToRandomKickoff(); // boost pads and everything

			std::vector<Car*> teamCars[2];
			for (Car* car : arena->_cars)
				teamCars[(int)car->team].push_back(car);

			constexpr float SEP = 900;
			std::vector<Vec> placed;
			auto fnSet = [&](Car* car, Vec pos, float yaw, Vec vel, float boost) {
				CarState cs = {};
				pos.z = 17;
				cs.pos = pos; cs.vel = vel; cs.boost = boost;
				cs.rotMat = Angle(yaw, 0, 0).ToRotMat();
				car->SetState(cs);
				placed.push_back(pos);
			};

			BallState bs = {};
			Car* attacker = nullptr; Car* victim = nullptr;
			const bool haveBoth = !teamCars[0].empty() && !teamCars[1].empty();
			if (haveBoth) {
				const int at = RandInt(0, 2);
				const float s = at == 0 ? 1.0f : -1.0f; // BLUE (team 0) attacks +y
				attacker = teamCars[at][RandInt(0, (int)teamCars[at].size())];
				victim = teamCars[1 - at][RandInt(0, (int)teamCars[1 - at].size())];

				Vec ballPos, vicPos, attPos, heading;
				for (int attempt = 0; attempt < 32; attempt++) {
					ballPos = Vec(RandFloat(-2200, 2200), s * RandFloat(300, 2800), CommonValues::BALL_RADIUS);
					vicPos = Vec(ballPos.x * 0.6f + RandFloat(-500, 500), ballPos.y + s * RandFloat(500, 1000), 17);
					const float ang = RandFloat(-0.87f, 0.87f); // +-50 deg off the attack axis
					heading = Vec(sinf(ang), s * cosf(ang), 0);
					const float run = RandFloat(minRun, maxRun);
					attPos = vicPos - heading * run;
					const bool ok = fabsf(vicPos.x) < 3400 && fabsf(vicPos.y) < 4400 && fabsf(attPos.x) < 3700 && fabsf(attPos.y) < 4700
						&& (attPos - ballPos).Length() >= 1000;
					if (ok) break;
					if (attempt == 31) { // deterministic in-bounds fallback: straight run up the middle
						ballPos = Vec(0, s * 1000, CommonValues::BALL_RADIUS);
						vicPos = Vec(0, s * 1700, 17);
						heading = Vec(0, s, 0);
						attPos = vicPos - heading * minRun;
					}
				}
				bs.pos = ballPos;
				arena->ball->SetState(bs);

				fnSet(attacker, attPos, atan2f(heading.y, heading.x), heading * RandFloat(minSpeed, maxSpeed), RandFloat(60, 100));
				const float vYaw = RandFloat(0, 2 * M_PI);
				fnSet(victim, vicPos, vYaw, Vec(cosf(vYaw), sinf(vYaw), 0) * RandFloat(0, maxVictimSpeed), RandFloat(0, 100));
			} else {
				bs.pos = Vec(RandFloat(-2200, 2200), RandFloat(-2800, 2800), CommonValues::BALL_RADIUS);
				arena->ball->SetState(bs);
			}

			// everyone else: rest, random yaw, far from every placed car and the ball
			for (Car* car : arena->_cars) {
				if (car == attacker || car == victim) continue;
				Vec p;
				for (int attempt = 0; attempt < 32; attempt++) {
					p = Vec(RandFloat(-3400, 3400), RandFloat(-4400, 4400), 17);
					bool clear = (p - bs.pos).Length() >= 600;
					for (const Vec& o : placed) if ((p - o).Length() < SEP) clear = false;
					if (clear) break;
				}
				fnSet(car, p, RandFloat(0, 2 * M_PI), Vec(0, 0, 0), RandFloat(0, 100));
			}
		}
	};
}
