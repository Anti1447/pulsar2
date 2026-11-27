// CPU twin of tests/gpu/gpu_bench.cu (same six-mode mix, random starts and actions, actions change every 4 ticks), on
// RocketSim through the compat facade (v3 or v2, by build), all cores. usage: parity_bench [threads] arenas...
#include <RLGymCPP/Framework.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <thread>
#include <vector>
using namespace RocketSim;

struct BMode { uint32_t cars, blue; double frac; };
static const BMode MODES[] = { { 2, 1, 0.3333 }, { 4, 2, 0.2833 }, { 3, 2, 0.0500 }, { 6, 3, 0.2667 }, { 5, 3, 0.0333 }, { 4, 3, 0.0333 } };
constexpr int ACTION_SETS = 64, REPEATS = 3, TICKS = 480;

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	const int threads = argc > 1 ? atoi(argv[1]) : (int)std::thread::hardware_concurrency();
	for (const char* path : { "collision_meshes", "../build/collision_meshes", "build/collision_meshes", "/home/nhafe/pulsar2/build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path, true); break; }
	printf("threads=%d\n   arenas | arena-ticks/s best | median\n", threads);
	for (int ai = 2; ai < argc; ai++) {
		const uint32_t N = (uint32_t)atoi(argv[ai]);
		std::mt19937 rng(1234);
		auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
		struct A { Arena* arena; std::vector<Car*> cars; std::vector<CarControls> act; std::vector<CarState> cs0; BallState b0; };
		std::vector<A> arenas;
		for (const BMode& m : MODES) {
			const uint32_t k = std::max<uint32_t>(1, (uint32_t)(N * m.frac + 0.5));
			for (uint32_t i = 0; i < k; i++) {
				A a; a.arena = Arena::Create(GameMode::SOCCAR);
				for (uint32_t c = 0; c < m.cars; c++) {
					Car* car = a.arena->AddCar(c < m.blue ? Team::BLUE : Team::ORANGE);
					const bool air = uni(0, 1) < 0.3f; const float yaw = uni(-3.14159f, 3.14159f), sp = uni(0, 1800);
					CarState s = {};
					s.pos = Vec(uni(-3900, 3900), uni(-4900, 4900), air ? uni(100, 1800) : 17.f); s.vel = Vec(sp * std::cos(yaw), sp * std::sin(yaw), air ? uni(-500, 700) : 0.f);
					s.rotMat = RotMat::LookAt(Vec(std::cos(yaw), std::sin(yaw), 0), Vec(0, 0, 1)); s.boost = uni(0, 100); s.isOnGround = !air; for (int w = 0; w < 4; w++) s.wheelsWithContact[w] = !air;
					car->SetState(s); a.cars.push_back(car); a.cs0.push_back(s);
				}
				BallState b = {}; b.pos = Vec(uni(-3500, 3500), uni(-4500, 4500), uni(100, 1500)); b.vel = Vec(uni(-2000, 2000), uni(-2000, 2000), uni(-500, 1000)); a.arena->ball->SetState(b); a.b0 = b;
				a.act.resize((size_t)ACTION_SETS * m.cars);
				for (CarControls& c : a.act) {
					c.throttle = uni(0, 1) < 0.8f ? 1.f : uni(-1, 1); c.steer = uni(0, 1) < 0.5f ? 0.f : uni(-1, 1); c.pitch = uni(-1, 1); c.yaw = c.steer; c.roll = uni(0, 1) < 0.7f ? 0.f : uni(-1, 1);
					c.jump = uni(0, 1) < 0.15f; c.boost = uni(0, 1) < 0.4f; c.handbrake = uni(0, 1) < 0.1f;
				}
				arenas.push_back(std::move(a));
			}
		}
		int step0 = 0;
		const int resetEvery = getenv("RESET_TICKS") ? atoi(getenv("RESET_TICKS")) : 0;
		auto run = [&](int ticks) {
			const int steps = ticks / 4;
			std::vector<std::thread> pool;
			for (int t = 0; t < threads; t++) pool.emplace_back([&, t] {
				for (size_t i = t; i < arenas.size(); i += threads) {
					A& a = arenas[i];
					for (int s = 0; s < steps; s++) {
						for (size_t c = 0; c < a.cars.size(); c++) a.cars[c]->controls = a.act[(size_t)((step0 + s) % ACTION_SETS) * a.cars.size() + c];
						a.arena->Step(4);
						if (resetEvery && ((step0 + s + 1) * 4) % resetEvery == 0) { for (size_t c = 0; c < a.cars.size(); c++) a.cars[c]->SetState(a.cs0[c]); a.arena->ball->SetState(a.b0); }
					}
				}
			});
			for (auto& th : pool) th.join();
			step0 += steps;
		};
		run(600);
		std::vector<double> rate;
		for (int r = 0; r < REPEATS; r++) { auto t0 = std::chrono::steady_clock::now(); run(TICKS); rate.push_back((double)arenas.size() * TICKS / std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()); }
		std::sort(rate.begin(), rate.end());
		printf("%9u | %17.2fM | %.2fM\n", N, rate.back() / 1e6, rate[REPEATS / 2] / 1e6);
		for (A& a : arenas) delete a.arena;
	}
	return 0;
}
