// Mixed game-mode throughput: one SimContext per mode (cars_per_env fixed per context), all stepped each tick.
#include <rocketsim_cuda/sim_context.cuh>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
using namespace rocketsim_cuda;
struct Mode { const char* name; uint32_t cars; double frac; };
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string which = argc > 1 ? argv[1] : "three";
    std::vector<Mode> modes;
    if (which == "three") modes = {{"1v1", 2, 1.0/3}, {"2v2", 4, 1.0/3}, {"3v3", 6, 1.0/3}};
    else modes = {{"1v1", 2, 0.3333}, {"2v2", 4, 0.2833}, {"2v1", 3, 0.0500}, {"3v3", 6, 0.2667}, {"3v2", 5, 0.0333}, {"3v1", 4, 0.0333}};
    std::vector<uint32_t> totals = {1024, 2048, 4096, 6144, 8192, 12288, 16384, 24576, 32768, 49152, 65536, 98304, 131072, 196608};
    printf("mix=%s\n   N_total | arena-ticks/s | car-ticks/s | ms per tick (all modes) | VRAM MB\n", which.c_str());
    for (uint32_t N : totals) {
        std::vector<std::unique_ptr<SimContext>> ctx; std::vector<uint32_t> n; double mb = 0;
        for (auto& m : modes) {
            uint32_t k = (uint32_t)(N * m.frac + 0.5); if (k < 1) k = 1;
            n.push_back(k); ctx.emplace_back(new SimContext(k, m.cars)); ctx.back()->ResetToDefault();
            mb += ctx.back()->GetAllocatedBytes() / 1048576.0;
        }
        uint32_t steps = (uint32_t)std::max(200.0, std::min(20000.0, 3e8 / N));
        for (int i = 0; i < 20; ++i) for (size_t j = 0; j < ctx.size(); ++j) ctx[j]->Step(n[j]);
        cudaDeviceSynchronize();
        auto t0 = std::chrono::steady_clock::now();
        for (uint32_t s = 0; s < steps; ++s) for (size_t j = 0; j < ctx.size(); ++j) ctx[j]->Step(n[j]);
        cudaDeviceSynchronize();
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        double arenas = 0, cars = 0; for (size_t j = 0; j < n.size(); ++j) { arenas += n[j]; cars += (double)n[j] * modes[j].cars; }
        printf("%10u | %13.1fM | %11.1fM | %8.3f | %7.1f\n", N, arenas * steps / dt / 1e6, cars * steps / dt / 1e6, dt / steps * 1e3, mb);
    }
    return 0;
}
