# Reward Lab: capturing a frame as a scenario (the P4-2 guide)

Commit `cdd71de` ("Reward Lab P4-2"). Part of the Reward Lab; design and status live in `docs/REWARD_VIEWER_SPEC.md`, day-to-day usage in `tools/reward_lab/README.md`.
Written 2026-11-02. Everything below was checked against the code and the tests unless it says **unverified**.

---

## 1. What it is, in one paragraph

While you explore a situation in the Reward Lab's Live page (driving a car, stepping, rewinding), you can save **the frame you are looking at** as a scenario file (`*.scenario.json`).
The file can be run later by the headless runner, opened in the viewer, put in a pack, and used as a regression test for a reward. There are two flavours:

| | **Save exact (replay)** | **Save state (approx)** |
|---|---|---|
| What it stores | the run's *start state* plus *every action up to that frame*, as the scenario's script | a scenario that *starts at* that frame: every car's pose, velocity, spin, boost, jump/flip flags, and the ball |
| Reproduces the original | **bit for bit**, frames 0..k (every frame fingerprint equal, including the whole reward trace) | **approximately** (see section 7) |
| Starts at | where the original run started | the captured frame |
| Use it for | regression tests, "this exact thing happened" | "start me from this situation" |

**It captures Lab sessions** (RocketSim, driven through the same `EnvSet` the trainer uses). It does **not** capture the real Rocket League game, and it does not capture the trainer or the RocketSimVis render viewer. See section 9 for those.

## 2. Intention and goal

The Reward Lab exists so that a human and Claude can **agree on what a reward actually does**, by looking at the same numbers on the same situation. P1-P3 made situations *authorable* (written by hand) and *explorable* (driven live). P4-2 closes the loop the other way: when you stumble on a situation worth keeping, one click turns it into a file instead of a description.

Design rules it follows, on purpose:

1. **Honest about what it keeps.** The exact flavour is exact because it replays; the state flavour says what it loses, in the UI, in the response, and inside the file's description.
2. **A capture never writes expectations.** An expectation is what a *person* says should happen, with the reason (`intent`). A machine-written one would only echo what happened, which is the opposite of agreement. So a captured scenario is not a test until you add expectations. The runner says "no expectations" until you do, and `reward_lab_pack sign` refuses it.
3. **The page cannot write to your disk or name a path.** The file is a browser download; the name is restricted to `[a-z0-9_]` because it becomes a file name.

## 3. Quick start (5 minutes)

**One-time build** (from the repo root, needs `build/collision_meshes`):

```bash
tools/reward_lab/build.sh            # builds build-lab/reward_lab_run, reward_lab_host, reward_lab_pack
```

**Run the live page:**

```bash
python3 tools/reward_lab/serve.py                    # http://127.0.0.1:8765/   (--port N, --pack DIR)
```

**Capture a frame:**

1. Open the page. In the **Live** card pick a scenario and press **Load** (it loads as a sandbox of up to 600 steps).
2. Drive: hold keys (default layout: `W/S` throttle+pitch, `A/D` steer+yaw, `Q/E` air roll, left mouse = boost, right mouse = jump over the field views, `Enter` = take one decision step). Take a few steps. You can also use the sliders and **Step / Step N**.
3. Scrub to the frame you want (timeline, `Left/Right` arrows). Scrubbing does **not** change the session; capturing an earlier frame does not branch it.
4. (Optional) type a name in **"capture the frame shown as"**. Leave it empty for the default `captured_<scenario>_f<k>_replay` or `..._state`.
5. Click **Save exact (replay)** or **Save state (approx)**. The browser downloads `<name>.scenario.json`. The status line says what was saved, and anything a state capture loses is listed under the buttons.

**Use the file:**

```bash
mkdir -p tools/reward_lab/packs/captured
mv ~/Downloads/captured_*.scenario.json tools/reward_lab/packs/captured/
build-lab/reward_lab_run tools/reward_lab/packs/captured/captured_x_f9_replay.scenario.json
```

It runs, prints the report, and says it has no expectations (exit code 1). Open the file, write expectations with an `intent` line each (copy the format from any file in `packs/core/`), run again. To see it in the live page, serve the folder: `python3 tools/reward_lab/serve.py --pack tools/reward_lab/packs/captured` (or a parent folder to list several packs; scenario names must be unique) and reload the page, because the scenario list is read when the page loads.

**From code instead of the page** (same thing the buttons call):

```bash
curl -s -X POST -H 'X-Lab-Client: 1' -H 'Content-Type: application/json' \
  -d '{"cmd":"capture","mode":"state","frame":12,"name":"my_setup","steps":20}' http://127.0.0.1:8765/api
```

Reply: `{"ok":true,"mode":"state","name":"my_setup","scenario":{...},"warnings":[...]}`. `frame` defaults to the last frame, `name` to the default name, and `steps` (state mode only) to the steps the original had left. The **buttons do not send `steps`**, which matters in practice, see section 6.

## 4. What you can do with it

- **Turn a hand-found situation into a regression test.** Reproduce it once, save the exact replay, add expectations. A later change that breaks the behaviour fails the scenario.
- **Pin a setup for a mechanic.** Drive to the moment before a flip reset, a wavedash landing or a shot, save the *state*, and you have a starting situation you can reach in one step, many times, with different inputs.
- **Hand a situation to Claude as a file.** It is plain JSON (schema 1). Claude can read it, run it, trace a reward on it, and draft expectations with you. This is the "agreement" workflow.
- **Compare a reward before and after an edit on the identical situation.** An exact replay reproduces the original run, so the trace of the old and the new reward differ only because of the reward. Diff the two `.trace.jsonl` files.
- **Build packs for sign-off.** Captured scenarios with your expectations go in a pack; `reward_lab_pack sign` (your act) records the reviewer, the reward-source hashes and an engine probe; `check` later says CURRENT / STALE.
- **Combine with macros (P3-1).** Load a captured scenario, then play a recorded 120 Hz macro on it at different skip/phase values to see how often a reward pays on the decision grid.

## 5. How it works (so the limits make sense)

- **The Lab's timeline is "start state + action log".** Rewinding re-simulates the log on a fresh arena. That is why an exact capture is exact: it just writes that start state and that log (as hold-until-next script entries, one entry per change, per car) into a scenario with `steps = k`. It copies the session's rewards, weights, tick skip and action delay, adds the tag `captured`, and clears the expectations.
- **A state capture replays a private copy of the session to frame k**, reads the engine's `GameState`, and writes each car (team, position, velocity, angular velocity, orientation converted to yaw/pitch/roll degrees through `Angle::FromRotMat`, boost, `hasJumped/hasDoubleJumped/hasFlipped`, `airTimeSinceJump`, demolished) and the ball into an authored scenario with no script. The session you are looking at is not touched (tested: the run JSON is identical before and after a capture).
- **The strict scenario format is the ceiling.** A scenario file can only say what `Scenario.h` has fields for. Anything else restarts at the engine's default when the file is loaded. That is the whole reason a state capture is approximate.

## 6. Best practices

1. **Default to exact for tests.** Use state only when you really need to *start* mid-situation.
2. **Write the expected value by hand first**, with the derivation in the scenario's `description` (the rule from CLAUDE-style harness work: two of ten core scenarios were wrong the first time because the expectation was guessed). Then run.
3. **Capture a few steps *before* the interesting event, not at it.** A state capture is accurate on the ground and exact in the air, but a **jump or a ball touch right after the capture can amplify the restart error to tens of uu** (measured: up to 18 uu on the car). Three or more steps of margin lets the restart settle.
4. **Shorten the horizon.** From the page, a state capture's `steps` is the sandbox length minus the frame (about 580 for a 600-step sandbox). Edit `"steps"` in the file to what you need, or use the host command's `steps`.
5. **Check the warnings and read the description.** They name what *this* capture lost (wall car, mid-jump, pads on cooldown, action delay). If a warning describes your situation, prefer an exact capture or a different frame.
6. **Keep scripted cars off the boost-pad grid** (small pads on x = 0, ±940, ...; big at (±3072, ±4096), (±3584, 0)). A state capture restarts with **every pad active**, so a car idling on a pad will pick up boost the original did not.
7. **Put captures in their own folder**, not in `packs/core`, so you do not stale a signed pack by adding files. Name by situation (`flip_reset_setup_f14`) rather than by default, and keep the file name equal to the `name` inside it (the runner writes `<name>.report.txt`).
8. **Add expectations in pairs** (golden path and one failure per gate), with an `intent` line each; see `packs/core/` for the pattern.
9. **Re-run after every reward edit** and, if you sign off a pack, run `reward_lab_pack check` before saying a reward is verified.
10. **Never describe a state capture as "the original continued."** It is a new scenario that begins in a similar place.

## 7. Limitations of the current implementation

**Scope**
- Captures **Lab sessions only**: one arena, RocketSim via `EnvSet`, at most 3 cars per team, actions per decision step (tick skip 8 by default), not per tick. Frames are decision steps, so you cannot capture "tick 5 of a step".
- No command-line flag for capture; it is the page buttons or the host/HTTP command.
- The page cannot load a scenario file from disk (only from the served pack folder, or a macro from disk). One session per server: two browser tabs share it.

**State capture (approximate)** keeps pose, velocity, spin, boost, jump/flip flags, `airTimeSinceJump`, demolished flag and the ball. It **loses**:
- the suspension and wheel-contact history (a ground car's first steps settle, about 0.05 uu after one step);
- jump and flip **timers** (a car mid-jump or mid-flip restarts those: warned);
- **boost-pad cooldowns** (all pads active: warned);
- a demolished car's respawn timer (warned);
- **actions in flight** under `action_delay > 0` (warned);
- the ball's and cars' other engine-internal bookkeeping;
- a car **on a wall or the ceiling restarts airborne**, because the scenario setter derives "on ground" from `z < 20` (warned), and a car airborne below `z = 20` restarts on the ground (warned).

**Reward state is not captured by a state capture.** Rewards start from `Reset()` at the scenario start, so cooldowns, windows, PBRS "previous potential" and per-episode caps are all cleared. A reward that depends on history (a cooldown that would still be running, a hit one step ago) behaves differently in the first steps. The exact replay does not have this problem.

**Measured drift** (the same actions given to the original and to the snapshot, 10 steps): about **0.1 uu** on the ground before any contact, **0** for a car in the air, up to **18.2 uu** (car) and **8.6 uu** (ball) when the capture is just before a jump and after a ball hit. These numbers are for one scenario (2v2 drive-and-hit); they show the regimes, not a guarantee.

**Exact capture**
- It reproduces the original **in this engine build**. Across builds the pack probe (0.1 uu tolerance) is what tells you whether results are comparable, and across compiler flags the engine probe differed by up to 0.010 uu in testing.
- It requires frame >= 1 (frame 0 has no actions; use state for it).

**Naming:** `[a-z0-9_]` only, starting with a letter or digit, at most 64 characters.

**Not tested:** the actual browser *save* dialog or download behaviour in a normal browser window. I verified the download path by intercepting the anchor click in the browser pane and reading the file it would have saved. Some browsers ask permission for several downloads in a row.

## 8. Troubleshooting

| What you see | Cause | Fix |
|---|---|---|
| `name must be lowercase letters, digits and _ (starting with a letter or digit), at most 64 characters` | the name has upper case, spaces, `/`, `.` or is too long | use e.g. `flip_reset_setup_f14` |
| `frame 0 has no actions to replay: use "Save state" for it, or step first` (page) / `an exact replay needs a frame in 1..N` (host) | exact capture at frame 0, or past the end | scrub to frame >= 1, or use Save state |
| `nothing loaded` | the host was restarted (server restarted, host crashed) or no scenario was ever loaded | press **Load**; if it keeps happening, check the terminal running `serve.py` |
| `server unreachable: ...` | `serve.py` stopped or the port changed | restart it; reload the page |
| `the reward_lab_host process died and could not be restarted` | the host binary crashes at start (missing meshes, bad build) | run `build-lab/reward_lab_host` by hand from the repo root; check `build/collision_meshes` exists |
| `missing .../build-lab/reward_lab_host (run tools/reward_lab/build.sh)` | not built | `tools/reward_lab/build.sh` |
| `403 missing X-Lab-Client header` / `bad Host header` (curl, scripts) | the API refuses cross-site style calls | send `X-Lab-Client: 1`, and call exactly `127.0.0.1:PORT` or `localhost:PORT` |
| `command not allowed from the browser` | you sent `page` or an unknown command | the browser may only send load_named, load, state, step, step_scripted, seek, reset, macro_record, macro_play, capture |
| No file downloads | browser blocked the download | allow downloads / multiple downloads for `127.0.0.1:PORT` **(unverified: depends on the browser)** |
| Captured file runs but the runner exits 1 with "no expectations" | by design | add expectations with `intent` lines |
| `reward_lab_pack sign` says `has no expectations, nothing to sign off` | same | same |
| Runner says `unknown key` / `unknown reward` after you edited the file | the scenario format is strict; only rewards registered in `Lab/RewardRegistry.h` can be named | fix the key, or register the reward (one line) |
| State capture "drifts" from the original | expected (section 7) | capture earlier, in the air, or use exact; read the warnings |
| State capture of a car on a wall starts in the air | the setter derives on-ground from `z < 20` | capture a different frame, or use exact |
| The captured scenario is 580 steps long | the page's sandbox length minus the frame | edit `"steps"` or use the host command's `steps` |
| The new scenario is not in the page's list | the list is read when the page loads, from the `--pack` folder | put the file in the served folder and reload the page |
| Two scenarios with one name in the list | the list is keyed by file name across the folder tree; the later one wins | make names unique |
| Reward values differ in a state capture's first steps | reward state (cooldowns etc.) restarted | capture exact, or start earlier |

## 9. Future thoughts

### 9.1 Does it already capture a live *training* state in RocketSim? No.
Capture works on Lab sessions. The trainer's render viewer (`VizControl` + `SnapshotRing`, UDP 9276) can already pause, scrub, edit and step the render arena's frames, but **nothing exports a scenario file from it**, and training arenas are not observable at all (no ring). So this is new work, not an existing feature.

### 9.2 Capturing from the trainer (the promising route)
The same trick that makes the exact capture exact would work for a training or render arena: **record the episode's start state and the actions taken**, and replay them in the Lab.
- A small **episode recorder** (per arena, off by default, ideally arena 0 or a sampled few): the state setter's start state at each reset (cars, ball: the same fields a scenario has) plus the action vector per step. Memory is small (about 8 floats x cars x steps, tens of KB per episode).
- On a **trigger** (a file touch, a key in the viewer, or a metric event such as a rare reward firing) write the current episode as an exact-replay scenario. No hidden engine state is needed, because the replay starts from a fresh arena exactly as the episode did.
- Checks needed first: that the start state is fully expressible in the scenario fields (kickoff and random state setters probably are; unverified), that the trainer's tick skip, action delay and engine build match the Lab's (the pack engine probe tells you), and that the cost on the training hot path is nil when disabled.
- This touches trainer code (`EnvSet` or the state-setter/`ExampleMain` wiring), so it needs your go-ahead; it belongs with the P4-3 decision in section 10 of the spec (same gate: nothing in the hot loop unless enabled).
- The scoped P4-3 "tap" (stream of frames from the render run) could also carry the fields the render stream lacks (`hasJumped`, air time, ...), which would make a **state capture from the render run** much better than anything built from RocketSimVis's stream.
- A **true engine-state capture** (including suspension, `engineExtra`, pad timers, RNG) is the spec's "captured, exact" scenario kind. It is bound to the engine build and the V3 (Rust FFI) facade's ability to save and restore full state **has not been investigated**; the Lab's scenario setter cannot write it today.

### 9.3 A BakkesMod plugin (capture from the real game)
Idea: a plugin that writes the game's current frame to a file the Lab can import as a state scenario; also, optionally, the **human's inputs** at game-tick resolution, which feeds the P3-1 macro machinery (record yourself doing a mechanic at 120 Hz, project it onto the decision grid, see how often a reward pays).
- **Converter:** a small tool that maps a game frame to a scenario: car team, position, velocity, angular velocity, orientation (game rotators to yaw/pitch/roll degrees, sign conventions to be **verified**), boost, ball. Same loss list as the state capture, **plus** the game and RocketSim are different simulators, so the restart error is larger and unknown until measured.
- **Validation experiment before trusting it:** in freeplay, export a stretch of frames and the inputs; import the first frame into the Lab; replay the inputs; compare trajectories. That measures RocketSim-vs-game fidelity and the state capture together, and tells you whether the feature is useful or only a sketch tool.
- Things to check in the plugin SDK (**unverified**): which jump/flip/air-time flags the car wrapper exposes, whether boost pad states and the demolition timer are readable, and the frame timing relative to the decision grid (the game does not run on your 8-tick grid, so a captured frame has no natural "step" alignment).
- There is a `BakkesPlugins/` folder in the RLGymBot_v3 repo that I have **not** looked at; it may already contain an exporter worth building on.
- Use in freeplay or custom training only; running a plugin in online matches is a separate risk to check with the BakkesMod terms.

### 9.4 Smaller follow-ups
- A page control for the `steps` horizon on a state capture (today only the host command has it).
- A scenario file picker in the live page (today files must sit in the served folder).
- Optional "suggested expectations" drafts clearly marked as drafts, never saved as expectations (the person still writes the `intent`).
- Boost-pad and demo state in a state capture (needs the scenario format to grow fields).
- A reward-state carry-over option for state captures (replay the rewards silently over the lead-in steps, then start recording) so that cooldown-style rewards start in the right state without the full exact replay.

---

## Appendix: files and tests

- Code: `GigaLearnCPP/RLGymCPP/src/RLGymCPP/Lab/Capture.h`, `Lab/LabHost.h` (command), `Lab/RewardLab.h` (`LiveState()`), `tools/reward_lab/serve.py` (whitelist), `tools/reward_lab/viewer/` (buttons).
- Tests: `tests/test_reward_lab_capture.cpp` (11 tests; replay bit-exact through a file, minimal entries, no expectations, refusals, session not disturbed, state fidelity, airborne orientation round-trip, measured drift in three regimes, warnings, host command, a captured file loads and replays), `tools/reward_lab/test_serve.py` (capture through the API, name safety). Lab suite 79/79 at the time of the commit; 24/24 deliberate mutations of the capture code caught.
