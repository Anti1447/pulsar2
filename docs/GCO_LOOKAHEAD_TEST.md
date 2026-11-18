# GCO lookahead test: does short real-sim search improve the policy? (pre-registration, 2026-11-18)

Status: **PROPOSED, nothing implemented.** Offline and evaluation-only: no training change, no trainer change, no GPU.
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

## 10. Record (fill in)

| Item | Value |
|---|---|
| Checkpoint(s) / step | |
| Snapshot+restore cost, decisions/sec | |
| N per arm, MDE | |
| Phase 1 sanity (H=0 == R) | |
| vs R: S2 / S4 / S4-noleaf win share | |
| vs bots goals-for share: R / S2 / S4 / S4-noleaf | |
| Verdict | |
