#pragma once
#include "StateSetter.h"
#include "../Math.h"
#include "../CommonValues.h"

namespace RLGC {
	// ADVANCED-AIR SEEDING (2026-07-20, user-directed). Flip resets, air dribbles
	// and wall play are ZERO-RATE: the policy never enters the precursor states, so
	// no reward can shape them (reward only shapes visited states) and no
	// amplification can find a fragment that is never sampled. This setter drops the
	// car straight INTO those setups so the rest of the stack (FlipResetReward,
	// AerialTouchReward, AirInterceptPotential, energy, B2G, Goal) can extend them
	// backward - the same reverse-curriculum logic as AirDrillState, one rung higher.
	//
	// Three sub-modes, chosen per reset (WALL_TO_AIR added 2026-11-11, user-directed; see below):
	//   FLIP_RESET_READY  car airborne below a high ball, WHEELS toward it (roof away,
	//                     wheels first), flip already spent (hasJumped) so a wheel touch
	//                     REGISTERS as a reset, drifting up into contact. (Until 2026-11-07
	//                     this put the ROOF toward the ball, a pose that cannot reset.)
	//   AIR_CARRY         a ball moving toward the opponent net and rising, car just
	//                     behind/under it at matched velocity, nose on it - the
	//                     air-dribble carry, ready to keep nudging it goalward.
	//   WALL_TO_AIR       a ball ROLLING on the ground toward a wall, a corner or the backboard
	//                     at 300..2300 uu/s, with ONE blue car behind it (250..600 uu, same heading,
	//                     speed near the ball's, 50..100 boost): the start of a wall-to-air play
	//                     (carry it up the wall, launch it, then the flip-reset / air-dribble chain).
	//                     Wall mix: side walls 70% / backboard 15% / corner 15%; side-wall height
	//                     along the pitch is triangular (peaked at midfield, tapering toward the
	//                     back corners). Every other car, including all orange cars, is placed
	//                     as in the other modes (grounded support goal-side of the ball).
	//                     wallFrac of the resets are this mode; the rest keep the old
	//                     resetFrac split between FLIP_RESET_READY and AIR_CARRY.
	//
	// TEAM-AWARE (mirrors AirDrillState): exactly one random car per team is the
	// air-player (both teams' air-players share the same ball, a symmetric contest);
	// the rest spawn grounded goal-side as support. 1v1 = just the air-player.
	class AirPlayState : public StateSetter {
	public:
		float carrySpeed;   // ball speed for the AIR_CARRY mode
		float resetFrac;    // fraction of resets that are FLIP_RESET_READY (rest AIR_CARRY)
		float supportMinDist, supportMaxDist;
		float wallFrac;     // fraction of ALL resets that are WALL_TO_AIR (the rest split by resetFrac)

		// WALL_TO_AIR constants
		static constexpr float WALL_SIDE_FRAC = 0.70f;      // of wall resets: side wall; backboard and corner are the rest, equally
		static constexpr float WALL_BACKBOARD_FRAC = 0.15f; // corner = 1 - side - backboard = 0.15
		static constexpr float WALL_BALL_SPEED_MIN = 300.f, WALL_BALL_SPEED_MAX = 2300.f;
		static constexpr float WALL_SIDE_Y_MAX = 3800.f;    // side-wall contact point |y| (triangular: peaked at 0)
		static constexpr float WALL_BACKBOARD_X_MIN = 1300.f, WALL_BACKBOARD_X_MAX = 3200.f; // outside the goal mouth (|x| < 893 + ball radius)
		static constexpr float WALL_ANGLE_JITTER = 0.61f;   // rad (~35 deg) off the wall normal
		static constexpr float WALL_CAR_BEHIND_MIN = 250.f, WALL_CAR_BEHIND_MAX = 600.f;

		AirPlayState(float carrySpeed = 900, float resetFrac = 0.5f,
			float supportMinDist = 1500, float supportMaxDist = 3000, float wallFrac = 0.30f)
			: carrySpeed(carrySpeed), resetFrac(resetFrac),
			  supportMinDist(supportMinDist), supportMaxDist(supportMaxDist), wallFrac(wallFrac) {}

		enum WallKind { WALL_SIDE = 0, WALL_BACKBOARD = 1, WALL_CORNER = 2 };

		// Samples one wall setup: the ball start, its heading `dir` (horizontal unit), its speed, and the car start behind it. Retries until the ball AND the car
		// start inside the pitch (the corner wall planes poke in at |x|+|y| ~ 8064); falls back to a midfield side-wall roll, so it always succeeds.
		static void SampleWall(Vec& ballPos, Vec& dir, float& speed, Vec& carPos, int& kind) {
			using RocketSim::Math::RandFloat;
			auto fnInside = [](const Vec& p, float xMax, float yMax, float cornerMax) {
				return fabsf(p.x) <= xMax && fabsf(p.y) <= yMax && fabsf(p.x) + fabsf(p.y) <= cornerMax;
			};
			for (int attempt = 0; attempt < 48; attempt++) {
				float r = RandFloat(0.f, 1.f);
				kind = (r < WALL_SIDE_FRAC) ? WALL_SIDE : (r < WALL_SIDE_FRAC + WALL_BACKBOARD_FRAC ? WALL_BACKBOARD : WALL_CORNER);
				float sx = RandFloat(0.f, 1.f) < 0.5f ? -1.f : 1.f, sy = RandFloat(0.f, 1.f) < 0.5f ? -1.f : 1.f;
				Vec target; // where the ball meets the wall
				float baseAngle;
				if (kind == WALL_SIDE) {
					float ty = (RandFloat(-1.f, 1.f) + RandFloat(-1.f, 1.f)) * 0.5f * WALL_SIDE_Y_MAX;
					target = Vec(sx * CommonValues::SIDE_WALL_X, ty, 0);
					baseAngle = atan2f(0.f, sx);
					baseAngle += RandFloat(-WALL_ANGLE_JITTER, WALL_ANGLE_JITTER);
				} else if (kind == WALL_BACKBOARD) {
					float tx = sx * RandFloat(WALL_BACKBOARD_X_MIN, WALL_BACKBOARD_X_MAX);
					target = Vec(tx, sy * CommonValues::BACK_WALL_Y, 0);
					baseAngle = atan2f(sy, 0.f);
					baseAngle += RandFloat(-WALL_ANGLE_JITTER, WALL_ANGLE_JITTER);
				} else {
					target = Vec(sx * 3700.f, sy * 4350.f, 0); // on the 45 degree corner wall
					baseAngle = atan2f(sy * RandFloat(0.5f, 0.9f), sx * RandFloat(0.5f, 0.9f));
				}
				dir = Vec(cosf(baseAngle), sinf(baseAngle), 0);
				speed = RandFloat(WALL_BALL_SPEED_MIN, WALL_BALL_SPEED_MAX);
				float rollSec = RandFloat(0.7f, 1.8f); // RS_CLAMP is a macro: keep RNG calls out of its arguments
				float dWall = RS_CLAMP(speed * rollSec, 450.f, 2400.f); // 0.7..1.8 s of rolling, never closer than 450 uu
				ballPos = Vec(target.x - dir.x * dWall, target.y - dir.y * dWall, CommonValues::BALL_RADIUS);
				float behind = RandFloat(WALL_CAR_BEHIND_MIN, WALL_CAR_BEHIND_MAX);
				carPos = Vec(ballPos.x - dir.x * behind, ballPos.y - dir.y * behind, 17.f);
				if (fnInside(ballPos, 3900.f, 4800.f, 7900.f) && fnInside(carPos, 3800.f, 4700.f, 7800.f))
					return;
			}
			kind = WALL_SIDE; dir = Vec(1, 0, 0); speed = 1200.f;
			ballPos = Vec(4096.f - 1500.f, 0, CommonValues::BALL_RADIUS);
			carPos = Vec(ballPos.x - 400.f, 0, 17.f);
		}

		virtual void ResetArena(Arena* arena) {
			using RocketSim::Math::RandFloat;
			using RocketSim::Math::RandInt;
			using namespace CommonValues;

			arena->ResetToRandomKickoff();

			const bool wallMode = RandFloat(0, 1) < wallFrac;
			bool flipReset = !wallMode && RandFloat(0, 1) < resetFrac;

			// Attack direction is shared by the setup; pick a random attacker frame so
			// the ball heads toward ONE net (BLUE attacks +y, ORANGE attacks -y). For
			// the reset mode direction is cosmetic; for carry it sets the goalward line.
			float attackSign = RandInt(0, 2) ? 1.f : -1.f;

			BallState bs = {};
			Vec wallDir = {}, wallCarPos = {};
			float wallSpeed = 0; int wallKind = 0;
			if (wallMode) {
				// Ball ROLLING toward a wall/corner/backboard: horizontal velocity, rolling spin (no slip on the ground).
				SampleWall(bs.pos, wallDir, wallSpeed, wallCarPos, wallKind);
				bs.vel = wallDir * wallSpeed;
				bs.angVel = Vec(-bs.vel.y / CommonValues::BALL_RADIUS, bs.vel.x / CommonValues::BALL_RADIUS, 0);
			} else if (flipReset) {
				// High, slow ball in the field interior.
				bs.pos = Math::RandVec(Vec(-2200, -3000, 1200), Vec(2200, 3000, 1700));
				bs.vel = Vec(RandFloat(-150, 150), RandFloat(-150, 150), RandFloat(-250, 50));
			} else {
				// Mid-height ball moving toward the attacked net and rising - a carry
				// in progress.
				bs.pos = Vec(RandFloat(-2000, 2000), attackSign * RandFloat(-500, 1500),
					RandFloat(350, 800));
				bs.vel = Vec(RandFloat(-200, 200), attackSign * carrySpeed, RandFloat(150, 450));
			}
			arena->ball->SetState(bs);

			constexpr float WALL_MARGIN = 300;
			constexpr float CAR_SEPARATION = 350;
			const float clampX = SIDE_WALL_X - WALL_MARGIN;
			const float clampY = BACK_WALL_Y - WALL_MARGIN;

			std::vector<Vec> placed = {};
			auto fnOverlaps = [&](const Vec& p) {
				for (Vec& o : placed)
					if ((p - o).Length() < CAR_SEPARATION) return true;
				return false;
			};

			// The two air-players (one per team) get mirrored slots around the ball, so neither starts closer by construction and
			// they never coincide. Until 2026-11-17 both were sampled at the same spot and the second (orange) was lifted 350 uu
			// on overlap: flip-reset put that car inside the ball in 37% of resets (orange first touch 741 vs 251), carry let blue
			// start closer in 67% and score 60% (docs/GCO_RESET_MIX.md P2). Now:
			//   flip-reset  below the ball on OPPOSITE sides of its vertical axis (shared random bearing, slot 1 = +180 deg),
			//               horizontal offset 180..280 each, so the cars are >= 360 apart and both see the ball wheels-first.
			//   carry       behind and below the ball on OPPOSITE lateral (x) sides, 180..260 each, so >= 360 apart.
			// Each slot's distribution is identical; the bearing / side sign is random, so the slots are colour-fair.
			const float airBearing = RandFloat(0, 2 * M_PI);
			const float carrySide = RandInt(0, 2) ? 1.f : -1.f;
			auto fnSetAir = [&](Car* car, int slot) {
				CarState cs = {};
				Vec carPos = {};
				const float slotSign = slot == 0 ? 1.f : -1.f;

				if (flipReset) {
					// Below the ball so "down" (the wheels) points at it.
					float drop = RandFloat(300, 520);
					float h = RandFloat(180, 280);
					carPos = bs.pos + Vec(cosf(airBearing) * h * slotSign, sinf(airBearing) * h * slotSign, -drop);
				} else {
					// Behind (goal-side of the ball's travel), just below it, to one side.
					carPos = bs.pos - Vec(0, attackSign * RandFloat(250, 500), RandFloat(60, 200));
					carPos.x += carrySide * slotSign * RandFloat(180, 260);
				}
				carPos.x = RS_CLAMP(carPos.x, -clampX, clampX);
				carPos.y = RS_CLAMP(carPos.y, -clampY, clampY);
				carPos = ClampToCorners(carPos);
				carPos.z = RS_MAX(300.f, carPos.z); // clearly airborne
				placed.push_back(carPos);
				cs.pos = carPos;

				Vec toBall = (bs.pos - carPos);
				Vec toBallN = toBall.Normalized();
				if (flipReset) {
					// WHEELS toward the ball (wheel contact = a reset): the car's up axis
					// points AWAY from the ball. Forward is any in-plane direction; LookAt
					// orthogonalizes against the up hint, so pass a horizontal forward and
					// up = -toBall.
					Vec fwd = Vec(toBallN.y, -toBallN.x, 0);
					if (fwd.Length() < 0.1f) fwd = Vec(1, 0, 0);
					cs.rotMat = RotMat::LookAt(fwd.Normalized(), toBallN * -1.f);
					// Flip already SPENT so a wheel touch registers as a reset.
					cs.hasJumped = true;
					cs.hasFlipped = true;
					cs.airTime = 0.6f;
					cs.airTimeSinceJump = 0.6f;
					// Drift up into the ball.
					cs.vel = toBallN * RandFloat(300, 650);
				} else {
					// Nose on the ball, roof up, velocity matched to the ball (carry).
					cs.rotMat = RotMat::LookAt(toBallN, Vec(0, 0, 1));
					cs.vel = bs.vel + toBallN * RandFloat(50, 200);
				}
				cs.isOnGround = false;
				cs.boost = RandFloat(50, 100);
				car->SetState(cs);
			};

			// The WALL_TO_AIR setup car: grounded behind the rolling ball, same heading, speed near the ball's (it can follow it up to the car's 2300 top speed).
			auto fnSetWall = [&](Car* car) {
				CarState cs = {};
				cs.pos = wallCarPos;
				placed.push_back(wallCarPos);
				cs.rotMat = Angle(atan2f(wallDir.y, wallDir.x), 0, 0).ToRotMat();
				float carSpeed = wallSpeed + RandFloat(-100.f, 250.f);
				cs.vel = wallDir * RS_CLAMP(carSpeed, 0.f, 2300.f);
				cs.boost = RandFloat(50, 100);
				car->SetState(cs);
			};

			auto fnSetSupport = [&](Car* car, Team team) {
				Vec ownGoal = (team == Team::BLUE) ? BLUE_GOAL_BACK : ORANGE_GOAL_BACK;
				Vec ballShadow = Vec(bs.pos.x, bs.pos.y, 0);
				Vec toGoal = ownGoal - ballShadow;
				float goalTheta = atan2f(toGoal.y, toGoal.x);
				Vec carPos = {};
				bool ok = false;
				for (int a = 0; a < 16 && !ok; a++) {
					float theta = goalTheta + RandFloat(-M_PI / 2, M_PI / 2);
					float dist = RandFloat(supportMinDist, supportMaxDist);
					carPos = ballShadow + Vec(cosf(theta) * dist, sinf(theta) * dist, 0);
					carPos.z = 17;
					if (fabsf(carPos.x) > clampX || fabsf(carPos.y) > clampY || !InsideCorners(carPos)) continue;
					ok = !fnOverlaps(carPos);
				}
				if (!ok) {
					carPos.x = RS_CLAMP(carPos.x, -clampX, clampX);
					carPos.y = RS_CLAMP(carPos.y, -clampY, clampY);
					carPos = ClampToCorners(carPos);
					carPos.z = 17;
				}
				placed.push_back(carPos);
				CarState cs = {};
				cs.pos = carPos;
				Vec toBall = bs.pos - carPos;
				cs.rotMat = Angle(atan2f(toBall.y, toBall.x), 0, 0).ToRotMat();
				cs.boost = RandFloat(50, 100);
				car->SetState(cs);
			};

			std::vector<Car*> teamCars[2];
			for (Car* car : arena->_cars)
				teamCars[(int)car->team].push_back(car);
			if (wallMode) {
				// ONE car is the setup car behind the rolling ball; every other car is grounded support. It is a blue car when the teams are
				// equal; in an uneven arena (2026-11-26) either colour, so the bigger and the smaller side each get it on both colours.
				// SampleWall draws every wall with either sign, so the geometry is the same for both colours.
				const int setupTeam = teamCars[0].size() != teamCars[1].size() ? RandInt(0, 2) : 0;
				int setupIdx = teamCars[setupTeam].size() > 1 ? RandInt(0, (int)teamCars[setupTeam].size()) : 0;
				if (!teamCars[setupTeam].empty())
					fnSetWall(teamCars[setupTeam][setupIdx]);
				for (int t = 0; t < 2; t++)
					for (int i = 0; i < (int)teamCars[t].size(); i++)
						if (!(t == setupTeam && i == setupIdx))
							fnSetSupport(teamCars[t][i], (Team)t);
				return;
			}

			int airIdx[2] = {};
			for (int t = 0; t < 2; t++)
				if (teamCars[t].size() > 1)
					airIdx[t] = RandInt(0, (int)teamCars[t].size());

			for (int t = 0; t < 2; t++)
				if (!teamCars[t].empty())
					fnSetAir(teamCars[t][airIdx[t]], t);
			for (int t = 0; t < 2; t++)
				for (int i = 0; i < (int)teamCars[t].size(); i++)
					if (i != airIdx[t])
						fnSetSupport(teamCars[t][i], (Team)t);
		}
	};
}
