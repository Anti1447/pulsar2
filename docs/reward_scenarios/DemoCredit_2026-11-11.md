# Are demos credited to the right car and arena? (2026-11-11)

Trigger: a bug in the original GigaLearn `EnvSet` (found in the user's RLGymBot_v3 repo): arenas are built on the thread pool and a per-arena index taken from
the JOB number instead of the arena's slot makes every goal / demo / save / shot / bump flag land in another arena's GameState. Pulsar2's `EnvSet.cpp` already
avoids it (vectors pre-sized, every per-arena object written at `[idx]`, so slot == job index; its comment says so). This adds the measurements and a test so the
claim is checked, not assumed.

Status: implemented and tested; the telemetry is in `ExampleMain.cpp` but **not deployed** (the trainer was not restarted).

## Telemetry (state-only, `Telemetry/DemoTelemetry.h`, wired in `StepCallback`)
| Panel | Meaning | Healthy |
|---|---|---|
| `Demo/Implausible Rate` | per credited demo: bumper not supersonic on the step before or this step, or more than 600 uu from the victim's position before the step | ~0 |
| `Demo/Event-State Mismatch Rate` | per step with any demo activity: the callback flags (`eventState.demo`/`.demoed`) disagree with the cars whose `isDemoed` actually rose, or a flagged bumper has no opposite-team flagged victim | 0 |
| `Demo/Events Checked` | how many such steps this iteration (the denominator) | > 0 |
A misrouted event pairs cars that are nowhere near each other, so it reads implausible, and it also leaves the real victim's `isDemoed` rise with no flag, so it reads as a mismatch.
Caveat: the mismatch panel also fires if the engine ever sets `isDemoed` through a path the event callback ignores (for example a same-team demo, which `_BumpCallback` drops by design). A nonzero rate is therefore a prompt to look, not proof of misrouting.
Constants: 600 uu = contact at ~120 uu plus up to ~310 uu of closing in one 8-tick step with margin.

## Tests
- `tests/test_demo_geometry.cpp` (29 pass, 5 new): both predicates on fabricated states, including 0.9x/1.1x of the distance bound, the supersonic flag flipping inside the step, the engine moving a demoed car, flags in an arena with no demo, a demo with no event, a victim flag on the wrong car, a same-team pair, and the first step of an episode.
- `tests/test_envset_event_routing.cpp` (real `EnvSet`, 48 arenas, creation sleeps in reverse job order): for arenas 0, 1, 17, 30, 47 a supersonic blue car is driven into a stationary orange car; the flags must appear on those two cars in that arena's GameState and in no other arena, `arenas[k]` must be the arena built for job k, and `eventCallbackInfos[k]->arenaIdx` must equal k. The new telemetry predicates must also read the real step as consistent and plausible.
- **Mutation check:** a copy of `EnvSet.cpp` with the slots written in reverse job order while `userInfo->arenaIdx` stays the job index (the original bug's shape) fails the test at the slot check. With the slot checks removed, it still fails on the routing assertion (flags in another arena), so the routing check does not depend on the structural one.

## Limits
Forced demos in idle-kickoff arenas, one demo per step. Not covered: goals, saves, shots (same mechanism and same fix, not exercised), two demos in the same step, or a full training run. The live panels need a trainer restart to appear.
