// Physics throughput with cars in motion: the six game modes (fixed mix), random starts over the whole arena (ground, air,
// near walls), random actions that change every 4 ticks. Reports arena-ticks/s per batch size, best and median of the repeats.
// usage: gpu_bench [total arenas ...]   Not a ctest. Compare builds by running them alternately on the same machine.
#include <rocketsim_cuda/sim_context.cuh>
#include <cuda_runtime.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>
using namespace rocketsim_cuda;

struct Mode { uint32_t cars, blue; double frac; };
static const Mode MODES[] = { { 2, 1, 0.3333 }, { 4, 2, 0.2833 }, { 3, 2, 0.0500 }, { 6, 3, 0.2667 }, { 5, 3, 0.0333 }, { 4, 3, 0.0333 } };
constexpr int ACTION_SETS = 64, REPEATS = 5, TICKS = 480;

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	std::vector<uint32_t> totals; for (int i = 1; i < argc; i++) totals.push_back((uint32_t)atoi(argv[i]));
	if (totals.empty()) totals = { 4096, 16384, 49152 };
	printf("   arenas | arena-ticks/s best | median\n");
	for (uint32_t N : totals) {
		std::mt19937 rng(1234);
		auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
		struct Ctx { std::unique_ptr<SimContext> sim; uint32_t arenas; float* actions; uint32_t cars; std::vector<CarStatePOD> cp0; std::vector<BallStatePOD> bp0; };
		std::vector<Ctx> ctxs; double arenas = 0;
		for (const Mode& m : MODES) {
			Ctx c; c.arenas = std::max(1u, (uint32_t)(N * m.frac + 0.5)); c.cars = c.arenas * m.cars;
			c.sim.reset(new SimContext(c.arenas, m.cars)); c.sim->ResetToDefault();
			std::vector<CarStatePOD> cp(c.cars); c.sim->CopyCarStateToHost(cp.data(), 0, c.cars); cudaDeviceSynchronize();
			for (CarStatePOD& p : cp) {
				const bool air = uni(0, 1) < 0.3f; const float yaw = uni(-3.14159f, 3.14159f), sp = uni(0, 1800);
				p.pos = Vec3(uni(-3900, 3900), uni(-4900, 4900), air ? uni(100, 1800) : 17.f); p.vel = Vec3(sp * std::cos(yaw), sp * std::sin(yaw), air ? uni(-500, 700) : 0.f);
				p.quat = Quat(std::cos(yaw / 2), 0, 0, std::sin(yaw / 2)); p.boost = uni(0, 100); p.is_on_ground = !air; for (int w = 0; w < 4; w++) p.wheels_with_contact[w] = !air;
			}
			std::vector<BallStatePOD> bp(c.arenas); for (BallStatePOD& b : bp) { b.pos = Vec3(uni(-3500, 3500), uni(-4500, 4500), uni(100, 1500)); b.vel = Vec3(uni(-2000, 2000), uni(-2000, 2000), uni(-500, 1000)); }
			c.sim->CopyCarStateToDevice(cp.data(), 0, c.cars); c.sim->CopyBallStateToDevice(bp.data(), 0, c.arenas); cudaDeviceSynchronize();
			std::vector<float> a((size_t)ACTION_SETS * c.cars * 8, 0.f);
			for (size_t i = 0; i < a.size(); i += 8) {
				a[i] = uni(0, 1) < 0.8f ? 1.f : uni(-1, 1); a[i + 1] = uni(0, 1) < 0.5f ? 0.f : uni(-1, 1); a[i + 2] = uni(-1, 1); a[i + 3] = a[i + 1]; a[i + 4] = uni(0, 1) < 0.7f ? 0.f : uni(-1, 1);
				a[i + 5] = uni(0, 1) < 0.15f; a[i + 6] = uni(0, 1) < 0.4f; a[i + 7] = uni(0, 1) < 0.1f;
			}
			if (cudaMalloc(&c.actions, a.size() * sizeof(float)) != cudaSuccess) { printf("%9u | out of GPU memory\n", N); return 0; }
			cudaMemcpy(c.actions, a.data(), a.size() * sizeof(float), cudaMemcpyHostToDevice);
			c.cp0 = cp; c.bp0 = bp; arenas += c.arenas; ctxs.push_back(std::move(c));
		}
		int tick = 0;
		const int resetEvery = getenv("RESET_TICKS") ? atoi(getenv("RESET_TICKS")) : 0;
		auto reset = [&] { if (resetEvery && tick && tick % resetEvery == 0) { for (Ctx& c : ctxs) { c.sim->CopyCarStateToDevice(c.cp0.data(), 0, c.cars); c.sim->CopyBallStateToDevice(c.bp0.data(), 0, c.arenas); } cudaDeviceSynchronize(); } };
		auto run = [&](int ticks) { for (int t = 0; t < ticks; t++, reset(), tick++) for (Ctx& c : ctxs) c.sim->Step(c.arenas, c.actions + (size_t)((tick / 4) % ACTION_SETS) * c.cars * 8); cudaDeviceSynchronize(); };
		run(600); // settle into physical states
		std::vector<double> rate;
		for (int r = 0; r < REPEATS; r++) {
			auto t0 = std::chrono::steady_clock::now(); run(TICKS);
			rate.push_back(arenas * TICKS / std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
		std::sort(rate.begin(), rate.end());
		printf("%9u | %17.1fM | %.1fM\n", N, rate.back() / 1e6, rate[REPEATS / 2] / 1e6);
		for (Ctx& c : ctxs) cudaFree(c.actions);
	}
	return 0;
}
