#pragma once
#include "StateSetter.h"
#include "../Math.h"

namespace RLGC {
	// BALL AND PLAYERS NEAR A GOAL (2026-11-18, user-directed, for the fresh-start 1v1 GCO run, docs/GCO_PURE_SELFPLAY_ARM.md).
	// A goal-only reward is sparse; a fresh policy that has never scored needs resets where a goal is a few seconds of play away
	// for EITHER side. One team is picked at random as the ATTACKER (blue attacks +y, orange -y, so the setter is colour-symmetric):
	//   * the ball sits 600..2200 uu in front of the attacked goal line, x in +-1500, 55% on the ground / 45% falling from
	//     150..900 uu, rolling 0..1000 uu/s (half the time roughly at the goal, half in a random horizontal direction);
	//   * the attacker's contester spawns 500..1100 uu from the ball on the side AWAY from the goal (within +-70 degrees of
	//     the ball->away-from-goal direction), facing the ball, on the ground, at rest;
	//   * the defender's contester spawns in the goal mouth zone (|x| <= 700, 4300..4900 uu from the centre line toward the attacked
	//     goal, facing the ball) 70% of the time and out of position (1500..3000 uu from the ball) 30%, so the attacker sometimes has an
	//     open net and sometimes a save to beat;
	//   * extra cars (2v2/3v3): attacker supports 1500..3000 uu behind the ball (+-90 degrees), defender supports spread in the
	//     goal zone.
	// Nothing is placed beyond the walls, the 45-degree corner planes, within 250 uu of the ball or within 300 uu of another car. The ball
	// starts away from the goal mouth itself, so a "free goal" with nobody touching is rare (measured in tests/test_near_goal_state.cpp).
	class NearGoalState : public StateSetter {
	public:
		float ballMinDepth, ballMaxDepth;   // Ball distance in front of the attacked goal line
		float groundBallFrac;               // Share of resets with the ball on the ground (the rest fall)
		float maxBallSpeed;                 // Horizontal ball speed 0..this
		float defenderInPositionFrac;       // Share of resets where the defender's contester starts in the goal zone

		NearGoalState(float ballMinDepth = 600, float ballMaxDepth = 2200, float groundBallFrac = 0.55f,
			float maxBallSpeed = 1000, float defenderInPositionFrac = 0.7f)
			: ballMinDepth(ballMinDepth), ballMaxDepth(ballMaxDepth), groundBallFrac(groundBallFrac),
			maxBallSpeed(maxBallSpeed), defenderInPositionFrac(defenderInPositionFrac) {
		}

		virtual void ResetArena(Arena* arena) {
			using RocketSim::Math::RandFloat;
			using RocketSim::Math::RandInt;

			arena->ResetToRandomKickoff(); // boost pads and everything else

			const Team attacker = RandFloat(0, 1) < 0.5f ? Team::BLUE : Team::ORANGE;
			const float sign = attacker == Team::BLUE ? +1.f : -1.f; // +y is the orange goal
			const float goalY = sign * CommonValues::GOAL_LINE_Y;

			BallState bs = {};
			bool grounded = RandFloat(0, 1) < groundBallFrac;
			bs.pos = Vec(RandFloat(-1500, 1500), goalY - sign * RandFloat(ballMinDepth, ballMaxDepth),
				grounded ? CommonValues::BALL_RADIUS : RandFloat(150, 900));
			{
				float theta;
				if (RandFloat(0, 1) < 0.5f) {
					// roughly at the goal: the direction to the goal centre, +-25 degrees
					Vec toGoal = Vec(0 - bs.pos.x, goalY - bs.pos.y, 0);
					theta = atan2f(toGoal.y, toGoal.x) + RandFloat(-0.44f, 0.44f);
				} else {
					theta = RandFloat(0, (float)M_PI * 2);
				}
				float speed = RandFloat(0, maxBallSpeed);
				bs.vel = Vec(cosf(theta), sinf(theta), 0) * speed;
			}
			arena->ball->SetState(bs);

			constexpr float WALL_MARGIN = 300;
			constexpr float CAR_SEPARATION = 300;
			constexpr float BALL_CLEARANCE = 250;
			const float clampX = CommonValues::SIDE_WALL_X - WALL_MARGIN;
			const float clampY = CommonValues::BACK_WALL_Y - WALL_MARGIN;

			std::vector<Vec> placed = {};
			auto fnClear = [&](const Vec& p) {
				if (!InsideCorners(p))
					return false;
				if ((Vec(p.x, p.y, 0) - Vec(bs.pos.x, bs.pos.y, 0)).Length() < BALL_CLEARANCE)
					return false;
				for (const Vec& o : placed)
					if ((p - o).Length() < CAR_SEPARATION)
						return false;
				return true;
			};

			// Ring around the ball, restricted to thetaCenter +- thetaHalfRange, in bounds and clear (rejection sampled)
			auto fnPlaceOnRing = [&](float ringMin, float ringMax, float thetaCenter, float thetaHalfRange) {
				Vec p = {};
				bool ok = false;
				for (int attempt = 0; attempt < 24 && !ok; attempt++) {
					float theta = thetaCenter + RandFloat(-thetaHalfRange, thetaHalfRange);
					p = bs.pos + Vec(cosf(theta), sinf(theta), 0) * RandFloat(ringMin, ringMax);
					p.z = 17;
					ok = fabsf(p.x) <= clampX && fabsf(p.y) <= clampY && fnClear(p);
				}
				if (!ok) { // best effort: pull inside the bounds, then off the ball
					p.x = RS_CLAMP(p.x, -clampX, clampX);
					p.y = RS_CLAMP(p.y, -clampY, clampY);
					p = ClampToCorners(p);
					p.z = 17;
				}
				placed.push_back(p);
				return p;
			};

			// Goal zone in front of the attacked line, for defenders
			auto fnPlaceInGoalZone = [&]() {
				Vec p = {};
				bool ok = false;
				for (int attempt = 0; attempt < 24 && !ok; attempt++) {
					p = Vec(RandFloat(-700, 700), sign * RandFloat(4300, 4900), 17);
					ok = fnClear(p);
				}
				if (!ok)
					return fnPlaceOnRing(1500, 3000, RandFloat(0, (float)M_PI * 2), (float)M_PI);
				placed.push_back(p);
				return p;
			};

			auto fnSetCar = [&](Car* car, Vec pos) {
				CarState cs = {};
				cs.pos = pos;
				Vec toBall = bs.pos - pos;
				cs.rotMat = Angle(atan2f(toBall.y, toBall.x), 0, 0).ToRotMat();
				cs.boost = RandFloat(30, 100);
				car->SetState(cs);
			};

			std::vector<Car*> teamCars[2];
			for (Car* car : arena->_cars)
				teamCars[(int)car->team].push_back(car);
			const int att = (int)attacker, def = 1 - att;

			// "away from the goal" direction from the ball
			Vec toGoal = Vec(0 - bs.pos.x, goalY - bs.pos.y, 0);
			float awayTheta = atan2f(-toGoal.y, -toGoal.x);

			// Contesters first (the attacker's, then the defender's), then supports
			if (!teamCars[att].empty()) {
				fnSetCar(teamCars[att][0], fnPlaceOnRing(500, 1100, awayTheta, 70.f * (float)M_PI / 180.f));
				for (size_t i = 1; i < teamCars[att].size(); i++)
					fnSetCar(teamCars[att][i], fnPlaceOnRing(1500, 3000, awayTheta, (float)M_PI / 2));
			}
			if (!teamCars[def].empty()) {
				bool inPosition = RandFloat(0, 1) < defenderInPositionFrac;
				fnSetCar(teamCars[def][0], inPosition ? fnPlaceInGoalZone() : fnPlaceOnRing(1500, 3000, RandFloat(0, (float)M_PI * 2), (float)M_PI));
				for (size_t i = 1; i < teamCars[def].size(); i++)
					fnSetCar(teamCars[def][i], fnPlaceInGoalZone());
			}
		}
	};
}
