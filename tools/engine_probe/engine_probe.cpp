// Engine probe: the same physics scenarios in whichever RocketSim the build uses (v2: -DGGL_ROCKETSIM_V3=OFF, v3: default).
// Prints one line per scenario so the v2 and v3 outputs can be diffed. Run from the repo root: ./build/EngineProbe
#include <RLGymCPP/EnvSet/EnvSet.h>
#include <filesystem>
#include <iomanip>
#include <iostream>

using namespace RLGC;

static Arena* NewArena(Car*& car) {
	Arena* arena = Arena::Create(GameMode::SOCCAR);
	car = arena->AddCar(Team::BLUE);
	return arena;
}

int main() {
	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
	std::cout << std::fixed << std::setprecision(1);

	// 1. ball dropped from z=400 onto the roof of a stationary car: successive bounce peaks
	{
		Car* car; Arena* a = NewArena(car);
		CarState c; c.pos = Vec(0, 0, 17); c.rotMat = Angle(0, 0, 0).ToRotMat(); a->ResetToRandomKickoff(0);
		CarState cs = {}; cs.pos = Vec(0, 0, 17.f); cs.rotMat = Angle(0, 0, 0).ToRotMat(); car->SetState(cs);
		BallState b = {}; b.pos = Vec(0, 0, 400); a->ball->SetState(b);
		std::vector<float> peaks; float prevZ = 400, prevVz = 0, maxZ = 0;
		for (int i = 0; i < 1200; i++) {
			a->Step(1);
			BallState s = a->ball->GetState();
			if (prevVz > 0 && s.vel.z <= 0) peaks.push_back(s.pos.z);
			prevVz = s.vel.z;
		}
		std::cout << "drop400_on_roof peaks:";
		for (size_t i = 0; i < peaks.size() && i < 6; i++) std::cout << " " << peaks[i];
		std::cout << std::endl;
		delete a;
	}
	// 2. ball dropped on the open ground (no car in the way): bounce peaks (ball restitution)
	{
		Car* car; Arena* a = NewArena(car);
		CarState cs = {}; cs.pos = Vec(3000, 3000, 17); car->SetState(cs);
		BallState b = {}; b.pos = Vec(0, 0, 400); a->ball->SetState(b);
		std::vector<float> peaks; float prevVz = 0;
		for (int i = 0; i < 1200; i++) { a->Step(1); BallState s = a->ball->GetState(); if (prevVz > 0 && s.vel.z <= 0) peaks.push_back(s.pos.z); prevVz = s.vel.z; }
		std::cout << "drop400_ground z@0.25s steps:"; { Car* c3; Arena* a3 = NewArena(c3); CarState q = {}; q.pos = Vec(3000, 3000, 17); c3->SetState(q); BallState bb = {}; bb.pos = Vec(0, 0, 400); a3->ball->SetState(bb); for (int i = 0; i < 12; i++) { a3->Step(30); std::cout << " " << a3->ball->GetState().pos.z; } std::cout << std::endl; delete a3; }
		std::cout << "drop400_ground peaks:";
		for (size_t i = 0; i < peaks.size() && i < 6; i++) std::cout << " " << peaks[i];
		std::cout << std::endl;
		delete a;
	}
	// 3. ball rolling on the ground at 1500 uu/s: speed after 1, 2, 3 s (ground drag / spin friction)
	{
		Car* car; Arena* a = NewArena(car);
		CarState cs = {}; cs.pos = Vec(3000, 3000, 17); car->SetState(cs);
		BallState b = {}; b.pos = Vec(-2000, 0, 93.15f); b.vel = Vec(1500, 0, 0); a->ball->SetState(b);
		std::cout << "roll1500 speed@1s,2s,3s:";
		for (int sec = 1; sec <= 3; sec++) {
			a->Step(120);
			BallState s = a->ball->GetState();
			std::cout << " " << s.vel.Length();
		}
		std::cout << std::endl;
		delete a;
	}
	// 4. car driving at constant speed S (throttle held, no boost) hits a resting ball head-on 0.5 s later: peak ball speed afterwards
	for (float carSpeed : { 800.f, 1200.f, 1400.f }) {
		Car* car; Arena* a = NewArena(car);
		CarState cs = {}; cs.pos = Vec(0, -carSpeed * 0.5f - 160, 17); cs.vel = Vec(0, carSpeed, 0); cs.rotMat = Angle(1.5708f, 0, 0).ToRotMat(); cs.boost = 0; car->SetState(cs);
		BallState b = {}; b.pos = Vec(0, 0, 93.15f); a->ball->SetState(b);
		float maxBall = 0, carAtHit = 0, extraSpeed = 0; int ticksSinceHit = 0; bool hit = false;
		for (int i = 0; i < 240; i++) {
			car->controls = CarControls(); car->controls.throttle = 1;
			a->Step(1);
			BallState s2 = a->ball->GetState();
			if (!hit && s2.vel.Length() > 5) { hit = true; carAtHit = car->GetState().vel.Length(); }
#ifdef RG_ROCKETSIM_V3
			const auto& hi = car->_ballHitInfo;
#else
			const auto& hi = car->_internalState.ballHitInfo;
#endif
			if (hi.isValid) { extraSpeed = std::max(extraSpeed, hi.extraHitVel.Length()); ticksSinceHit = i; }
			maxBall = std::max(maxBall, s2.vel.Length());
		}
		std::cout << "carhit_" << (int)carSpeed << " car_speed_at_contact=" << carAtHit << " peak_ball_speed=" << maxBall << " engine_extraHitVel=" << extraSpeed << std::endl;
		delete a;
	}
	// 5. carry: ball placed on the roof of a car driving at 800 uu/s with throttle only: ticks the ball stays within 200uu of the car in 8 s
	{
		Car* car; Arena* a = NewArena(car);
		CarState cs = {}; cs.pos = Vec(0, -3000, 17); cs.vel = Vec(0, 800, 0); cs.rotMat = Angle(1.5708f, 0, 0).ToRotMat(); cs.boost = 0; car->SetState(cs);
		BallState b = {}; b.pos = Vec(0, -3000, 17 + 70 + 93.15f); b.vel = Vec(0, 800, 0); a->ball->SetState(b);
		int near = 0;
		for (int i = 0; i < 960; i++) {
			car->controls = CarControls(); car->controls.throttle = 0.4f;
			a->Step(1);
			if ((a->ball->GetState().pos - car->GetState().pos).Length() < 200) near++;
		}
		std::cout << "carry_800 ticks_near_car_of_960=" << near << std::endl;
		delete a;
	}
	// 6. ball rolling at a stationary car (nose-on) at speed S: rebound speed and how long the ball stays within 140uu of the car
	for (float ballSpeed : { 500.f, 1000.f, 2000.f }) {
		Car* car; Arena* a = NewArena(car);
		CarState cs = {}; cs.pos = Vec(0, 0, 17); cs.rotMat = Angle(1.5708f, 0, 0).ToRotMat(); car->SetState(cs);
		BallState b = {}; b.pos = Vec(0, 1500, 93.15f); b.vel = Vec(0, -ballSpeed, 0); a->ball->SetState(b);
		float maxRebound = 0; int near = 0;
		for (int i = 0; i < 480; i++) {
			car->controls = CarControls();
			a->Step(1);
			BallState s2 = a->ball->GetState();
			if (s2.vel.y > 0) maxRebound = std::max(maxRebound, s2.vel.y);
			if ((s2.pos - car->GetState().pos).Length() < 160) near++;
		}
		std::cout << "ball_into_parked_car_" << (int)ballSpeed << " max_rebound_vy=" << maxRebound << " ticks_within_160uu=" << near << std::endl;
		delete a;
	}
	return 0;
}
