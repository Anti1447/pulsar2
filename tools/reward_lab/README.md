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

## The viewer (P2)

```bash
build-lab/reward_lab_run tools/reward_lab/packs/core --out /tmp/lab_out --html     # writes <name>.lab.html next to the report and trace
cd /tmp/lab_out && python3 -m http.server 8765 --bind 127.0.0.1                     # or just open the .html file
```

Each `.lab.html` is one file with the run embedded (no network, no server needed). It shows a top-down and a side view with the reward's own
geometry (offside planes, landing targets, potentials) drawn by role, a HUD with every reward's raw / wrapped / weighted value for the selected car,
each gate with pass or fail and its reason, the named components and events, cumulative sums, a timeline with event and gate-flip markers, and the
scenario's expectations (click one to jump to the frames it checks). Keys: Space play, arrows step, Home/End, `[` `]` previous / next event.
Everything on the page is the trace the reward reported: nothing is recomputed from the state, and the page re-checks its own arithmetic
(weighted = wrapped x weight, total = sum of weighted). Claude reads the same page through the browser's text/DOM (`data-raw`, `data-gate`,
`data-passed`, ... hold the exact JSON values) or `window.LAB.snapshot()`.

Page logic tests (node; pass exported pages to also check real runs): `node tools/reward_lab/viewer/test_core.js /tmp/lab_out/*.lab.html`.
A browser caches pages by URL: add `?v=2` after re-exporting.

Adding a reward: one line in `Lab/RewardRegistry.h`. Adding a scenario: copy one from `packs/core/`, derive the expected values by hand first and
write the derivation in `description`, keep cars off the boost-pad grid.

Limits: one arena; actions are per decision step (tick_skip engine ticks), not per tick; a run shows a reward's logic on scripted situations,
not that the policy learns the mechanic (that is W&B health plus watching the policy).
