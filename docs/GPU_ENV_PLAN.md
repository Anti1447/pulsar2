# GPU environment port (RocketSim-CUDA) - PLAN, awaiting approval (2026-11-24)

Branch `gpu-env` (from `gco-goalcritic` 28c2854). Default build unchanged: everything below sits behind CMake option `GGL_GPU_ENV` (OFF by default), so the live CPU trainer and Pulsar3 are never affected. Revert = switch the branch / leave the option off.

## What exists already (measured on the 3090, see scratch notes ~/rocketgpu_scratch)
RocketSim-CUDA builds for sm_86 after two Linux CMake fixes. It already has: GPU car/ball/pad/bump/demo physics, up to 8 cars per arena, zero-copy SoA state, state setters from tensors (ball, car, pads), indexed/masked GPU resets, a dedicated ball-trajectory prediction kernel, goal extrapolation, and a GPU GameEventTracker (touches, shots, saves, goals, assists). Per-car state carries ball-hit info (rel pos, extra force, tick), wheel contacts, jump/flip/boost timers, demo state, `team`.
Speed (physics only, 3 modes at 1/3 each): plateau ~33M arena-ticks/s, 90% at ~40k total arenas. Six modes: 90% at ~100-130k.

## What it does not have (we must build)
1. Observations (278-dim AdvancedObsPadded, team-canonical, padded slots, pad timers, 8 prediction slices) and DefaultAction (90 actions + masks).
2. Rewards (24 terms, ~1.8k lines of stateful C++), ZeroSum/TEAM_SPIRIT, PBRS gamma, stepScale, terminals (goal, no-touch 20 s).
3. State setters (the 5-way reset mix) and the per-mode (1v1, 2v2, 2v1, 3v3, 3v2, 3v1) contexts.
4. Bump / bumped / demo / demoed per-step event flags (not exposed by its tracker).
5. Batched opponent inference (old versions) and external bots (Nexto, GalactusV2, ChronosV2, Pulsar2: their own obs builders).
6. Learner hand-off (device rollout buffers -> existing ExperienceBuffer) and metrics.
Known sim differences to measure, not assume: analytic arena (SDF) instead of collision meshes; its own parity gate fails on `idle` at tick 22 (1.4e-4 vs 1e-4) and diverges in contact-heavy play (random-control rollout: median 10 uu, p95 61 uu after 1 s).

## Strategy: the CPU stack is the oracle
Every GPU piece is checked against the CURRENT CPU code on the SAME states. A "hybrid" adapter (GPU physics -> download -> build the existing CPU `GameState`) runs our unmodified obs/rewards/setters on GPU physics first. That separates two questions: (a) is the GPU physics good enough to train on, (b) are the GPU kernels exact ports. Nothing is trained on the new path until gates G1-G3 pass.

## Phases and gates (each ends in a written report; stop at the first failed gate)
P0  Setup. Vendor RocketSim-CUDA into GigaLearnCPP/libsrc/RocketSimCUDA with our patches (Linux CMake, -fPIC, CUDA::cudart, team override / active-car mask, bump+demo event flags). Build in `build-gpu/`. No trainer change.
P1  Hybrid physics adapter (`GpuArenaBatch`): per-mode contexts, our action table -> controls, tickSkip 4 / delay 0, car config (hitbox as in the trainer), CPU state setters uploaded through the tensor setters, device -> `GameState` conversion.
    GATE G1 (policy-level physics parity, pre-registered before running): current Pulsar3 checkpoint (2.09B) played against (a) itself and (b) archived versions, CPU v3 vs GPU sim, >= 20k episodes per cell: goals/1k steps, touch share, kickoff first-touch %, mean episode length, aerial touch ratio within +/-15% relative; plus scripted mechanic checks (wavedash, dodge/flip, wall ride, ceiling, bump, demo, boost pad) with trajectory bands. Fail -> report, no further work.
P2  GPU observation + action-mask kernels. GATE G2: max abs error <= 1e-4 per feature vs the CPU ObsBuilder on >= 1e6 identical states (all six modes, both teams).
P3  GPU reward stack in three waves, each term diffed against the CPU class on recorded GameState sequences: wave 1 stateless/potentials (Goal, TimeCost, TeamPressure, BallToGoal, BallProximity, CarEnergy, AirIntercept, GoalSpeed, GuardedPickup, TouchAccel, OpposedSave, VerifiedShot, Offside, TooFar, TeammateSpacing); wave 2 events (Demo, OffensiveDemo, bump flags); wave 3 stateful latches (AerialTouch, ConsecutiveAirTouch, WallJumpToBall, RolvWavedash, Wavedash, KickoffRace, KickoffOutcome); then ZeroSum + TEAM_SPIRIT. GATE G3: per-term |err| <= 1e-4 abs / 1e-3 rel, stack sum identical per player.
P4  GPU state setters (reset mix 0.20 each), terminals, mode mix (1v1 .333, 2v2 .283, 2v1 .05, 3v3 .267, 3v2 .033, 3v1 .033), PHASE A/B handling. GATE G4: distribution tests (KS) against the CPU setters.
P5  Learner integration: `GpuEnvSet` behind the EnvSet contract; device rollouts, inference without host copies, ONE bulk copy per iteration into the existing ExperienceBuffer (keeps Learner/PPO unchanged). Old-version opponents on GPU; external bots on the hybrid path for the ~20% served iterations; telemetry from a sampled subset of arenas. Gauntlet and skill tracker stay on CPU RocketSim v3 (a built-in sim-to-sim transfer check).
    GATE G5: 500 iterations from a copy of the 2.09B checkpoint: stable (entropy, Vdag, rating on the CPU eval), no NaN, per-iteration time reported.
P6  Throughput tuning and arena-count scan (8k-65k), then a pre-registered A/B training run against Pulsar3's curve at equal samples. Expect the learn pass (~6.4 s for ~450k collected steps, ~60-70k steps/s) to become the ceiling; learner-side changes are a separate decision, not part of this port.

## Unit and parity test suite (test-first: no kernel is merged without its test)
Location `tests/gpu/`, built only with `GGL_GPU_ENV=ON`, on the existing `tests/TestFramework.h` / `RewardTestUtil.h` harness (reward-harness skill). ctest labels: `unit` (seconds, no oracle), `parity` (GPU vs CPU oracle), `gpu`, `slow` (nightly / gate runs). Tests skip cleanly with no CUDA device, keep VRAM under ~1 GB so they can run next to the live trainer, and run niced with thread caps.
L0 Vendored library: upstream `test_math`, `test_soa`, `test_sdf`, `test_integrator` and the differential harness wired into ctest, with our known-failing upstream scenario (`idle`, tick 22) recorded as an expected result, not hidden.
L1 Physics parity: a scenario table run on CPU RocketSim v3 and on the GPU, trajectories compared per scenario with its own tolerance: idle, throttle, boost, steer/handbrake, jump, double jump, 8 dodge directions, flip cancel, wavedash, wall ride, ceiling, floor/wall/ceiling/corner ball bounces, car-ball hit (speed and angle sweep), bump, demo, boost pad pickup and respawn, kickoff, all six modes. Results written to a golden JSON; any drift beyond tolerance fails.
L2 Adapter: device -> `GameState` field-by-field equality (every `Player`/`BallState` field the rewards read), team and slot ordering per mode (including 3v1 with the team override), the 90-action table -> controls equality, and action masks for every ground/air/boost/flip state class.
L3 Observation kernels: per-feature equality vs the CPU `AdvancedObsPadded` over fabricated edge states (ORANGE canonicalisation, zero-padded slots and presence flags for every mode, prevAction, inverted pad timers, the 8 ball-prediction slices vs `BallPredCache`) plus a 1e6-state randomized differential.
L4 Rewards (per term): (a) the existing hand-traced scenario ladder replayed through both the CPU class and the GPU kernel; (b) randomized replay differential on recorded `GameState` sequences; (c) the farming probes carried over; (d) mutation tests for the stateful ports (AerialTouch, ConsecutiveAirTouch, WallJumpToBall, Wavedash, KickoffRace/Outcome): the suite must catch each injected bug; (e) stack invariants: ZeroSum/TEAM_SPIRIT sums, PBRS telescoping with `g_TrainGamma`, `stepScale` applied only to per-step terms, the 24-term sum per player equal to the CPU stack.
L5 Setters and terminals: KS tests of each setter's distribution vs the CPU setter, kickoff positions exact, bounds and no-overlap constraints, mode mix proportions, goal / no-touch (20 s) / truncation flags and their NORMAL vs TRUNCATED semantics.
L6 Lifecycle and safety: indexed and masked resets leave every other arena bit-identical (isolation); same seed -> same state hash across runs and across batch sizes; NaN/inf guards trip; no out-of-range indexing; `compute-sanitizer` memcheck, racecheck and synccheck over the kernel tests (label `slow`).
L7 Integration: rollout layout equals the CPU path (row-contiguous episodes per player, terminal types), GAE and reward normalization identical on the same trajectory, GPU metric counters equal CPU counters on the same state stream, a 50-iteration end-to-end smoke (no NaN, finite losses), and a golden-hash regression of a fixed-seed rollout.
L8 Performance guards (reported, not correctness gates): per-tick latency at 1k / 8k / 32k arenas per mode, and per-iteration collection time, with regression thresholds.
Each phase report states test counts (pass/fail/skipped) and mutation-catch ratios; a gate cannot pass with a skipped parity test.

## Risks
Physics gap from the analytic arena (checked at G1 and again by the CPU gauntlet); single-maintainer upstream; one thread per arena and ~0.18 ms fixed cost per mode context per tick (six contexts ~1.1 ms/tick floor); fixed cars per context and team-by-index default (3v1 needs the team override); ~1.8k lines of reward logic to port exactly; GPU shared with learn pass and inference on one 3090; two code paths to maintain.

## Decisions (2026-11-24)
RocketSim-CUDA is VENDORED into our repo (no GitHub fork). Pulsar3 restarted; I may stop it if it interferes with the port and will say so. Plan approval for execution still pending.
