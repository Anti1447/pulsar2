# Reward Lab (headless runner, P1)

Design: `docs/REWARD_VIEWER_SPEC.md`. Library: `GigaLearnCPP/RLGymCPP/src/RLGymCPP/Lab/` (header-only). Tests: `tests/test_reward_lab.cpp`.

A **scenario** is a JSON file: a situation (cars, ball), a per-car action script, the rewards to run, and expectations. Every expectation
carries an `intent` line written by a person ("why this should hold"); the report prints it next to the measured value.
The scenario runs on the real `EnvSet` (physics, action application, state building, ball prediction, terminals) with the reward block
mirrored by `TracedRewardStack`, so what the report shows is what the reward computed on the training call path.

```
reward_lab_run <scenario.json | directory>... [--out DIR] [--quiet]
```

Exit code 0: every expectation passed. 1: an expectation failed (or a scenario has none). 2: a scenario could not be read or run
(unknown key, unknown reward, missing collision meshes). With `--out`, writes `<name>.report.txt` and `<name>.trace.jsonl`
(RewardTraceJson schema 1, one object per step).

Build and run (from the repo root; needs `build/collision_meshes` and a `libRLGymCPP.a` built from the current headers, because the prebuilt
`build/libRLGymCPP.a` predates `Reward::SetTrace` and is stale for `EnvSet`):

```bash
tools/reward_lab/build.sh --tests          # -> build-lab/libRLGymCPP.a, build-lab/reward_lab_run, build-lab/lab_tests (gitignored; build/ untouched)
build-lab/reward_lab_run tools/reward_lab/packs/core
build-lab/lab_tests
```

`build.sh` caches objects per `.cpp`; after editing a header the library includes (`Reward.h`, `GameState.h`, ...) run it with `FORCE=1`.

Adding a reward: one line in `Lab/RewardRegistry.h`. Adding a scenario: copy one from `packs/core/`, derive the expected values by hand first and
write the derivation in `description`, keep cars off the boost-pad grid.

Limits: one arena; actions are per decision step (tick_skip engine ticks), not per tick; a run shows a reward's logic on scripted situations,
not that the policy learns the mechanic (that is W&B health plus watching the policy).
