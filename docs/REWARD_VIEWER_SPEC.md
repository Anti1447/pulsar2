# Reward Lab - spec overview (proposal, nothing implemented)

Date: 2026-11-02, revision 1. Target: pulsar2 on RocketSim v3. Provenance: **[R]** read in the pulsar2 source today, **[A]** assumption / design choice, **[?]** unverified.

## 1. Goal and the one idea that makes it work
Goal: a human and Claude must agree, from evidence, on what a reward actually does. They look at different things: the human watches the sim, Claude reads text. If each sees a
separate derivation of "what the reward did", they can still disagree. So:

> **Both are fed by one artifact: the reward's own trace, produced by the same call that produces the training reward.**

The trace (per player, per step) holds the reward value, its components, each gate's pass/fail with a reason, and the geometry the reward used. The overlay draws it (human). The
headless runner prints it as text/JSONL (Claude). A scenario file plus expectations makes the agreement executable and re-checkable after any code change.

The failure this design must prevent is **the viewer lying**: an overlay computed by a second copy of the logic can look right while the reward is wrong (the exact failure CLAUDE.md 7D
documents for the harness). So the overlay never re-derives anything; it only displays what `GetReward` itself reported.

## 2. What already exists (read today) and what is missing
Exists [R]:
- `VizControl` (`GigaLearnCPP/src/public/GigaLearnCPP/Util/VizControl.{h,cpp}`): commands `pause, play, step, seek, speed, deterministic, agent, set_ball, set_car, reset_record` over UDP 9276; ports 9273 (state in), 9274 (page websocket), 9275 (http).
- `SnapshotRing` of `ArenaSnapshot` (900 frames): ball, rng state, per-car engine state + the opaque `engineExtra` block + boost pads. `test_viz_snapshot.cpp` pins that restore is exact. Branching truncates the discarded future.
- `RenderSender` -> `render_receiver.py` -> web page (`tools/viz/RocketSimVisWeb`, THREE.js). The frame already carries `pulsar` (panel state) and `actions` (controls per car).
- `StateSetter::ResetArena(Arena*)` and several scenario-style setters (`AirDrillState`, `AirPlayState`, `BallNearCarState`, `FuzzedKickoffState`).
- Known limit [R, in a code comment]: `tickCount` cannot be restored by a snapshot (no FFI setter).

Missing:
1. **No reward visibility at all.** Nothing streams per-reward values, gates or geometry, and I found no overlay-primitive support in the page's JS (by grep; confirm).
2. **The viewer is welded to the trainer.** The render loop lives inside `Learner.cpp` and drives cars with the policy, so it needs a checkpoint, libtorch and the learner config. Testing a reward on a scripted situation needs none of that.
3. **No input source other than the policy.** No manual control, scripted control, macro recording or replay.
4. **No scenario / training-pack format.** Cars and ball can be dragged live, but a situation cannot be saved, named, loaded or asserted on.
5. **Rewind restores the arena, not the reward.** Most rewards hold per-player state (cooldowns, caps, windows, `lastEventTime`). Seeking the arena back while the reward keeps its old state would show a wrong trace. This is the main design problem (section 4).

## 3. Components
```
 Scenario pack (json) --+                         +--> Overlay (web page: HUD + 3D primitives + timeline)   <- human
                        v                         |
 Input source ---> RewardLab host ---> Reward stack (the real BuildRewards subset) ---> Trace ---+
 (manual|macro|        (1 arena, no policy,                 ^ same call, sink attached          |
  script|policy)        no GPU)                             |                                    +--> JSONL + text report (headless runner)  <- Claude
                        ^ timeline = start state + action log
```

### 3.1 Reward trace contract (`Rewards/RewardTrace.h`)
- `struct TraceSink` collects, for one (player, step): `Component(name, value)`, `Gate(name, passed, reason)`, `Event(name)`, and world-space primitives
  `Point / Line / Arrow / Ring / Box / Text` with a semantic role (`target`, `corridor`, `threshold`, `ok`, `blocked`, `phi`) rather than a raw colour, so the page owns the styling.
- A reward opts in by splitting its body: `float Evaluate(player, state, TraceSink* sink)`; `GetReward` is `Evaluate(player, state, trace_)`. **One code path.** `sink == nullptr` costs one branch, so training is unaffected [A, measure].
- Rewards that do not opt in still show: name, weight, raw value, weighted value, and whether `ZeroSum`/`gated` wrappers changed it. Instrumentation is incremental, 2-3 rewards first.
- The wrapper (`WeightedReward`, `ZeroSumReward`) attaches the sink around the single existing call. **The reward is never called a second time for display**: a second call would tick its cooldowns twice.
- Streams per step also: PBRS potentials (`Phi`, and the discounted shaping term with `TRAIN_GAMMA`), and the discounted running return per reward, since that is what PPO sees.
- An optional independent **oracle** channel per mechanic (e.g. the tick-level wavedash labeler in `test_wavedash_formal.cpp`): the overlay marks steps where oracle and reward disagree. This is the one place a second derivation is allowed, because its purpose is to disagree.

### 3.2 RewardLab host (new executable, no libtorch policy)
- Builds `EnvSet`-style stepping for **one arena** with the **real** reward stack. Same tickSkip / actionDelay as training (8 / 0, 15 Hz) [R from earlier work]; the step order (`PreStep` -> `GetReward` -> obs) is the one in `EnvSet.cpp`.
- Reuses `VizControl`, `SnapshotRing`, `RenderSender` unchanged where possible. **Decision needed:** link the existing GigaLearnCPP library (already built; it needs the libtorch DLLs at load but does not use the GPU), or lift `ArenaSnapshot`/`SnapshotRing` into RLGymCPP. Recommend the first, to ship quickly; revisit if the dependency hurts.
- Reward set chosen by name and weight from a small config (`lab.toml`), defaulting to the live `BuildRewards()` list so what you see is what trains. A `--only RewardA,RewardB` isolates terms.

### 3.3 Input sources (per car, switchable at runtime)
`idle` | `manual` (keyboard/gamepad from the page, step-locked or slow-mo) | `macro` (recorded track) | `script` (small controller, e.g. "hold boost, jump, press dodge at T-1.5 ticks") | `policy` (a checkpoint, later; this is also how to watch the real bot with the overlay).

### 3.4 Locations the overlay can read from (my reading of "configurable location(s)")
A `TraceSource` abstraction with three implementations, selectable and combinable in the page: **in-process** (the Lab), **stream** (UDP, the trainer's render arena or training arena 0, read-only), **file** (a recorded JSONL trace). Several can be shown at once for comparison (same scenario, before vs after a reward edit). If you meant something else by "locations" (for instance world-space anchors the overlay draws at), say so; the primitives already carry world coordinates.

### 3.5 Overlay (P2 as built: a self-contained static page, not a patch of RocketSimVisWeb)
**Decision (2026-11-02):** `tools/viz/RocketSimVisWeb` is a third-party git submodule whose page has no control panel, no command channel and no overlay layer, so extending it would mean carrying a patched fork inside pulsar2. P2 instead exports each run as ONE html file (`reward_lab_run --html`): the run JSON is embedded, so a person opens it in any browser, Claude reads the same page (`get_page_text` or the `window.LAB` object), and the file can be sent to someone. The 3D page can later consume the same run JSON if wanted. A live host (step, branch, manual input) arrives with P3, when input recording needs it; until then the "agreement" artifact is the exported page plus the JSONL, which are checked equal.

Original description of the overlay, kept for the live version:
- **HUD**: one row per reward, with weight, raw and weighted value, a live bar, gate checklist (green/red + reason), cumulative and discounted return, `Phi`.
- **3D**: the trace primitives, with gates/thresholds drawn where they live (corridors, goal-mouth targets, offside lines), and an expanding marker when a reward pays.
- **Timeline** under the viewport: a per-reward value strip, event markers, the oracle agree/disagree track, scrubbable; the same bar as the existing seek.
- The HUD is plain DOM text, so Claude can read it through the browser tools without screenshots. Screenshots stay under the allowlist rule in CLAUDE.md 7D; a new window title would have to be added by you.

## 4. Time travel: the design that makes rewind trustworthy
**The timeline is `start state + action log`, not a pile of snapshots.** Seeking to frame F = reset the reward stack and the obs builder, restore the nearest keyframe (or the scenario start), and **re-simulate with the recorded actions up to F**. The engine is deterministic [A, relies on the snapshot test and a new replay test], and 120 Hz sim for a few hundred ticks is instant.
Why not just restore snapshots (which the ring already does): the reward objects, the ball predictor inside the obs builder, and `tickCount` would not match the restored arena. Replay gets all three right by construction.
- **Measured in P1: replay builds a FRESH arena.** Writing the initial state into a reused arena does not reproduce a first run: from frame 1 the car's height differs by 0.004 uu, because the engine keeps internal suspension state the state setter cannot reach (the same limit `VizControl`'s `engineExtra` documents). A fresh arena is exact and costs about 1.9 ms (2v2), so `Reset()`/`ReplayTo()` rebuild the environment.
- Scrubbing (viewing only) uses the stored per-frame trace and snapshot: free, lossless.
- **Branching** (press play after a seek, change an input, drag a car) truncates the future, as the ring does today, and the new action log forks from that frame.
- "Replay a series of actions" and "macro playback" are the same thing: a log applied from a start state.

## 5. Scenario files (training-pack support)
`*.scenario.json`, in packs (a folder plus `manifest.json`), versioned with a `schema` field:
- **Authored** (portable, human-written): mode (1v1/2v2/3v3), per-car position / velocity / orientation / boost / flags (`hasJumped`, `airTimeSinceJump`, `hasFlipped`, ...), ball state, optional action track, notes, tags.
- **Captured** (exact, "save this frame"): the full `ArenaSnapshot` including `engineExtra` and rng. Bound to the engine version; refused with a clear message on mismatch.
- **Expectations** (this is what turns a picture into agreement): e.g. `reward X sums to [a,b] over steps m..n`, `gate Y passes at step k`, `event Z fires exactly once`, `total |r| <= cap`. Each expectation also carries one plain-English intent line written by you: "should pay ~3 here: held the flip, cancelled at touchdown".
- **Sign-off**: the manifest records the reviewer, date, and a hash of the reward header; a later edit that changes the hash marks the pack stale until re-run and re-approved. Claude may not call a reward "verified" on a stale pack.
- Import from the game's custom training packs: **[?] not investigated**; feasibility unknown, so it is a later item, not promised.
- The same files drive the **headless runner** (`reward_lab_run pack/ --report out/`), which is the replacement for hand-built fake states in new harness tests: real engine, real stepping, assertions on the trace.

## 6. Recording and the tick-skip trap
Macros are stored **per tick (120 Hz)**. Humans can press at 120 Hz; the bot cannot (15 Hz, held 8 ticks). A human demonstrating a mechanic at tick resolution proves nothing about the bot; this is exactly how the "40-60% captured" claim was wrong. So:
- Every macro can be **projected onto the decision grid** (`hold-last` at skip 8, 4, 1, with a selectable phase offset). The Lab plays either version and shows both traces side by side.
- The scenario runner's default for pass/fail is the **grid version at skip 8**; the 120 Hz version is labelled "ceiling, not achievable".
- A phase sweep (all 8 offsets) reports how often the reward pays under each; this reproduces the 25%-by-chance finding for the wavedash window.

## 7. Build order (each step with its own tests and revert)
| step | deliverable | exit gate (pre-registered) |
|---|---|---|
| P0 **DONE 2026-11-02** | `RewardTrace.h`, sink, JSON schema, wrapper hook; 3 rewards instrumented (`OffsidePenalty` gated geometric, `CarEnergyPotentialReward` PBRS, `PassingSetupReward` stateful cooldown) | trace-on and trace-off return **bit-identical** rewards over a 10k-step random rollout; null sink adds no allocation; the stateful reward is called exactly once per step. **Met:** 12 tests in `tests/test_reward_trace.cpp`, 10,200 steps across 1v1/2v2/3v3, also at the production flags (`-march=native -ffast-math`); 16/16 mutations caught. The weighted *total* is only checked to float noise (FMA contraction differs between translation units); each reward's own output is bit for bit. Not wired into EnvSet or the trainer. |
| P1 **DONE 2026-11-02** | Scenario format + headless runner + report; replay-based timeline | same scenario twice -> identical trace; replay to frame F equals the original run's frame F exactly; seeking back resets reward state (a cooldown test must show no leak); expectations fail when a rule is deliberately broken (mutation). **Met:** `Lab/` (Scenario, LabEnv on the real EnvSet, RewardLab session, Expectations, LabReport), `tools/reward_lab/reward_lab_run.cpp`, a 10-scenario pack with hand-derived expectations, 20 tests in `tests/test_reward_lab.cpp` (also at the production flags); 24/24 mutations caught (8 in the rewards via the pack, 16 in the Lab). Gaps: no live viewer yet (P2); scenario actions are per decision step, not per tick (P3); one arena, no reward-block hook inside EnvSet (the Lab runs its own copy, checked equal to EnvSet's own in a test). |
| P2 **DONE 2026-11-02** (as a static viewer, see 3.5) | RewardLab host + overlay HUD/primitives/timeline | a human opens one scenario and sees values and gates; Claude reads the same numbers from the DOM; they match the JSONL. **Met:** `Lab/LabExport.h` (run as JSON, one self-contained HTML), `tools/reward_lab/viewer/` (top-down and side views with the reward geometry, HUD of values/gates/components/events as plain DOM with exact `data-*` values, scrubbable timeline with event and gate-flip markers, expectation panel, light and dark), `reward_lab_run --html`. In the browser pane every frame x every car of `passing_setup_straight_hit` was compared to its JSONL (160 reward rows plus all gates, events, components and totals): 0 mismatches. 7 C++ export tests, 64 node checks on the page logic, 28/28 mutations caught (16 page logic, 12 export). Gaps: not live (no step/branch/input from the page: P3), not 3D, the page's click/keyboard handling is exercised by script in the browser, not by an automated test. |
| P3 **DONE 2026-11-02 (P3-1 headless macros, P3-2 live host)** | Input sources: manual, macro record/play, script, grid projection and phase sweep | recorded macro replays bit-exact; grid projection of a known macro matches a hand-built hold-last sequence. **Met:** `Lab/Macro.h` (tick-resolution `Macro`, `*.macro.json` schema 1, `MacroFromLog` recorder, `ProjectToGrid` hold-last at any skip/phase, `RunGrid`, `SweepPhases`), CLI `--record-macro`, `--macro FILE [--skip N] [--phase P \| --sweep]`. A tick-level run (skip 1) recorded, written to JSON, read back and replayed is bit-identical in every frame fingerprint (state, actions, whole reward trace); a grid run expanded to ticks and projected back at phase 0 is the identity. A 1-tick jump press at tick 20 is kept by exactly 1 of 8 phases at skip 8 (phase 4) and by the engine run (only phase 4 leaves the car `hasJumped`). 9 tests in `tests/test_reward_lab_macro.cpp`, 16/16 mutations caught. **P3-2 (live host), met:** `Lab/LabHost.h` (one `LabSession` behind a JSON command interpreter: load, step with explicit actions, step_scripted, seek = branch, reset, macro_record, macro_play, page), `tools/reward_lab/reward_lab_host.cpp` (JSON lines on stdin/stdout), `tools/reward_lab/serve.py` (stdlib HTTP, 127.0.0.1 only, serves the viewer in live mode and forwards commands), viewer live card (keyboard, mouse or sliders drive one car, with configurable bindings that default to Rocket League's keyboard-and-mouse layout (unverified against the game; saved per browser), Enter = one decision step held for tick_skip ticks, other cars follow the script / idle / hold, stepping from an earlier frame branches, Save macro, Play macro at a chosen skip/phase, scenario picker). Live steps are bit-identical to a batch run of the same actions (host test, every frame as JSON; and in the browser the first 13 frames of a keyboard-driven run hash equal to the static batch export). A branch that re-takes the original input reproduces the original future exactly (reward state does not leak). A recorded manual session replays bit-identical through `macro_play`. 9 host tests (`tests/test_reward_lab_host.cpp`), 7 server tests (`tools/reward_lab/test_serve.py`), 73 node checks, mutations 22/22 (host), 12/12 (server), 16 on the page logic (14 killed after one test gap was fixed, 2 equivalent: see the change log). **Not built:** gamepad input, input inside a decision step (one step = one held action), a scripted-controller or `policy` input source, saving bindings anywhere but the browser,  a viewer view of the 120 Hz and the grid trace side by side, multi-user / remote use (loopback only by design). |
| P4 **P4-1 and P4-2 DONE 2026-11-02**; P4-3 (trainer stream): everything EXCEPT the trainer wiring is built (section 10.8), the wiring is held by decision | Capture-from-live, pack manifest and sign-off hash, trainer stream source | stale-hash detection; a pack from an older engine build is refused cleanly. **P4-1 met:** `Lab/Pack.h` + `tools/reward_lab/reward_lab_pack` (`sign`, `check`, `probe`). `manifest.json` records reviewer, date, a sha256 per scenario file, a sha256 per reward over every source file reachable from its header through quoted `#include`s (line endings normalised; a comment edit counts on purpose; the engine itself is outside the closure), the ZeroSum wrapper when a scenario uses it, and a 180-number engine probe (60 decision steps: drive, boost, jump, dodge, a ball hit, a second car). `check` re-derives all of it and re-runs every scenario: UNSIGNED / INVALID / ENGINE_MISMATCH (refused, scenarios not re-run) / STALE (names the scenario or the reward and the file) / CURRENT. Only CURRENT may be called verified. Engine tolerance is 0.1 uu, set 10x above the measured build noise (the same probe at -O0 and -O2 is identical, at the production flags it differs by up to 0.010); the probe is sensitive to a 0.5 uu start offset and a 5 uu ball offset (both far above the tolerance). `sign` needs a reviewer name, refuses a failing pack or a scenario without expectations, and is never run by tooling on its own. 23 tests in `tests/test_reward_lab_pack.cpp` (Lab suite 69/69), SHA-256 checked against published and Python reference vectors, 24/24 mutations caught. **Gaps:** the engine probe cannot prove two engine builds are the same, only that they agree on this probe; a reward in `CommonRewards.h` is stale when ANY reward in that file changes; the Lab harness change is only a warning; the core pack is NOT signed (that is the reviewer's act). **P4-2 (capture-from-live) met:** `Lab/Capture.h`, host command `capture {mode, frame?, name?, steps?}`, and two buttons in the Live card (name field, Save exact, Save state). **replay** is EXACT: the session's start state plus its first k actions as the script; the saved file reproduces frames 0..k bit for bit (every frame fingerprint equal, also in the browser: the downloaded file loaded into the host and replayed equals what the page recorded). **state** is APPROXIMATE: a scenario that starts AT frame k (pose, velocity, spin, boost, jump/flip flags, ball); what a scenario file cannot say restarts at defaults (suspension and contact history, jump/flip timers, boost-pad cooldowns, demo respawn, actions in flight under action_delay), and each loss that applies to the capture is listed as a warning and written into the file's description. A car on a wall or ceiling restarts airborne (the setter derives on-ground from z < 20), so that case warns. **Measured drift** (same actions applied to the original and to the snapshot, 10 steps): on the ground before any contact 0.05-0.06 uu after one step and 0.08 uu (car) / 0.10 uu (ball) over 10 steps; in the air 0; captured just before a jump and after a ball hit up to 18.2 uu (car) / 8.6 uu (ball): an event right after the capture amplifies the restart. A capture never writes expectations (an expectation is what a person says should happen, with the reason). Server whitelists `capture`; the name becomes a download file name and only `[a-z0-9_]` is accepted. 11 tests in `tests/test_reward_lab_capture.cpp` (Lab suite 79/79), 8 server tests, mutations 24/24. **Not built (P4-3):** the trainer stream (UDP) as a read-only source; capture from the trainer; boost-pad and demo state in a state capture. |

First reward through the whole pipeline: the planned wavedash event (it has the oracle labeler, a cap, a cooldown and a grid-sensitive trigger), after P0 instruments one simple reward as the pipe cleaner.

## 8. Risks and limits (say these out loud when reporting)
- **Fidelity of "agreement".** The viewer shows what the reward computed on a scripted situation. It does not show the trained bot learns the mechanic or that constants match real self-play; that stays W&B health plus watching the policy (CLAUDE.md section 7).
- **Instrumentation drift.** A reward whose trace is hand-written separately from its logic can mislead. The single-path rule (3.1) plus the oracle channel are the mitigation; review each instrumented reward for it.
- **Wrapper semantics.** `ZeroSum` subtracts the opponent's value and `gated` can zero a term; the trace must show raw, wrapped, and final values or it will disagree with the stack sum.
- **Engine state.** `engineExtra` is opaque and version-bound; `tickCount` is not restorable, so anything reading absolute tick counts must be checked under replay.
- **Cost.** Per-frame trace storage is small, but primitives per step are capped (proposal: 256) so a verbose reward cannot flood the socket.
- **Page work is real work.** The existing page has no overlay layer; P2 is the largest single item.

## 9. Open decisions
1. Link the GigaLearnCPP library or lift the snapshot code into RLGymCPP (3.2). Recommend: link.
2. Is "locations" (3.4) the right reading?
3. Manual control: step-locked only, or also real-time with latency? Recommend step-locked plus slow-mo.
4. First instrumented rewards (P0): suggest `OffsidePenalty` (geometric gate), `CarEnergyPotentialReward` (PBRS), a cooldown reward from `RewardTiming.h`.

## 10. P4-3 scope: the trainer stream (2026-11-02; read-only investigation, NOTHING built, no trainer file touched)

**Goal:** see the live policy's reward trace (values, gates, events, geometry) in the Lab viewer, read-only, as a third `TraceSource` next to the in-process Lab and a recorded file (3.4).

### 10.1 What exists today (read from the code)
- **Render mode already plays the policy.** `Learner.cpp` ~233/264/310: render mode builds a 1-arena `EnvSet` and a `RenderSender`, loads the newest checkpoint (live reload, `ReloadNewestCheckpointForRender`), and every step calls `renderSender->Send(envSet->state.gameStates[0], vizControl->StatusJSON())` (~2174; ~1959 while parked). `ExampleMain.cpp` ~481 gives arena 0 the real `BuildRewards()` stack, so those rewards DO run in render mode.
- **The existing stream cannot carry a trace.** `RenderSender::Send` serialises per player `car_id, team_num, phys{pos, forward/right/up, vel, ang_vel}, is_demoed, on_ground, ball_touched, has_flip, boost_amount`, `boost_pads`, `actions`, and passes `pulsar` (the `VizControl::StatusJSON()` string) verbatim. Wire: python `render_receiver.py` (imported by the `RenderSender` constructor) sends UDP to 9273, `RocketSimVisWeb` reads it.
- **Rewards cannot be re-computed from that stream.** It lacks what rewards read (`hasJumped/hasDoubleJumped/hasFlipped`, `airTimeSinceJump`, `eventState`, the `prev` chain, `ballPred`, pad timers, ...). So the trace has to come from the process that ran the rewards.
- **Reward block** (`EnvSet.cpp` 222-288): `PreStep` on every reward, then `GetAllRewards` on each, times weight, summed. The per-reward output vector is dropped after the sum (only one sampled player is kept, under `saveRewards`). `Reward::trace` (P0) is already a null pointer in training.
- **`RewardWrapper` forwards `GetName()`, `PreStep`, `Reset` and the trace sink** (`RewardWrapper.h`), so a wrapper does not change a reward's W&B key. `EnvSet` resolves `zeroSumPtr` by `dynamic_cast` on `weighted.reward` once, in its constructor (`EnvSet.cpp:111`), and uses it only in the `saveRewards` path.

### 10.2 Options
| | what | trainer files touched | verdict |
|---|---|---|---|
| **A. Tap in the render EnvCreate (recommended)** | a new header `Rewards/RewardStreamTap.h`: a `RewardWrapper` per reward that forwards to its child and records `output`, weight, the sink and (for a ZeroSum child) the pre-ZeroSum value; the tap of the LAST reward assembles the step's `StackStepTrace` and one UDP datagram carrying a full Lab frame (players, ball, actions from `prevAction`, trace) | `src/ExampleMain.cpp`: wrap `result.rewards` in the render branch only, about 8 lines, behind an env var. Nothing in `RLGymCPP/`, `GigaLearnCPP/`, `RenderSender`, `Learner`, `EnvSet` | **Best.** The tap sees the full `GameState`, so no join with the render stream is needed; training arenas are never wrapped (zero cost on the training path by construction) |
| B. Hook inside `EnvSet` | optional callback in the reward block | `EnvSet.cpp/.h`, shared with training | Works, but touches the hot loop of the trainer for something only render mode needs |
| C. Extend `RenderSender` / `pulsar` | put the trace into the existing frame | `RenderSender.cpp`, `Learner.cpp`, and `RocketSimVisWeb` (third-party submodule, which we decided not to patch) | Needs B or A underneath anyway, plus a page we cannot extend |
| D. Run the policy inside the Lab | a `policy` input source | none, but links libtorch, needs the exact obs builder, action parser and model config of the run | Not now: there is no checkpoint yet (training has not started) and the V3 obs builder is not wired; render mode already does this |
| E. Tap training arena 0 | same as A on a training arena | `ExampleMain.cpp` training branch | Not recommended: the training hot path, and arena 0 is a 1v1 sample anyway. If ever wanted: sampled every Nth step |

### 10.3 Design of A (what would be built)
- **Wire:** UDP datagrams to `127.0.0.1:9280` (not 9273, so `render_receiver` / RocketSimVis keep working beside it). `{"kind":"lab_stream_hello","schema":1,"tick_skip":8,"rewards":[{"name","weight","zero_sum"}...],"cars":[...]}` once per episode, then `{"kind":"lab_stream_frame","schema":1,"seq":n,"frame":{FrameToJson}}` per step. Same JSON as the Lab's frames, so the viewer code is reused. Primitives are already capped (256 per car) so a datagram stays well below the 64 KB loopback limit (a 2v2 frame with 3 rewards is about 2.6 KB today; a 3v3 frame with ~12 rewards is estimated at 10-20 KB and must be MEASURED before the format is fixed).
- **Receiver:** `serve.py --stream` binds UDP 9280 on loopback only, keeps a ring (default 4096 frames, about 4.5 minutes at 15 Hz), and serves `/api/stream?after=seq`. A lost or reordered datagram shows as a gap in `seq` (marked in the timeline, never filled in).
- **Page:** a `stream` source: follow-live / pause / scrub the ring, same views and HUD, no expectations (a stream is not a scenario), no step / branch / capture buttons (read-only by construction; the page has no way to send anything to the trainer).
- **Footprint when approved:** NEW `Rewards/RewardStreamTap.h`, `Lab/StreamFrame.h`, `serve.py` stream mode, viewer stream source, tests. TRAINER-side: about 8 lines in `ExampleMain.cpp`'s render branch. That file holds your uncommitted edits, so that change would be committed separately and only on your go-ahead.

### 10.4 What can be built and verified WITHOUT touching the trainer
1. **The tap against ground truth.** `LabEnv::Create(scenario, envRewards)` already lets a real `EnvSet` run supplied rewards. Give it taps instead of bare rewards: (a) the rewards' outputs with taps on equal the outputs with the bare rewards, bit for bit, over many steps (also at the production flags, where only the weighted total may differ by sub-ulp); (b) the tap's trace for every frame equals the `TracedRewardStack` trace of the same run (JSON equality); (c) `GetName()` is unchanged.
2. **The wire.** A fake emitter plus the receiver: frames round-trip, `seq` gaps are reported, oversize frames are refused loudly (not truncated), the ring evicts oldest first, loopback-only bind.
3. **The page stream source** against a recorded stream (exported JSONL), including follow / pause / scrub.
4. **Overhead** of the tap per step (measured, so the render-mode cost is a number).

### 10.5 What stays unverified until a real render run
- The 8-line wiring in `ExampleMain.cpp` and that the real render EnvSet calls the taps in the order assumed (the Lab's `EnvSet` uses the same call order, so this is expected but not proven for the trainer binary).
- Real policy behaviour and real frame sizes (3v3, ~12 rewards).
- That a tapped render run and an untapped one play identically (same seed): checkable only on the trainer.
- Windows vs WSL: the trainer is built on Windows/MSVC in `RLGymBot_v3` and under WSL for pulsar2; UDP loopback across that boundary has not been tried.

### 10.6 Pre-registered exit gates (for the build)
(1) tap on/off rewards bit-identical; (2) a streamed frame equals the Lab frame for the same actions (JSON equality); (3) a dropped datagram is visible as a `seq` gap; (4) an oversize frame is refused with a message; (5) `git diff` of the trainer-side change is only the guarded render branch; (6) measured tap overhead recorded.

### 10.7 Decisions needed from you
1. Go with option A (tap in the render `EnvCreate`, about 8 trainer-side lines)? Or hold the trainer-side wiring until a run exists, and build everything else now?
2. Switch: an env var (`REWARD_LAB_STREAM=127.0.0.1:9280`) or a command-line flag?
3. Port 9280 and a 4096-frame ring: fine?
4. Should the stream page also be able to follow a RECORDED stream file (replay of a past render session)? Cheap once the source exists.

### 10.8 Status, 2026-11-02 (decisions taken: hold the trainer wiring; env var switch; port 9280 and a 4096-message ring; recorded-stream replay: yes)
**Built and tested without touching the trainer:**
- `Rewards/RewardStreamTap.h` (+ `Lab/FrameJson.h`, split out of `LabExport.h`): `StreamTapReward`, `StreamTapCollector`, `InstallStreamTaps(rewards, sink, tickSkip)`, `MaybeInstallStreamTapsFromEnv(rewards, tickSkip)` (does nothing unless `REWARD_LAB_STREAM=127.0.0.1:PORT`; a bad value logs one line and leaves the rewards alone), `UdpStreamSink` (non-blocking, counts drops, never throws), `ParseStreamTarget` (127.x.x.x only, port 1024..65535). Wire format in the header comment: `lab_stream_hello` / `lab_stream_frame` / `lab_stream_error`, one datagram each, `seq` counts every message.
- `tools/reward_lab/lab_stream.py` (receiver, validator, ring, record, replay, page builder) and `serve.py --stream [--stream-port 9280] [--ring 4096] [--record FILE]` / `--replay FILE` / `--no-live`: pages `/stream`, `/api/stream`, `/api/stream/run`, `/api/stream/info`, all GET and read-only.
- Viewer stream mode (`window.LAB_STREAM`): waits for the first frames and reloads itself, follows the newest frame, appends contiguous frames, refetches on a gap / refused frame / sender restart / new episode, an episode selector, and a status line with received / rejected / gaps / restarts. `viewer_core.js appendStreamMessages`.
- `build-lab/reward_lab_stream_demo`: plays a scenario through the REAL tap and `MaybeInstallStreamTapsFromEnv` (the exact call the render branch would make), paced at 15 Hz, so the stream, the receiver and the page can be used and tested today without a trainer.

**Pre-registered gates (10.6), results:**
1. Tap on vs off, rewards bit-identical inside a real `EnvSet` over 30 steps and 4 cars: MET (`Stream_tap_changes_nothing_the_rewards_pay_or_are_called`; `GetName()`, weights and the gated flag unchanged). Run at -O1 (the Lab test build) and again with the production flags (-O3 -march=native -ffast-math ...): 13/13 both times (+273 us per step there).
2. A streamed frame equals the Lab's frame for the same actions, by JSON equality, also over a real UDP socket: MET.
3. A dropped datagram is visible as a `seq` gap (stats, banner, window starts after it): MET (also in the browser: two lost datagrams gave "Lost datagrams (seq 148..149)" and a window of frames 23..40).
4. An oversize frame is refused with a message, not truncated, and takes its seq slot: MET.
5. Trainer-side change is only a guarded render branch: no trainer-side change exists yet. `src/ExampleMain.cpp` shows as modified in `git status`, but those are your earlier uncommitted edits, not this work.
6. Overhead measured: for 2v2 with 3 rewards, largest message 8,774 B, mean 7,276 B; +300 us per env step (jsonizing one frame) on a 92 us step, measured with an in-memory sink, so the real UDP send is not in that number. At the render run's 15 Hz that is 0.5% of the 66 ms step budget.

**Receiver tests:** 20 Python tests (`tools/reward_lab/test_stream.py`) run against messages made by the real tap, including a byte-for-byte check that the Python page builder equals the C++ exporter, and tap -> UDP -> `serve.py --stream --record` -> API -> replay equal. 113 node checks. Mutations: tap 23/24 (the survivor, an unguarded index, is equivalent), `lab_stream.py` 25/26 (the survivor, an explicit error-hole in `build_run`, is redundant with the seq-contiguity check), `appendStreamMessages` 14/14. Lab suite 92/92.

**Still open:**
- The trainer wiring: two lines in the render branch of `ExampleMain.cpp`'s EnvCreate (`MaybeInstallStreamTapsFromEnv(result.rewards, TICK_SKIP);`). Held until you start a render run, then it is a separate commit.
- Real frame sizes: 3v3 with about 12 rewards has not been measured; extrapolating from the 2v2 numbers it may reach 25-35 KB, inside the 60,000-byte limit but not by a wide margin. If it does not fit, frames are refused loudly (never truncated) and the answer is fewer primitives or a stream per car.
- Only 3 rewards emit gates and geometry (`OffsidePenalty`, `CarEnergyPotentialReward`, `PassingSetupReward`); every other reward in the real stack shows values but no geometry until it is instrumented.
- The first frame of a window that does not start at the episode's reset is shown as a state-only frame 0 (its own reward is dropped), and cumulative sums count from the window start; the banner says so.
- A tapped render run and an untapped one playing identically (same seed) can only be checked on the trainer.
