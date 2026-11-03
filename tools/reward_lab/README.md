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

## Macros and the decision grid (P3, headless)

A macro is what was done at engine-tick resolution (120 Hz); the bot decides every `tick_skip` ticks and holds. So a macro is projected onto the
decision grid before it is judged: decision step k takes the macro's action at tick `phase + k*skip` and holds it (**hold-last**). Anything done
between two sampled ticks is lost; that is the point (a 1-tick press survives 1 phase of 8 at skip 8).

```bash
# record a scenario's scripted run as a macro (a scenario with "tick_skip":1 gives an exact 120 Hz recording)
build-lab/reward_lab_run packs/x.scenario.json --record-macro /tmp/x.macro.json
# play a macro INSTEAD of the script: --skip N (default the scenario's tick_skip), --phase P, or --sweep for every phase
build-lab/reward_lab_run packs/x.scenario.json --macro /tmp/x.macro.json --sweep
```

File: `{"schema":1,"kind":"lab_macro","cars":N,"ticks":T,"entries":[{"car":0,"tick":20,"action":{...}}]}`, hold-until-next per car, strict parsing like scenarios.
Expectations are written for the scenario's own tick skip and are only evaluated when the macro is played at that skip. A sweep prints, per phase, the
weighted sum per reward per car and the event counts. Code: `Lab/Macro.h`; tests: `tests/test_reward_lab_macro.cpp`.
Not built: recording from a person (needs a live host), a viewer view of the 120 Hz and the grid trace side by side.

## Live mode (P3-2)

```bash
tools/reward_lab/build.sh                       # also builds build-lab/reward_lab_host
python3 tools/reward_lab/serve.py               # http://127.0.0.1:8765/  (--port N, --pack DIR)
python3 tools/reward_lab/test_serve.py          # server tests
```

The page is the same viewer plus a **Live** card. Hold keys (or click, over the two field views) to set the selected car's inputs, or use the sliders, then the Step key
(`Enter` by default) or the Step button takes ONE decision step: the inputs are held for
`tick_skip` engine ticks, exactly like the bot's. "Step N" holds them for N steps. Other cars follow the scenario's script, idle, or hold. Stepping while you are
looking at an earlier frame BRANCHES: the host re-simulates the action log up to that frame (fresh arena, fresh reward state) and the later frames are dropped.
Load picks a scenario from the pack as a sandbox (length up to the number you give, default 600; its expectations are still judged on the frames you made).
Save macro downloads the session as a tick-resolution `*.macro.json`; Play macro loads one at the skip/phase you choose (hold-last projection, see above).

**Controls are configurable** (Live card > Controls). The default layout is Rocket League's keyboard and mouse: `W`/`S` throttle and pitch, `A`/`D` steer and yaw, `Q`/`E` air roll
left/right, left mouse boost, right mouse jump, left Shift powerslide, `Enter` step. W/A/S/D drive both the ground input and the air input, as in the game (the car uses
throttle/steer on the ground and pitch/yaw in the air; a pitch input on the ground changes nothing, checked on the engine). Each input has a list of keys: **Add key** then press a key or
click a mouse button (Esc cancels), x removes one, a layout list switches between "Rocket League default", "Keyboard only" and "Lab: separate air keys", and Reset to layout restores
the selected one. Edits are saved in this browser (localStorage) and shown as "Custom". Bindings use the physical key (`KeyW`, `ShiftLeft`, ...), so layouts do not matter. A key bound to
something that cancels itself, to the step key and an input, or to a playback key (Space, arrows, Home/End, `[` `]`) shows a warning; a live binding beats the playback key.
**The Rocket League layout is unverified**: it comes from Liquipedia's control-settings table as summarised by a search (the page itself returned 403), and the summary was not consistent about
air roll; check it against Settings > Controls in the game and change it here if it differs. Measured engine signs: pitch +1 = nose up, yaw +1 / roll +1 / steer +1 = right.

Architecture: `Lab/LabHost.h` (command interpreter around one `LabSession`) <- `reward_lab_host` (JSON lines on stdin/stdout) <- `serve.py` (HTTP) <- the page.
Everything shown is what the real EnvSet / reward stack returned; the page simulates and scores nothing. Safety: loopback only; `POST /api` needs the
`X-Lab-Client: 1` header and a Host header naming the server; the browser can only send load_named/load/state/step/step_scripted/seek/reset/macro_record/macro_play
(never a file path or the `page` command). A scenario is picked by NAME from the pack folder.
Limits: one session per server (a second browser tab shares it); one decision step is one held action; no gamepad.

## Pack sign-off (P4-1)

```bash
build-lab/reward_lab_pack check tools/reward_lab/packs/core      # exit 0 CURRENT, 1 STALE / ENGINE_MISMATCH, 2 INVALID, 3 UNSIGNED
build-lab/reward_lab_pack sign  tools/reward_lab/packs/core --reviewer "Your Name"   # YOUR act: writes manifest.json
build-lab/reward_lab_pack probe                                  # the engine samples stored under "engine"
```

The manifest says "this person reviewed THESE scenarios, against THESE reward sources, on THIS engine behaviour, and they passed". `check` re-derives each part:
scenario files (sha256), each reward's source (sha256 over every file reachable from its header through quoted `#include`s: a comment edit counts; the engine is outside),
the engine (a 60-step probe compared within 0.1 uu; a different engine is REFUSED and the scenarios are not re-run), and the results (every scenario is re-run and must pass).
The verdict names what changed ("reward OffsidePenalty changed after sign-off: Rewards/OffsidePenalty.h"). Only **CURRENT** may be called verified. The Lab harness changing is
a warning, not a stale. Limits: the probe shows two engines agree on that probe, not that they are identical; a reward in `CommonRewards.h` goes stale when any reward in that
file changes. `sign` refuses a failing pack, a scenario with no expectations, and a missing reviewer; it is never run by tooling on its own. The core pack ships unsigned.

## Capture a frame as a scenario (P4-2)

In the Live card, scrub to a frame, type a name (optional), then:
- **Save exact (replay)**: the run's start state plus the actions up to that frame, as a scenario. Running it reproduces frames 0..k bit for bit. It starts where the run started, not at frame k.
- **Save state (approx)**: a scenario that STARTS at that frame (poses, velocities, spin, boost, jump/flip flags, ball). The hidden engine state restarts at defaults (suspension and contact history,
  jump/flip timers, boost-pad cooldowns, demo timers, actions in flight under `action_delay`); what applies to your capture is listed under the buttons and written into the file's description.
  Measured drift when the snapshot and the original get the same actions for 10 steps: about 0.1 uu on the ground before any contact, 0 in the air, but **up to 18 uu** when a jump or a ball touch
  happens right after the capture. Treat it as a starting point for a situation, not as the original continued.

A capture has **no expectations** on purpose: an expectation is what a person says should happen, with the reason. Add them (with an `intent` line) before it counts as a test; the runner says
"no expectations" until you do and `reward_lab_pack sign` refuses it. The same thing from code: host command `capture` (`Lab/Capture.h`).

## Watching a render run's rewards: the stream (P4-3)

```bash
python3 tools/reward_lab/serve.py --stream --no-live --record /tmp/session.jsonl      # receives on UDP 127.0.0.1:9280, page at http://127.0.0.1:8765/stream
REWARD_LAB_STREAM=127.0.0.1:9280 build-lab/reward_lab_stream_demo tools/reward_lab/packs/core/passing_setup_straight_hit.scenario.json --loop    # a stand-in sender
python3 tools/reward_lab/serve.py --replay /tmp/session.jsonl --no-live                # replay a recorded session later
python3 tools/reward_lab/test_stream.py                                                # receiver tests (needs the build)
```

The sender is `Rewards/RewardStreamTap.h`: it wraps the rewards of ONE arena (render mode), sends each step's Lab frame (players, ball, actions, the whole reward trace) as one JSON datagram,
and passes every reward's value through untouched. It does nothing unless `REWARD_LAB_STREAM=127.0.0.1:PORT` is set (loopback only; anything else is refused with one log line). **It is not
wired into the trainer yet** (held by decision): the render branch of `ExampleMain.cpp` would call `MaybeInstallStreamTapsFromEnv(result.rewards, TICK_SKIP)`; until then `reward_lab_stream_demo`
sends what a render run would. The stream page follows the newest frame, appends frames while they are contiguous, and refetches on a gap, a refused frame, a sender restart or a new episode;
a lost datagram is shown as a gap and never filled in; the run shown is the latest unbroken stretch of one episode. Read-only: the page only GETs, and nothing it does reaches a trainer.
Limits: a frame must fit one datagram (60,000 bytes; larger frames are refused with an error message, not truncated), only rewards that emit show geometry, a window that starts mid-episode
shows its first frame without a reward and counts sums from there. Ring: `--ring N` messages (default 4096, about 4.5 minutes at 15 Hz).

Adding a reward: one line in `Lab/RewardRegistry.h`. Adding a scenario: copy one from `packs/core/`, derive the expected values by hand first and
write the derivation in `description`, keep cars off the boost-pad grid.

Limits: one arena; actions are per decision step (tick_skip engine ticks), not per tick; a run shows a reward's logic on scripted situations,
not that the policy learns the mechanic (that is W&B health plus watching the policy).
