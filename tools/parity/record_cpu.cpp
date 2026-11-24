// CPU recorder: runs the shared scenarios in whichever RocketSim the build selects (v2: -DGGL_ROCKETSIM_V3=OFF, v3: ON)
// and writes one CSV trace per scenario (see tools/parity/README.md). usage: parity_record <outdir> [scenario-filter]
#include <RLGymCPP/Framework.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include "scenarios.h"

using namespace parity;

static Vec V(Vec3f v) { return Vec(v.x, v.y, v.z); }

int main(int argc, char** argv) {
	if (argc < 2) { std::cerr << "usage: parity_record <outdir> [filter]\n"; return 1; }
	std::filesystem::path out = argv[1]; std::filesystem::create_directories(out);
	std::string filter = argc > 2 ? argv[2] : "";
	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes", "../build/collision_meshes", "/home/nhafe/pulsar2/build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path, true); break; }

	for (const Scenario& sc : BuildScenarios()) {
		if (!filter.empty() && sc.name.find(filter) == std::string::npos) continue;
		Arena* arena = Arena::Create(GameMode::SOCCAR);
		std::vector<Car*> cars;
		for (const CarInit& ci : sc.cars) cars.push_back(arena->AddCar(ci.team == 0 ? Team::BLUE : Team::ORANGE));
		for (size_t i = 0; i < cars.size(); i++) {
			const CarInit& ci = sc.cars[i];
			CarState cs = {};
			cs.pos = V(ci.pos); cs.vel = V(ci.vel); cs.angVel = V(ci.angVel);
			cs.rotMat = RotMat::LookAt(V(ci.forward), V(ci.up));
			cs.boost = ci.boost; cs.isOnGround = ci.onGround;
			for (int w = 0; w < 4; w++) cs.wheelsWithContact[w] = ci.onGround;
			cs.hasJumped = ci.hasJumped; cs.hasDoubleJumped = ci.hasDoubleJumped; cs.hasFlipped = ci.hasFlipped;
			cars[i]->SetState(cs);
		}
		BallState bs = {}; bs.pos = V(sc.ball.pos); bs.vel = V(sc.ball.vel); bs.angVel = V(sc.ball.angVel); arena->ball->SetState(bs);

		std::ofstream f(out / (sc.name + ".csv"));
		f << std::setprecision(9);
		f << "tick,bpx,bpy,bpz,bvx,bvy,bvz,bwx,bwy,bwz";
		for (size_t i = 0; i < cars.size(); i++)
			for (const char* k : { "px", "py", "pz", "vx", "vy", "vz", "wx", "wy", "wz", "fx", "fy", "fz", "ux", "uy", "uz", "boost", "ground", "jumped", "dj", "flipped", "demoed", "hit" })
				f << ",c" << i << "_" << k;
		f << "\n";
		auto dump = [&](int tick) {
			BallState b = arena->ball->GetState();
			f << tick << "," << b.pos.x << "," << b.pos.y << "," << b.pos.z << "," << b.vel.x << "," << b.vel.y << "," << b.vel.z << "," << b.angVel.x << "," << b.angVel.y << "," << b.angVel.z;
			for (Car* c : cars) {
				CarState s = c->GetState();
				f << "," << s.pos.x << "," << s.pos.y << "," << s.pos.z << "," << s.vel.x << "," << s.vel.y << "," << s.vel.z << "," << s.angVel.x << "," << s.angVel.y << "," << s.angVel.z
				  << "," << s.rotMat.forward.x << "," << s.rotMat.forward.y << "," << s.rotMat.forward.z << "," << s.rotMat.up.x << "," << s.rotMat.up.y << "," << s.rotMat.up.z
				  << "," << s.boost << "," << (int)s.isOnGround << "," << (int)s.hasJumped << "," << (int)s.hasDoubleJumped << "," << (int)s.hasFlipped << "," << (int)s.isDemoed
				  << "," << (s.ballHitInfo.isValid ? (long long)s.ballHitInfo.tickCountWhenHit : -1LL);
			}
			f << "\n";
		};
		dump(0);
		for (int t = 0; t < sc.ticks; t++) {
			for (size_t i = 0; i < cars.size(); i++) {
				Ctl k = sc.ctl(t, (int)i); CarControls cc;
				cc.throttle = k.throttle; cc.steer = k.steer; cc.pitch = k.pitch; cc.yaw = k.yaw; cc.roll = k.roll; cc.jump = k.jump; cc.boost = k.boost; cc.handbrake = k.handbrake;
				cars[i]->controls = cc;
			}
			arena->Step(1);
			dump(t + 1);
		}
		delete arena;
		std::cout << "recorded " << sc.name << " (" << sc.ticks << " ticks)\n";
	}
	return 0;
}
