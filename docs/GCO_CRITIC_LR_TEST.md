# GCO critic-LR test (pre-registration, 2026-11-16)

Status: **PROPOSED, not enabled.** Nothing in `src/ExampleMain.cpp` is changed by this document.
Branch: `gco-goalcritic`. Doctrine: measurement before machinery, one lever.

## 1. The problem (what the measurement says)

GCO's policy-LR gate steps up only when the median explained variance (EV) since the previous check exceeds
`cfg.exploiter.explainedVarThreshold`. That threshold was lowered 0.5 -> 0.41 because 178 checks (to 12.43B) never
stepped: window medians sat at **0.35-0.38** (comment above `ConfigureGCO`, `src/ExampleMain.cpp`). So the critic,
not the policy LR, is what is holding the policy at its reduced learning rate. Reward is goal-only (+-1, -0.5 on
a no-touch timeout), which is sparse, so value targets are noisy and the critic is the slow component.

Current critic settings (read from `ExampleMain.cpp`): `criticLR = 1.5e-4` (equal to `policyLR`), shared
`criticTrunk` of 3x1280, `gaeGamma = 0.9969`, long-horizon goal critic on (gamma 0.9994, beta 0.25, lr 1.5e-4).
The `PPOLearnerConfig` default `criticLR` is 3e-4, so the run is at half the library default.

## 2. The lever (exactly one)

`cfg.ppo.criticLR`: **1.5e-4 -> 3e-4** (x2), applied in the GCO path only (`--gco`), main lineage untouched.
Nothing else changes: `policyLR`, `goalCritic.lr`, entropy, batch sizes, reset mix, rewards, terminals, opponents.

Revert: set `criticLR` back to `1.5e-4`, rebuild, restart. Pure config; no checkpoint surgery (optimizer state
loads as-is, only the LR scalar differs). Verified 2026-11-16: `SetLearningRates` (PPOLearner.cpp:1113) sets `critic`, `critic_trunk` and the goal heads from `criticLR`, and Learner.cpp:3277 reapplies `config.ppo.criticLR` at every gate step. EV panel name: `GCO/Critic Explained Variance`. (Original note: verify that `SetLearningRates` actually applies
`criticLR` to the live optimizers (same trap as the `vdag1`/`vdag2` lr=0 incident in CLAUDE.md): read the
"Update Magnitude"-style panel for the critic family after the first iteration and confirm it is nonzero and larger.

## 3. Baseline (must be captured BEFORE the change)

From wandb for the current GCO run, over the last 200M steps, per mode (1v1 / 2v2 / 3v3 where logged):
- median and IQR of critic EV (the panel the gate reads; confirm its exact name in the metrics sender)
- value loss, goal-critic EV, policy entropy, `Plasticity/Policy Dead Units`
- `Rating/1v1`, `RatingWatch/*`, `Ref/Oldest Share`
- policy-LR gate: current scale and number of steps taken

Write these numbers into section 7 before restart. Without them the result below cannot be read.

## 4. Pre-registered predictions and criteria

Run window: **300M steps** from restart (>= 12 gate checks at the 25M interval).

| Metric | Healthy / success | Failure |
|---|---|---|
| Critic EV, median over the last 100M of the window | >= 0.41 (the gate threshold), and above baseline median by >= 0.04 | no gain over baseline (+-0.02): critic LR was not the bottleneck |
| Value loss | falls, no sustained spikes | oscillation or a rise over 3+ consecutive checks |
| Policy-LR gate | steps up at least once | zero steps (confirms EV did not clear the bar) |
| Policy entropy | within +-15% of baseline slope | collapses faster than baseline |
| `Rating/1v1` and `Ref/Oldest Share` | no drop beyond the mature noise band (+-30-50) | sustained drop > 50 for 100M steps |
| Kickoff test (offline, 2-minute) | >= 9/10 touches | < 8/10 |

**Confound, stated up front:** if EV clears 0.41 the gate will raise the policy LR, which changes policy
behavior too. That is the expected downstream effect, not a second lever, but it means Rating movement after the
first gate step is attributed to "critic LR, via the gate", not to critic LR alone. To separate them, read EV
and value loss BEFORE the first gate step; read Rating only for the full window.

## 5. Guards (manual; there is no automatic update-damage guard in this run)

- Check wandb every ~50M steps. Revert immediately if value loss diverges (any NaN/inf, or a 3x jump that
  persists two checks) or entropy falls below half its baseline.
- Before the run, confirm a golden-archive checkpoint exists (`best_r*`, `best2v2_r*`, `best3v3_r*`) so a revert
  restores a KNOWN-GOOD FULL checkpoint (recovery doctrine: no hybrids).
- At the end of the window run the offline kickoff test on the newest checkpoint and the anchor battery
  (`research/tools/anchor_battery.py`, not `Rating/1v1`).

## 6. Decision rule

- **Success** (EV criterion met, no failure row tripped): keep 3e-4, record in `docs/GCO.md` and the change log.
  Next lever (separate test): the second-ranked item below.
- **Neutral** (EV flat, nothing broken): revert to 1.5e-4. Conclusion: critic LR is not the bottleneck; the
  next candidate is critic capacity/targets, or batch size (noisy sparse targets), not more LR.
- **Failure** (any failure row): revert, restore from the golden archive if Rating dropped, write up why.

Candidate next levers, NOT part of this test: batch size above 200k rows (noisier advantages from sparse goals
is the other likely EV limiter; bounded by the 16GB GPU), the +-1 reward vs. return-normalization scale,
the -0.5 timeout penalty, and policy entropy 0.014.

## 7. Record (fill in)

| Item | Value |
|---|---|
| Baseline EV median / IQR | 0.298 / 0.263-0.347 (GCO/Critic Explained Variance, 3404 iterations of train-20261116-151425.log; lower than the 0.35-0.38 quoted in section 1) |
| Baseline Rating/1v1, Ref/Oldest Share | |
| Restart step | (fill at launch; last logged step ~58.07B) |
| Window end step | |
| Result EV median (last 100M) | |
| Gate steps taken | |
| Verdict | |

Change-log line to add on enabling (`docs/REWARD_CHANGE_LOG.md`):
`2026-11-16 | GCO config (not a reward): criticLR 1.5e-4 -> 3e-4 under --gco only | EV stuck 0.35-0.38 holds the policy-LR gate; critic is the bottleneck | see docs/GCO_CRITIC_LR_TEST.md`
