# GCO lookahead test: does short real-sim search improve the policy? (pre-registration, 2026-11-18)

Status: **Phase 0, 1 and 2a DONE (sections 11-12): the screen passed (0.770 vs raw).** Phase 2b not run. Offline and evaluation-only: no training change, no trainer change, no GPU.
Branch `gco-goalcritic`. Doctrine: measurement before machinery. This follows the critic tests
(`GCO_CRITIC_LR_TEST.md` neutral, `GCO_LAMBDA_TEST.md` reverted) and the finding that far-row credit is the binding
constraint (EV falls 0.94 -> 0.69 -> 0.23 -> negative with time-to-end, `GCO_TEAM_CRITIC_TEST.md` section 12).

## 1. Why this test exists

AlphaZero turns a sparse outcome into dense per-move targets by searching. PPO here has no such operator, so far rows
get credit only through a learned critic that cannot see past ~10 s. A prior MuZero-style attempt (RLGymBot_v3, small
scale) did not learn, and mixing it with PPO made PPO worse. Per the 9uz761ua lesson, a mechanism must not touch
training until a measurement says it helps. This test asks the one prior question:

> Is a short lookahead on the real simulator a policy-IMPROVEMENT operator for this checkpoint?

No model is learned: RocketSim is a deterministic, near-exact simulator, and the FFI has an exact whole-arena
snapshot/restore (`rsf_arena_snapshot` / `rsf_arena_restore`, `RocketSimV3FFI.h:82-86`: physics, contacts, RNG;
exercised by `tests/test_viz_snapshot.cpp`).

## 2. Hypotheses

- **H-S (search helps):** lookahead of 2-4 s with the checkpoint's own policy and critic beats the raw policy
  by more than the minimum detectable effect, because within that horizon the critic is reliable (EV ~0.5-0.9) and the
  rollout sees real outcomes the critic only estimates.
- **H-0 (no gain):** leaf values or the opponent model are too weak, or the horizon that matters is longer than the
  horizon where the critic is reliable. Search adds cost and no strength.

## 3. The procedure (the single lever is "search on/off")

At every decision step (15 Hz) of the searching side, with the arena in state s:

1. Candidates: the K most likely legal actions under the policy (masked softmax), plus the argmax. K=8 default.
2. For each candidate a: `snapshot`, apply `a` for the 8-tick window, then roll out H-1 further steps with our
   policy acting for the searching side and a MODELED opponent (section 4), then `restore`.
3. Score(a) = discounted goal outcome inside the rollout + gamma^H * V_real(s_H) (leaf). Mean over M rollouts per
   candidate with common random numbers (the same sampled action stream across candidates, to cut winner's-curse noise).
4. Play the argmax-score candidate. Real opponent actions come from the real opponent, not the model.

Arms (same checkpoint, same fixed kickoff seeds as `GauntletEval`, both orientations):

| Arm | Searching side |
|---|---|
| R | raw policy, deterministic (argmax), as `GauntletEval` plays it |
| S2 | search, H = 2 s (30 steps), leaf on |
| S4 | search, H = 4 s (60 steps), leaf on |
| S4-noleaf | search, H = 4 s, leaf value forced to 0 (rollout goals only) |

Checkpoint: a COPY of the golden `best_r2672_*` full checkpoint (checkpoints rotate; copy first, retry next-newest).
Second checkpoint if it holds a critic: `policy_versions/ref_8000378436` (8B steps), to test whether search helps a
weaker policy more. Not verified that the `ref_*` dirs contain a critic; if policy-only, run only S4-noleaf there.

## 4. Opponent model (the main bias, stated up front)

External bots (Nexto etc.) cannot be snapshotted, so inside rollouts the opponent is modeled as OUR OWN policy
mirrored (self-model). Against frozen bots this is a wrong model; against our raw policy (arm vs R) it is close
to right. Therefore two evaluations:

- **vs R (self-play pair):** search side vs raw side, same checkpoint. Cleanest read of improvement.
  Both orientations, equal games per orientation.
- **vs frozen bots (Dice, GalactusV2, ChronosV2, Nexto):** the real-world read, with model mismatch.

## 5. Measures and pre-registered criteria

Primary: goals-for share vs the three bots (mean of Dice, Galactus, Chronos, as in the lambda test) and win share vs R.

| Measure | H-S true | H-0 true |
|---|---|---|
| vs R, search side goals share (S4 or S2) | >= 0.55 | within 0.50 +- MDE |
| vs bots mean goals-for share, S minus R | >= +MDE, same sign on all four bots | within +-MDE |
| Horizon shape | S4 >= S2 >= R | no ordering |
| Leaf ablation | S4 > S4-noleaf (the critic adds value) | equal |

MDE and game count N are fixed in Phase 0 from the raw policy's per-game variance (`GAUNTLET_LOG.jsonl` plus a
fresh run), targeting MDE <= 5 pts goals-for share per opponent, and written into section 9 BEFORE the main
comparison. If N for that MDE exceeds the compute budget, report the achievable MDE and do not claim below it.

Falsifiers: gain only vs R and not vs bots (model-mismatch artifact); gain in S2 but negative in S4 (horizon exceeds
critic reliability); S4-noleaf >= S4 (critic adds nothing, the search is rollout-only and expensive).

## 6. Phases

- **Phase 0, no policy needed:** (a) microbenchmark `snapshot`/`restore`/step cost on one arena; (b) re-confirm
  exactness (restore then replay equals, per `test_viz_snapshot.cpp`) on a game with 1v1 cars and boost pads;
  (c) compute decisions/sec for the budget and set N and MDE; (d) confirm the harness can rebuild obs and
  masks from arena state inside a rollout (needs an obs-builder reset / `prevAction` handling that `EnvSet` does
  internally, not verified reusable).
- **Phase 1, sanity:** H=0 (or K=1) reproduces R exactly. Any difference is a harness bug and voids the test.
- **Phase 2, main comparison:** arms R, S2, S4, S4-noleaf, vs R and vs bots, N games each.

## 7. Guards (the trainer is live)

- CPU only, `nice -n 19`, thread-capped (`OMP_NUM_THREADS=4` etc.): the 1024 env threads must not be starved.
  No GPU (RTX 5080 is the trainer's). Policy forwards run on CPU libtorch.
- Read only copies of checkpoints. Nothing under `build/checkpoints_gco_6.1/` is written.
- New code lives in its own target/binary (e.g. `tests/` or a lab build dir), NOT linked into `GigaLearnBot`.
  `CMakeLists.txt` currently carries another session's uncommitted edits: do not touch it until those land, or use
  `build-lab` with a separate CMake file.
- No change to `ExampleMain.cpp` or any reward, terminal or training path.

## 8. Decision rule

- **H-S supported** (S beats R by >= MDE on bots, >= 0.55 vs R, ordering holds, leaf adds value): search is a real
  improvement operator. Next, a SEPARATE pre-registered test: distill search choices into the policy
  (policy-target loss, trained offline first, evaluated by this same harness). Never added to the PPO loss until it
  beats the raw policy standalone, and never as a hybrid-restore (recovery doctrine).
- **H-0 supported:** record, drop. Cost of the full integration is saved. Next candidates: critic conditioning
  (no-touch clock, opponent id), then the gate test.
- **Mixed** (vs R only, or S2 only): the usable horizon is short or the opponent model is the limit; next lever is
  an opponent model, not training integration.

## 9. Known unknowns / not verified

- Python `RocketSim==2.2.1` (v2 engine) is used by `research/tools`; I did not confirm whether it has clone/snapshot,
  and the harness must run on the v3 engine for parity anyway. The research venv was not present in this checkout
  when checked, so the likely path is a C++ harness reusing `MatchLoop`/`GauntletEval` pieces and libtorch.
- Snapshot/restore cost per call and decisions/sec are unmeasured; they set N.
- Whether a rollout can reuse `EnvSet` step logic for one arena or needs a hand-rolled step loop (obs, action
  parse, terminals, boost pad state in the snapshot).
- Whether `ref_*` checkpoints include critic weights.
- Winner's curse: even with common random numbers, picking the max of K noisy estimates inflates the chosen
  candidate's score. Measured here only through the end-to-end result, not separately.

## 11. Phase 0 result (2026-11-18, `tools/lookahead_bench/bench.cpp`, target `LookaheadBench`, run niced from `build/`)

CPU only, no checkpoint, no GPU, trainer untouched (the live `GigaLearnBot` binary was not rebuilt; only the bench
target and the library it links). Control inputs were RANDOM, not policy play, so contact-heavy states are
under-represented in the cost numbers.

**(b) Exactness: PASS.** 12 of 12 runs (1v1, 2v2, 3v3 x 4 seeds each): restore equals the snapshot state, and
replaying the same 240-step (128 s) control stream after restore matches the first run bit-for-bit (ball, cars, boost
pads), through the raw FFI snapshot alone, with boost pads changing state during every drive. Not covered: the
facade-side `_ballHitInfo` and `GameEventTracker` state live outside the engine snapshot, so a rollout that reads
touch events must save/restore them itself (a goal-only score does not need them).

**(a)+(c) Cost, one thread, one arena:**

| mode | Step(8 ticks) | snapshot | restore | rollouts/s, H=30 (2 s) | rollouts/s, H=60 (4 s) |
|---|---|---|---|---|---|
| 1v1 | 6.6 us | 11.0 us | 10.6 us | 5,773 | 2,484 |
| 2v2 | 10.8 us | 11.6 us | 11.5 us | 3,270 | 1,571 |
| 3v3 | 15.7 us | 12.8 us | 14.1 us | 1,681 | 1,050 |

Four threads on independent arenas: 1v1 19,392 rollouts/s total (4,848 per thread, about 84% of one thread);
3v3 8,036 total (2,009 per thread, about 120% of the single-thread figure, so no contention penalty; the single
run is noisy). A rollout is snapshot + H steps + restore, with no policy inference.

**What this settles.** The simulator side is cheap: 8 candidates x 2 rollouts of 2 s at 1v1 is about 3 ms of sim per
decision on one thread. It does NOT settle the search cost, because each rollout step also needs policy forwards
(ours and the modeled opponent's): about 16 rollouts x 30 steps x 2 sides = 960 forwards per decision. That
inference is UNMEASURED; from the layer sizes in CLAUDE.md I estimate it at 10x or more the sim cost on CPU, so the
CPU libtorch forward throughput (batched across candidates and arenas) is the next number to measure, not sim speed.

**The binding constraint is game count, not compute.** Using the gauntlet log (`GAUNTLET_LOG.jsonl`: 96 games per
bot per entry, 1v1 wins about 28-38 of 96 vs Chronos/Dice) and a binomial approximation that ignores draws and any
pairing benefit (both unmeasured): detecting a 5-point goals-for-share difference with 80% power needs roughly
1,500 games per arm per bot; a 0.55 win share against R needs roughly 800 games. So the pre-registered MDE of 5 pts
is too ambitious for the first pass. Revised, before any main run:

- Practical-significance floor: **8 points** goals-for share vs bots (a search that is a real improvement operator
  should show far more than that; below 8 it is not worth its inference cost anyway). That needs roughly 600 games
  per arm per bot under the same approximation.
- Use paired kickoff seeds (both arms play the same layouts) and report the paired difference; measure how much
  variance that removes in Phase 1 before committing N.
- Cut arms for the first pass: R, S4, S4-noleaf only (S2 added only if S4 shows a gain).
- Win share vs R tightened to the same 8-point floor (0.58) for the first pass.

### 11b. CPU inference throughput (2026-11-18, `LookaheadInferBench`, copy of `best_r2721_62750066410`, niced, no GPU)

The live architecture is much larger than the layer sizes in CLAUDE.md suggested (shared 2x1152, policy 4x1024,
critic trunk 2x1280; 12.49M parameters, obs 278, 90 actions), so the earlier inference estimate in section 11 was
too low by about 2x. The checkpoint loaded with all parameters finite; I did not check its behavior (no kickoff test
was run on the copy, so "loaded and finite" is all this establishes).

Rows per second (sampled policy action; "+V" adds the critic value):

| batch | 1 thread, policy | 4 threads, policy | 4 threads, policy+V |
|---|---|---|---|
| 1 | 1,015 | 2,612 | 658 |
| 16 | 5,023 | 8,698 | 4,278 |
| 64 | 7,387 | 20,421 | 8,987 |
| 256 | 8,348 | 24,768 | 10,202 |
| 1024 | 7,628 | 27,320 | 11,715 |

Takeaways. (1) Throughput needs batch >= 64 to approach its ceiling (about 27k policy rows/s on 4 threads), so the
search must batch candidates x rollouts across many concurrent games, not one decision at a time (one decision is
about 16 rows, which runs at about a third of the ceiling). (2) The critic costs about as much again as the policy per
row, so it should be evaluated only at leaves (about 16 rows per decision), never at every rollout step.

**Budget, 1v1, K=8 candidates, M=2 rollouts, 4 threads, ceiling throughput:** one decision is 16 rollouts x H steps x
2 sides (ours + the mirrored opponent model). H=30: 960 policy rows, about 35 ms of inference plus about 3 ms of sim.
A 1v1 game is about 20 s (gauntlet log mean) = about 300 decisions, so about 11 s per game at H=30 and about 21 s at
H=60. Inference is therefore roughly 10x the sim cost, as suspected. 600 games per arm per bot is then about
1.8 h (H=30) or 3.5 h (H=60) per arm per bot on 4 threads; the full first pass (S4 and S4-noleaf over three bots, plus
vs R) would be about 20+ hours. This excludes the external bots' own inference. 2v2/3v3 are costlier on every term.

**Therefore the test is staged, cheapest first (replaces the single Phase 2 comparison):**

- **Phase 2a, screen:** S2 (H=30, K=8, M=1 to halve the cost, about 5 s per game) vs R, both orientations, 200 paired
  games, about 20 min on 4 threads. The standard error of the win share is about 0.035, so this detects a gain of
  about 10 points or more, which is the only size worth building on. Gate: win share < 0.55 -> stop, H-0, nothing else
  is run. Win share >= 0.55 -> continue.
- **Phase 2b, confirm:** S2 and S4 vs R and vs bots at the 8-point floor, N chosen from the paired variance measured
  in 2a. S4-noleaf only if S4 shows a gain (it answers whether the critic matters, which only matters if search does).

## 12. Phase 1 and Phase 2a result (2026-11-18, `tools/lookahead_bench/search_match.cpp`, target `LookaheadSearch`)

Checkpoint: copy of `best_r2721_62750066410` (1v1 rating 2721 in the run's own units). CPU only, 4 pinned cores, niced.
Game: kickoff, first goal ends it, 30 s cap = draw (scored 0.5). Both sides SAMPLE from the policy (temperature 1).
Sides alternate blue/orange. Opponent model inside rollouts = the same raw policy (an exact distributional model
here, the best case for search; see caveats). Common random numbers across candidates for opponent actions and
every rollout step after the first.

**Phase 1 (fidelity): PASS.** Snapshot into a different arena + replay of 20 recorded decisions: end state
bit-identical in 6 of 6 trials; obs identical in the final run (one earlier run showed 2e-6 on one trial, no cause
found; pass bar set to obs within 1e-4 plus an identical end state). This covers `car->controls`, `_ballHitInfo`,
GameState/prevAction copy and the split-step protocol, not only engine state.

**Calibration (400 raw-vs-raw games):** blue score 0.505 (parity); critic vs realised discounted outcome
corr^2 = 0.289, fit G = 0.4618 V - 0.0033 (the critic over-predicts; leaf values are shrunk by 0.46).

**Phase 2a screen, S2 (H=30 steps = 2 s, K=8, M=1, leaf on) vs raw, 200 games, seed 777:**
W 138, L 30, D 32, **score 0.770** (se about 0.035), mean game 18.0 s vs 24.4 s raw-vs-raw. Gate was >= 0.55: **PASSED**
(about 6 se above the gate). A 16-game smoke test beforehand (0.750, different seed) is not part of the 200.

**Control (added after seeing the result, so labelled as post-hoc): the same raw policy played with deterministic argmax**
against a sampled raw opponent, 400 games: W 128, L 84, D 188, **score 0.555** (se about 0.025). So de-noising the
action (argmax instead of a temperature-1 sample) is worth about +5.5 points and a lot of draws; the search's +27 is
mostly not that. Search still wins 0.77 against the same sampled opponent.

**What this does and does not show.** It shows that, against an opponent whose behaviour the rollouts model exactly,
a 2 s lookahead on the real sim improves this checkpoint a lot more than de-noising does. It does NOT show:
- improvement against the frozen bots (their actions cannot be modeled; Phase 2b, untested);
- improvement against a differently-behaving opponent (e.g. a deterministic one, which the model mismatches);
- that the critic leaf matters (S2-noleaf not run), or the horizon shape (S4 not run);
- anything about 2v2/3v3.
The scale of the effect also makes a harness leak worth ruling out before building on it. I checked by reading: the
real opponent action and the rollout opponent samples use different uniforms (no future-action leak), and the snapshot
carries no future information, but I have not run a leak test (for example giving the search an opponent that plays
argmax while the rollouts model sampling, or a search that is told the wrong opponent).

## 10. Record (fill in)

| Item | Value |
|---|---|
| Checkpoint(s) / step | |
| Snapshot+restore cost, decisions/sec | Phase 0: snapshot about 11 us, restore about 11 us, 1v1 step 6.6 us; exact (section 11). Decisions/sec needs inference cost. |
| N per arm, MDE | |
| Phase 1 sanity (H=0 == R) | |
| vs R: S2 / S4 / S4-noleaf win share | |
| vs bots goals-for share: R / S2 / S4 / S4-noleaf | |
| Verdict | |
