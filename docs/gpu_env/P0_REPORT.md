# P0 report - setup (2026-11-24)
Plan: docs/GPU_ENV_PLAN.md. Branch: gpu-env.

Done
- Vendored RocketSim-CUDA (upstream 4ae73b3, MIT) into GigaLearnCPP/libsrc/RocketSimCUDA with UPSTREAM.txt (provenance) and PATCHES.md (every local change).
- Standalone GPU build: `gpu/CMakeLists.txt` + `tools/build_gpu.sh` -> `build-gpu/` (niced; the trainer's build/ is untouched; root option GGL_GPU_ENV exists but is inert until P5).
- Linux build fixes (P0-1..P0-5), team layout for uneven modes (P0-6), bump/demo step event flags (P0-7).
- Test suite scaffolding: tests/gpu/ on the existing TestFramework.h, shared main that SKIPs without a CUDA device, GpuTestUtil.h, ctest labels unit/parity/gpu.

Tests (ctest, `tools/build_gpu.sh test`): 9/9 pass in ~30 s
- L0 upstream: test_math, test_soa, test_sdf, test_integrator; RSC_DifferentialReport (parity table, data); RSC_DifferentialIdleKnownFail (expected failure recorded: upstream's idle gate breaks at tick 22).
- L6: team layout 4/4, reset isolation + determinism 3/3.   L1: bump/demo event flags 4/4.
- Mutation check on the new patches: 4/4 injected bugs caught.

Not verified / known
- Upstream's own fail-fast parity gate does not pass on sm_86 (idle, tick 22); parity vs RocketSim v3 is NOT yet measured (that is P1 / gate G1).
- No sanitizer run yet (planned in L6, label slow).
- Trainer ran alongside the builds and tests; one learn pass read 7.6 s during the ctest run (normal ~6.4 s). No stop needed.
