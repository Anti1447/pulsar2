# GCO distributional critic, Phase A (design note and pre-registration, 2026-11-18)

Status: **DESIGN ONLY. Nothing is implemented or run.** Branch `gco-goalcritic`. Doctrine: measurement before machinery; one
lever; an automatic guard where one exists; a revert path. Follows `GCO_CRITIC_CONTEXT_TEST.md` (section 14: opponent source and
the clocks add nothing, H-0 supported), which this note reuses as a template (detached side heads, paired panels, env switch).

## 1. The problem and what is already ruled out

From `GCO_CRITIC_CONTEXT_TEST.md` section 1 (medians over about 2.2B steps): pooled critic EV 0.400 / 0.292 / 0.266 (1v1 / 2v2 /
3v3), gate threshold 0.41. By realised time to the end: 0-2 s 0.86-0.94, 2-5 s 0.45-0.67, 5-10 s 0.10-0.19, **> 10 s about -0.2**,
and the > 10 s rows are 59% / 71% / 76% of the data. Advantage-outcome correlation in the far bin is 0.012.

Ruled out (section 14 there, a measured zero with CI about +-0.0005): opponent source, the no-touch clock and the episode clock
as extra critic inputs. The team-mode EV gap is not a missing-input problem of those kinds.

## 2. Hypothesis, stated honestly

The live critic is an MSE regressor. Its target is close to bimodal (a goal for, a goal against, the -0.5 timeout, or a long
neutral stretch), and MSE returns the mean of that mixture. Two different claims follow, and they need different evidence:

- **H-mean (the optimistic claim):** a categorical head trained with a cross-entropy loss is a better *predictor of the mean*
  than the MSE head on the same features (better-conditioned gradients on a multimodal target, the usual C51 / HL-Gauss result).
  Prediction: mean EV rises in the middle and far bins.
- **H-shape (the modest claim):** the head's mean is no better, but its *distribution* carries information the MSE critic cannot:
  it is calibrated, its spread tracks its own error, and it separates "unpredictable far state" from "predictable near-terminal
  state". That is what a later risk or quantile advantage would need.
- **H-0:** neither. The far rows are unpredictable from the observation and the head just reports a wide, uninformative
  distribution there.

**My prior: H-0 or H-shape is likelier than H-mean.** The head's mean is the same quantity MSE already targets; if the far-row
return is not a function of the observation, no loss function recovers it. The context test already showed that extra inputs do not
help, which points the same way. This note exists to find out, cheaply and without touching the policy.

## 3. The lever (exactly one): measurement-only side heads on the detached trunk

New module family `critic_dist_*`, trained beside the live critic and **never used** in collection value predictions, GAE, the
truncation bootstrap, the critic's own loss, or the EV the policy-LR gate reads.

- **Input:** the DETACHED `critic_trunk` output (1280), the same read the context heads use. No gradient reaches `critic_trunk`,
  `shared_head` or the policy.
- **`critic_dist`:** 2x256 (GELU, LayerNorm) -> K = 51 logits over a fixed support. **HL-Gauss** targets (a Gaussian of width
  0.75 bin spread over the bins, then cross-entropy): smoother than a one-hot C51 projection and needs no Bellman projection step
  because the target here is a scalar.
- **`critic_dist_mse` (control, required):** the identical 2x256 body, one output, MSE, same data, same optimizer and LR. Every
  gain must be read AGAINST it, not against the live critic. This separates "the loss function helps" from "an extra 2x256 head
  on the detached trunk helps" (the context test's `nul` head played the same role).
- **Target:** the same `targetValues` the live critic trains on, in the same units, so the paired comparison is like for like.
- **Support:** fixed, chosen from the measured range of `targetValues` over a recent window (to be read from `metrics.jsonl` /
  a one-off dump before coding; not assumed). Clip targets to the support and log the clipped fraction.
- **Loss:** cross-entropy for `critic_dist`, MSE for the control, each with its own optimizer at `criticLR` (1.5e-4), computed on
  the SAME minibatches as the critic's loss (`PPOLearner.cpp` about 694-707).

Two limits, disclosed up front:

1. `targetValues` are GAE lambda-returns (bootstrapped from the critic's own values), not Monte Carlo returns. A distribution
   trained on them is a distribution of *bootstrapped targets*, which understates the true return spread in far rows. So the
   calibration and spread panels in section 4 are scored against the **realised MC return** that the existing EV block already
   computes (that is also what `GCO/EV TTE` uses), not against the training target.
2. The head learns a target that depends on the live critic, so it can inherit its far-row bias. If the far bin looks the same,
   that is evidence about the target, not only about the head.

## 4. Panels (every iteration unless noted; TTE ones every 10th, like the existing ones)

All scored out of sample (the head's prediction on this iteration's rows is computed before this iteration's update), against the
realised MC return, per mode (1v1 / 2v2 / 3v3) and per time-to-end bin (0-2 / 2-5 / 5-10 / > 10 s, as the existing panels):

| Panel | Meaning |
|---|---|
| `GCO/EVdist Mean`, `GCO/EVdist MSE` | EV of the distributional head's mean, and of the MSE control, on the same rows |
| `GCO/Dist Gain Mean`, `Dist Gain MSE` | `EVdist Mean - EV` (live critic) and `EVdist Mean - EVdist MSE` (the control comparison that counts) |
| `GCO/Dist CRPS`, `Dist CRPS Skill` | CRPS of the predicted distribution vs the realised return, and skill = 1 - CRPS / CRPS(climatology), where climatology is the per-mode, per-bin empirical return distribution of the same window |
| `GCO/Dist Coverage 80` | fraction of realised returns inside the predicted 10-90% interval (target 0.80) |
| `GCO/Dist PIT Dev` | max deviation of the PIT histogram from uniform (calibration) |
| `GCO/Dist Spread-Err Corr` | Spearman between predicted spread (q90 - q10) and the absolute error of the head's own mean |
| `GCO/Dist Mass Modes` | predicted mass in the goal-for, goal-against and timeout neighbourhoods, per bin |
| `GCO/Dist Entropy`, `Dist Clip Frac`, `Dist Update Magnitude` | health: mean predictive entropy, target clipping, and the LR-trap check |

## 5. Pre-registered predictions (median over the last 100M of a 150M window, bootstrap CI over iterations)

| Measure | H-mean true | H-shape true | H-0 true |
|---|---|---|---|
| `Dist Gain MSE`, 5-10 s bin, 2v2 and 3v3 | >= +0.04 | within +-0.02 | within +-0.02 |
| `Dist Gain MSE`, > 10 s bin | >= +0.05 (EV from about -0.2 toward -0.15) | within +-0.02 | within +-0.02 |
| Pooled `Dist Gain MSE`, team modes | >= +0.03 | within +-0.02 | within +-0.02 |
| `Dist CRPS Skill`, > 10 s bin | any | >= +0.05 | < +0.02 (no better than climatology) |
| `Dist Coverage 80` (all bins) | 0.75-0.85 | 0.75-0.85 | outside 0.75-0.85 in the far bin |
| `Dist Spread-Err Corr` | any | >= +0.30 | < +0.15 |

A result counts only if its bootstrap interval excludes 0 (for gains) and it clears the floor. The far-bin sign and the middle
bin are reported first; pooled EV alone is NOT the success metric, because the far rows dominate it and the policy barely learns
from them (same rule as the context test).

**Falsifiers.** A gain only in the 0-2 s bin (the head memorises near-terminal states); a gain only in 1v1; a gain that appears
and decays across the window (overfit); a gain over the live critic that disappears against the MSE control (it was the extra
head, not the loss); a mean EV gain that comes with Coverage far from 0.80 (a sharp, miscalibrated head).

## 6. Guards

- **LR trap (the `vdag1`/`vdag2` lr=0 incident):** `SetLearningRates` must name both new modules; `Dist Update Magnitude` must
  read nonzero after the first iteration, else the window is void.
- **Policy untouched:** inputs are detached and the loss never reaches the trunk. Control panels (Policy Entropy, Policy Update
  Magnitude, clip fraction, `Rating/1v1`, `Ref/Oldest Share`, `GCO/Critic Explained Variance`) must stay in their prior bands;
  record the pre-restart medians the way the context test did.
- **Loader:** `models.Load(..., allowNotExist=true)` resets any missing model silently. After the restart the boot log must list
  "will be reset" for the `critic_dist*` modules ONLY. Any other model in that list is a damaged checkpoint: stop, do not
  hand-assemble (recovery doctrine).
- **Default OFF:** an env switch `GGL_CRITIC_DIST` (the same pattern as `GGL_CRITIC_CTX`), so the rebuilt library is inert until a
  process starts with it.
- Revert on NaN/inf, a 3x persistent critic-loss jump, or entropy below half its baseline. There is no policy gate to trip, since
  the policy never reads the heads.

## 7. Cost

- Code (not written): two `Model`s in `MakeModels`, optimizer LRs in `SetLearningRates`, a loss block beside the critic's in the
  learn pass, a distributional-mean and quantile read in the paired-EV block, the panels, and exclusion from policy versions and
  the exploiter snapshot. Unlike the context test, **no per-row buffer column is needed**: every input already exists. That makes
  this smaller than the context test (243 lines in 5 files).
- Extra GPU memory: about 0.7M parameters per head plus optimizer state, negligible.
- The live trainer must be stopped and restarted on the new binary (a few minutes, at most one save interval, resumes from its
  checkpoint). **That needs the user's approval; nothing has been restarted.**
- **Restart procedure** (from the context test's lessons): `systemd-run --user` does not forward a shell's environment, so use
  `systemctl --user set-environment GGL_CRITIC_DIST=1`, then `tools/run_trainer.sh --stop`, then from `build/`
  `../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco`. **Do not use `tools/trainerctl restart`** (it starts the bare
  `./GigaLearnBot`, the 6.1 lineage), and **do not run `run_trainer.sh` from the repo root** (`./GigaLearnBot` is not found there;
  it crash-loops). Check `Checkpoint Save/Load Dir: "checkpoints_gco_6.1"` in the boot log first.

## 8. Known unknowns (not verified)

- The numeric range of `targetValues` (and so the support); read it before coding, do not assume +-1.
- Whether the realised MC return per row is exposed where the paired-EV block runs, with the same truncation handling the existing
  EV panels use (a truncated episode has no terminal return; the existing panels' convention must be matched, not re-invented).
- Whether the HL-Gauss width of 0.75 bins is right for this range; it only changes the smoothness of the target.
- A CRPS climatology baseline per mode and bin needs a rolling window of realised returns; the window length is a choice, not a
  measurement.
- The Phase A heads see a detached 1280-wide trunk read, like the context heads; a distributional head trained *through* the trunk
  could behave differently (that is Phase B, and it changes the policy).

## 9. Decision rule

- **H-mean supported** (floors met against the MSE control): Phase B, pre-registered separately: train the live critic's loss with
  the categorical objective (a restart-level change to the value baseline), so the critic-aliasing rule applies: the critic must
  be given time to learn the new return structure, and `GCO/Critic Explained Variance` is judged after that, not during.
- **H-shape supported** (no mean gain, but CRPS skill, coverage and spread-error correlation clear): keep the head as a
  measurement instrument; Phase B is a **risk-quantile advantage** experiment (an advantage from the 0.7 or 0.3 quantile
  instead of the mean), forked two-arm, outcomes against the frozen bots, with the clipping-ratchet lesson in force.
- **H-0 supported:** drop the distributional critic. Next candidates, as already listed in `GCO_CRITIC_CONTEXT_TEST.md` section 14:
  event-probability (hazard) heads evaluated offline, the team `Delta` encoder, a fixed-window truncation test.

## 10. Record (fill in)

| Item | Value |
|---|---|
| Binary / commit, restart step | |
| `targetValues` range and support used | |
| Boot log "will be reset" list | |
| Update magnitudes (`dist`, `dist_mse`) nonzero? | |
| Paired gain vs live critic and vs MSE control, per mode | |
| Gain by time-to-end bin, per mode | |
| CRPS skill, coverage, PIT deviation, spread-error correlation | |
| Control panels vs prior bands | |
| Verdict | |
