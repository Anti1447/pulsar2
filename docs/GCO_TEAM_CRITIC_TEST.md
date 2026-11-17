# GCO team-mode critic test (pre-registration, 2026-11-17)

Status: **PROPOSED, nothing implemented or enabled.** Branch `gco-goalcritic`. Doctrine: measurement before
machinery; one lever; a revert path. Follows `docs/GCO_CRITIC_LR_TEST.md` (neutral).

## 1. The problem (measured)

From `train-20261117-001921.log` (583 iterations, GCO alone, 200k batch, before the reset-mix and Nexto edits):

| Measure | Result |
|---|---|
| `GCO/Critic Explained Variance` (pooled) | median 0.324, below the 0.41 policy-LR gate |
| EV by mode | 1v1 **0.412**, 2v2 **0.309**, 3v3 **0.273** |
| EV by opponent source | self-play 0.32; old version -0.07; Galactus -0.19; Dice -0.22; Chronos -0.41 |
| Critic LR x2 (earlier test) | update magnitude x2, EV +0.022 -> not optimization-limited |

Rows scale with players (1v1 : 2v2 : 3v3 ~ 2 : 4 : 6 rows per step), so the pooled EV is ~50% 3v3. The gate reads the
pooled value, so the team modes are what hold the policy LR down.

EV here is measured against the raw discounted MC return (`GAE.cpp` `outReturns`, no bootstrap), so it is independent
of lambda and of the critic's own targets.

## 2. Hypothesis

H1 (symmetry): the critic reads `obs(278)` through the shared trunk as a flat vector. Slots are shuffled every build
(`AdvancedObsPadded`, `shuffleSlots`), so an MLP must learn that teammate/opponent order is irrelevant: 1 ordering in
1v1, but 2! x 3! = 12 in 3v3 (2 teammate slots, 3 opponent slots, plus the ego block). Learning that symmetry from
data costs capacity and samples in proportion to team size.

H0 (variance): the 2v2/3v3 gap is mostly irreducible (teammates' sampled actions, unseen opponent identity), and no
critic architecture closes it.

The two hypotheses make different predictions (section 6), so the experiment can separate them.

## 3. The lever (exactly one)

A **permutation-invariant residual value correction**: `V(s) = V_old(s) + Delta(s)`.

- `V_old` is the existing path `obs -> shared_head -> critic_trunk -> critic` (unchanged, seeded).
- `Delta` is a NEW critic-side module that reads the RAW obs (not the shared trunk):
  - ego block (29 floats at the self offset) + header (51: ball, prevAction, boost pads) + ball-prediction slices (48),
  - a shared per-player encoder `phi` (29 -> 128 -> 128, residual+LN like the rest) applied to each of the 2 teammate
    slots and 3 opponent slots, masked by the presence flags, pooled by masked mean per group (teammates, opponents),
    with separate pooled outputs per role (no shared role mixing),
  - concat -> 2x256 MLP -> 1.
- **Last layer zero-initialised**, so `Delta = 0` at the start: the seeded critic is reproduced exactly, no partial
  restore of a hybrid (recovery doctrine), and `Delta` loads fresh the way the goal-critic head did on resume.
- **No gradient reaches the shared trunk or the policy**: `Delta` reads raw obs. This keeps "probes/heads never reshape
  the trunk" (CLAUDE.md law 2/4). Value loss on `Delta` is the same loss as the critic's, on the same targets.
- Used wherever the critic value is used: collection value preds, GAE, the truncation bootstrap, the learn-pass value loss,
  and the EV report.

Constants (verify in `AdvancedObs.h` / `AdvancedObsPadded.cpp` before coding): obs = 51 header + 6 x 29 player blocks
(self, 2 teammate slots, 3 opponent slots) + 5 presence flags = 230; 6.0 appends 8 x 6 ball-prediction floats = 278.

## 4. Prerequisites (measurement first; none changes training)

Land these as telemetry before touching the critic. Each is a new `GCO/*` report key next to the existing EV panels
(`Learner.cpp` ~line 3195):

1. **Source-corrected EV** per mode: EV after subtracting each (mode, source) group's mean residual. Separates the
   "critic cannot tell the opponent" bias from within-group variance.
2. **Mode x source EV matrix** (groups under 300 rows skipped, as now).
3. **EV by time-to-terminal bin** (rows within 2 s, 5 s, 10 s, >10 s of the episode end). Near-terminal EV poor = the
   critic is the problem; far-terminal poor only = irreducible.
4. **Effective independent games** per mode (distinct arenas per iteration, not rows).
5. **Truncated-episode fraction** (the EV target ignores the truncation bootstrap).
6. **Unit tests** (`tests/`, no GPU): `Delta` output unchanged under any permutation of teammate slots and of
   opponent slots; `Delta == 0` exactly at init; padded slots (presence 0) contribute nothing.

If prerequisite 1 shows source-corrected 3v3 EV rising by >= 0.08 over the plain value, opponent identity (a different
lever) should be tested first, not this one.

## 5. Experimental design

Last time the baseline was an earlier time window of a different configuration. This time: **two arms forked from the
SAME checkpoint**, run sequentially on the same fleet (`--gco` alone, same reset mix, same opponents).

- Fork point: copy the newest GCO checkpoint (checkpoints rotate, so copy first; retry on next-newest).
- **Control arm**: current code, no `Delta`.
- **Treatment arm**: `Delta` on.
- Both arms freeze the policy-LR gate (`cfg.exploiter.explainedVarThreshold` set above reach, e.g. 2.0) so the policy LR
  is the same in both and EV differences come from the critic, not from the gate stepping the policy.
- Window per arm: **200M steps** (>= 250 iterations at 400k rows; the previous test's 300M took ~53 min).
- Read per-mode EV over the last 100M steps of each arm, plus the section 4 panels.

## 6. Pre-registered predictions

| Measure (last 100M, treatment minus control) | H1 true | H0 true |
|---|---|---|
| EV 3v3 | **>= +0.05** | within +-0.02 |
| EV 2v2 | **>= +0.03** | within +-0.02 |
| EV 1v1 | within +-0.02 | within +-0.02 |
| Pooled EV | >= +0.03 | within +-0.02 |
| Shape | gain grows with team size (3v3 > 2v2 > 1v1) | no ordering |

Falsifiers: equal gains in all modes (the encoder is just extra capacity, not symmetry); gain only in 1v1; gain only
before the first 50M steps and then gone (overfit/transient).

Health metrics (both arms): value loss, `Critic Update Magnitude`, `Plasticity/*`, policy entropy, `Rating/1v1` and
`Ref/Oldest Share` (should be flat or noise-level since the policy LR is frozen and `Delta` cannot touch the policy).

## 7. Guards and checks before the run

- **LR trap (the `vdag1/vdag2` lr=0 incident):** confirm `SetLearningRates` names the new module, and that its update
  magnitude panel is nonzero after the first iteration. If it reads 0, the mechanism is inert and the result is void.
- **Exactness at init:** the first-iteration value preds of the treatment arm must equal the control's (zero-init).
- **Policy untouched:** policy logits identical between arms at step 0 on a fixed obs batch.
- **No new checkpoint hybrid:** the new module's files are absent from the old checkpoint and initialise fresh; the rest
  loads whole. If the loader fails on the missing files, stop (do not hand-assemble).
- Manual checks every 50M steps; revert on NaN/inf, a 3x persistent value-loss jump, or entropy below half baseline.

## 8. Revert

Delete the module registration (or a config flag set false), rebuild, restart from the control arm's checkpoint. The
shared trunk and the old critic path are untouched, so nothing else needs undoing.

## 9. Decision rule

- **H1 supported** (3v3 >= +0.05, ordered by team size): keep `Delta`, then re-test the policy-LR gate and
  per-mode gating as separate levers.
- **H0 supported** (flat): drop symmetry as the cause. Next candidates, in order: opponent-source one-hot and the
  no-touch clock as privileged critic inputs, then lambda 0.95 -> 0.99, then critic epochs.
- **Mixed** (gain in all modes alike): record as extra capacity, not symmetry; no follow-up on symmetry.

## 10. Known unknowns / what I did not verify

- I did not read `Model`, the critic's save/load, or every `models["critic"]->Forward` call site. The file list to
  change is therefore a hypothesis: `PPOLearner.cpp` (model creation near line 129-148, `SetLearningRates`, value
  forwards at ~273 and ~641, the learn-pass loss), `Learner.cpp` (EV panels), a new module under `PPO/`, `tests/`.
- Whether `Delta` should also feed the goal critic (it reads raw obs already).
- The fork mechanism: whether the launcher can start two arms from a copied checkpoint folder without `--gco-from`
  surgery.
- The live run currently has batch 400k, the reset-mix edit and Nexto serving; this test's control arm absorbs those
  because both arms start from the same checkpoint with the same config.

## 11. Record (fill in)

| Item | Control | Treatment |
|---|---|---|
| Fork checkpoint / step | | |
| EV 1v1 / 2v2 / 3v3 (last 100M) | | |
| Pooled EV | | |
| Source-corrected EV per mode | | |
| `Delta` update magnitude (nonzero?) | n/a | |
| Entropy, Rating/1v1, Ref/Oldest Share | | |
| Verdict | | |
