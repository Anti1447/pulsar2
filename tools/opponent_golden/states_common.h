// Shared by the opponent golden dumpers (tools/opponent_golden): the recorded-state generator.
// Real RocketSim states: every kickoff layout, random drives, aerials, wall/ceiling, flips, demolished cars, spent
// pads, ball in corners and nets, 1v1/2v2/3v3 (both teams are always present).
#pragma once
#include <RLGymCPP/Gamestates/GameState.h>
#include <RLGymCPP/CommonValues.h>

#include <filesystem>
#include <functional>
#include <iostream>
#include <random>
#include <string>

namespace golden {
	using namespace RLGC;

	inline std::mt19937_64& Rng() {
		static std::mt19937_64 rng;
		return rng;
	}
	inline float Rand(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(Rng()); }
	inline int RandInt(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(Rng()); }

	inline bool InitRocketSim() {
		for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
			if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) {
				RocketSim::Init(path);
				return true;
			}
		std::cerr << "no collision_meshes folder found\n";
		return false;
	}

	inline void RandomizeCar(Car* car, bool aerial) {
		CarState s = car->GetState();
		s.pos = Vec(Rand(-3500, 3500), Rand(-4500, 4500), aerial ? Rand(300, 1800) : 17.f);
		s.vel = Vec(Rand(-1500, 1500), Rand(-1500, 1500), aerial ? Rand(-800, 800) : 0.f);
		s.angVel = aerial ? Vec(Rand(-4, 4), Rand(-4, 4), Rand(-4, 4)) : Vec(0, 0, 0);
		s.rotMat = Angle(Rand(-3.14f, 3.14f), aerial ? Rand(-1.2f, 1.2f) : 0.f, aerial ? Rand(-3.14f, 3.14f) : 0.f).ToRotMat();
		s.boost = Rand(0, 100);
		car->SetState(s);
	}

	inline void RandomizeBall(Arena* arena, int kind) {
		BallState b = arena->ball->GetState();
		switch (kind) {
		case 0: b.pos = Vec(Rand(-3500, 3500), Rand(-4500, 4500), Rand(100, 1800)); break;
		case 1: b.pos = Vec(Rand(0, 1) < 0.5f ? -3900.f : 3900.f, Rand(0, 1) < 0.5f ? -4800.f : 4800.f, 150.f); break; // corner
		case 2: b.pos = Vec(Rand(-700, 700), Rand(0, 1) < 0.5f ? -5200.f : 5200.f, 150.f); break;                       // in a net
		default: b.pos = Vec(Rand(-3000, 3000), Rand(-4000, 4000), 93.f); break;
		}
		b.vel = Vec(Rand(-2000, 2000), Rand(-2000, 2000), Rand(-500, 900));
		b.angVel = Vec(Rand(-5, 5), Rand(-5, 5), Rand(-5, 5));
		arena->ball->SetState(b);
	}

	inline void ScrambleControls(Arena* arena) {
		for (Car* car : arena->_cars) {
			CarControls c = {};
			c.throttle = (float)RandInt(-1, 1);
			c.steer = (float)RandInt(-1, 1);
			c.pitch = (float)RandInt(-1, 1);
			c.yaw = (float)RandInt(-1, 1);
			c.roll = (float)RandInt(-1, 1);
			c.jump = RandInt(0, 3) == 0;
			c.boost = RandInt(0, 1);
			c.handbrake = RandInt(0, 4) == 0;
			car->controls = c;
		}
	}

	// Calls snapshot(arena, tag) for 64 states per mode (14 kickoff layouts + 50 drives)
	inline void GenerateStates(const std::function<void(Arena*, const std::string&)>& snapshot) {
		for (int perTeam = 1; perTeam <= 3; perTeam++) {
			Arena* arena = Arena::Create(GameMode::SOCCAR);
			for (int i = 0; i < perTeam; i++) {
				arena->AddCar(Team::BLUE);
				arena->AddCar(Team::ORANGE);
			}
			std::string mode = std::to_string(perTeam) + "v" + std::to_string(perTeam);

			for (int seed = 0; seed < 14; seed++) {
				arena->ResetToRandomKickoff(seed);
				snapshot(arena, mode + " kickoff seed " + std::to_string(seed));
			}

			for (int scenario = 0; scenario < 50; scenario++) {
				arena->ResetToRandomKickoff(100 + scenario);
				bool aerial = scenario % 3 == 0;
				RandomizeBall(arena, scenario % 4);
				for (Car* car : arena->_cars)
					if (RandInt(0, 3) != 0)
						RandomizeCar(car, aerial && RandInt(0, 1));
				if (scenario % 5 == 1) // a few spent pads
					for (auto* pad : arena->_boostPads)
						if (RandInt(0, 2) == 0) {
							BoostPadState ps = pad->GetState();
							ps.isActive = false;
							ps.cooldown = Rand(0.5f, 10.f);
							pad->SetState(ps);
						}
				if (scenario % 7 == 2) { // a demolished car
					Car* car = *arena->_cars.begin();
					CarState cs = car->GetState();
					cs.isDemoed = true;
					cs.demoRespawnTimer = 2.f;
					car->SetState(cs);
				}
				int windows = RandInt(0, 6);
				for (int w = 0; w < windows; w++) {
					ScrambleControls(arena);
					arena->Step(8);
				}
				snapshot(arena, mode + " drive " + std::to_string(scenario));
			}
			delete arena;
		}
	}
}
