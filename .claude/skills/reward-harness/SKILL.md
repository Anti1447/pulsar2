---
name: reward-harness
description: How to write and run a compiled scenario test for a Pulsar reward or reward helper (tests/ + RewardTestUtil.h), without touching the trainer, the GPU or any checkpoint. Use after a `reward-scenario-test` trace, when porting or changing a reward's gating logic, when using BallPredCache / goal geometry, or when hard-coding a physics/timing constant. Ground-truth execution of the real header on fabricated GameStates.
---

# Reward harness (Pulsar)

Pulsar's reward tests are offline C++ unit tests in `tests/` (`TestFramework.h`: `TEST(name)`, `CHECK`,
`CHECK_EQ`, `CHECK_NEAR`; `TestMain.cpp` runs everything). They are OFF in the trainer build
(`-DGGL_BUILD_TESTS=ON` adds the `GigaLearnTests` target). Existing examples to copy:
`tests/test_pbr_rewards.cpp` (PBRS properties: unfarmable cycle nets zero, zero-sum sums to zero, demo guard),
`tests/test_team_rewards.cpp` (1v1 identical to the old form, 2v2 second man free, wrapper share math),
`tests/test_ballpred_cache.cpp` and `tests/test_passing_setup.cpp` (ported v3 rewards on engine-style ball paths).

**Never run `GigaLearnBot` to "verify" a reward** - the trainer usually owns the only GPU and
`build/checkpoints_*`. This harness needs no GPU, no Arena, no `RocketSim::Init`.

## Write the test

1. New `tests/test_<thing>.cpp`; `#include "TestFramework.h"` and `"RewardTestUtil.h"`, then the reward header.
2. Fabricate states. `RewardTest::MakePlayer(carId, index, team, pos)` gives a grounded upright car; set only the
   fields the reward reads (`pos`, `vel`, `isOnGround`, `ballTouchedStep`, `isDemoed`, `boost`,
   `prevAction`, `rotMat`...). `gs.deltaTime = RewardTest::STEP_DT` (8/120).
3. Drive it the way `EnvSet` does: `r.Reset(gs)`, then per step `r.PreStep(gs)` and then
   `r.GetReward(player, gs, isFinal)` for each player. Chain states with `gs.prev` / `player.prev` for rewards that
   read the previous step; build the frame list up front and reserve the vector so the pointers stay valid
   (`test_pbr_rewards.cpp`'s `RunSingle`). Branch frame copies off a **playerless** base state, or every frame
   inherits stale players.
4. Ball prediction: `gs.ballPred = &path` where `path = RewardTest::BallPath(pos, vel)` (one BallState per tick,
   241 ticks = the 2.0 s obs-builder horizon). It is a drag-free restitution-0.6 stand-in: use it for logic, never
   to calibrate a constant. A reward on `BallPredCache` must also be tested with `gs.ballPred = nullptr`.

## Structure the scenarios (rules learned the hard way in v3)

- **Golden path** (pays) and **one failure per gate** (does not pay). Include doomed and near-miss variants
  (lands short, wide, over the bar), not only the happy path.
- **Counterfactual pairs:** change exactly one factor. The reward should differ only when it matters.
- **State transitions** for stateful rewards: re-touch, opponent touch, demo, cooldown expiry, `Reset()`.
- **Properties, not just points** (the Pulsar style): PBRS returns to the start state => sums to 0 at gamma 1;
  `ZeroSum` => per-step sum 0 in 1v1; a gate the reward claims => assert the exact boundary on both sides.
- **Compute expected values by hand before trusting a failure** (or a pasted "expected" example). A scripted
  actor must behave the way the model assumes; report results as "under this fixture".
- **Never treat a cache as knowing about goals.** `BallPredCache` flags `hitBackboard` when the ball centre reaches
  the back-wall plane (that includes a shot into the goal); judge x/z at the first slice where it sets.
- Check an edit actually landed (`grep` the changed line) after edits that matter.

## Build and run

Direct compile against the already-built libs (does not touch `build/` outputs and is safe while the trainer runs):

```bash
cd ~/pulsar2 && P=$PWD/GigaLearnCPP
g++ -std=gnu++20 -O1 -DRG_ROCKETSIM_V3 -Itests -I$P/src/public -I$P/libsrc/json -I$P/RLGymCPP/src -I$P/RLGymCPP/thread_pool \
  tests/TestMain.cpp tests/test_<thing>.cpp -o /tmp/reward_tests \
  build/libRLGymCPP.a build/libRocketSimV3.a -lpthread -ldl && /tmp/reward_tests
```

Header-only syntax check of everything ported, no link: add `-fsyntax-only` and include all the headers in one TU.
Full target (separate build dir so `build/` is untouched): `cmake -B build-tests -DGGL_BUILD_TESTS=ON && cmake --build build-tests --target GigaLearnTests`.
If `libRLGymCPP.a` is missing, the trainer has not been built on this box yet.

Gotchas: compile `GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/ZeroSumReward.cpp` into the test binary when a test uses `ZeroSumReward` (the prebuilt `libRLGymCPP.a` segfaulted in `ZeroSumReward::GetAllRewards` against current headers - a stale-lib layout mismatch, so don't trust the .a for classes whose headers changed); `ZeroSumReward`'s `Reset`/`GetAllRewards` are protected - call them through a `Reward&`; `Vec` operators live in the engine lib (link `libRocketSimV3.a`); gcc rejects `const Config& = Config()` as a
default argument for a nested config struct (MSVC allows it) - use two constructors; a bare `Car` identifier is
ambiguous with the engine's `Car`.

## Validate fixture flags against the real engine

A scripted state can be impossible. The first RolvWavedash audit claimed "a plain jump-flip hop is paid" because the fixture set
`isFlipping` true on the landing step; a real car never does that (the dodge lifts it and the flip ends before touchdown). When a reward
keys on engine-produced flags (`isFlipping`, `hasDoubleJumped`, `isOnGround`, contact normals, event flags), drive a real car with
`RocketSim::Init("build/collision_meshes")` + `Arena::Create(GameMode::SOCCAR)` + `AddCar`, step it tick by tick and replay the reward's
rule on an 8-tick sampler at all 8 phases - see `tests/test_wavedash_engine.cpp`. Link `GigaLearnCPP/RLGymCPP/RocketSimV3/target/release/librocketsim_ffi.a`
and run from the repo root (meshes).

## Reward Lab: scenario files on the real engine

For anything that needs real physics (a touch, a landing, a boost pad), write a scenario instead of fabricating states. A scenario
(`tools/reward_lab/packs/core/*.scenario.json`, schema in `RLGymCPP/Lab/Scenario.h`) is a situation + a per-car action script + the
rewards to run + expectations, each with a human-written `intent` line. It runs on the same `EnvSet` that trains and prints a report
(rewards, events, gates, expectations with the measured value).

```bash
# 1. fresh libRLGymCPP.a from the CURRENT headers (the prebuilt one has a stale Reward vtable; EnvSet crashes against it)
# 2. build and run the CLI (from the repo root, which has build/collision_meshes)
g++ -std=gnu++20 -O2 -DRG_ROCKETSIM_V3 -IGigaLearnCPP/src/public -IGigaLearnCPP/libsrc/json -IGigaLearnCPP/RLGymCPP/src -IGigaLearnCPP/RLGymCPP/thread_pool \
  tools/reward_lab/reward_lab_run.cpp -o /tmp/rlr <fresh>/libRLGymCPP.a build/libRocketSimV3.a GigaLearnCPP/RLGymCPP/RocketSimV3/target/release/librocketsim_ffi.a -lpthread -ldl
/tmp/rlr tools/reward_lab/packs/core --out /tmp/lab_out    # exit 0 all pass, 1 an expectation failed, 2 unreadable
```

Rules learned building it:
- **Derive the expected number by hand BEFORE running**, and write the derivation in the scenario's `description`. Two of ten scenarios were
  wrong the first time: an idle car placed on the big boost pad at (3072, 4096) picked up boost (the trace showed the boost term rising), and a
  speed assumed for a sprint was 250 uu/s low. Keep scripted cars off the boost-pad grid (small pads on x = 0, +-940, +-1792...; big at (+-3072, +-4096), (+-3584, 0)).
- **A reused arena is not a fresh arena.** Resetting a used arena to the same state changes the car's height by 0.004 uu from frame 1 (engine suspension state the setter cannot reach). `LabSession::Reset/ReplayTo` build a new arena (about 2 ms). Never compare a run in a reused arena to a first run.
- **A mutation that hangs is a kill.** A run with the action log disabled loops forever; the sweep script must treat a timeout as killed, not "survived".
- **A stale-state leak needs the cooldown still running.** To prove a reset clears reward state, stop the first run right after the payment and replay: by the end of a long run a stale cooldown has expired and the leak is invisible.
- `RS_CLAMP`/`RS_MAX`/`RS_MIN` are macros: never put an RNG call or other side effect in an argument (it is evaluated twice).
- A bare `Car` is ambiguous with the engine's class; `GetName()` is the mangled type name under gcc (the W&B key); use `ReadableRewardName()` for display.

## Limits - say so when reporting

A harness proves the logic on fabricated states with scripted actors. It does NOT show the bot learns the mechanic,
that thresholds match real self-play, or that engine ball paths classify the same as the stand-in (flag
classification is tested only on synthetic paths). Those are W&B reward-health panels plus the viewer
(`GGL_RENDER=1`), per the measurement program in `CLAUDE.md` / `docs/REWARD_SHAPING.md` section 9: pre-registered
criteria that can fail on a null, one lever at a time, and a revert path.
