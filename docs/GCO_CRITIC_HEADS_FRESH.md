# GCO critic heads from a fresh start: event-probability heads and an uncertainty critic (test plan, 2026-11-20)

Status: **PLAN ONLY. Nothing is implemented or run.** Branch `gco-goalcritic`. Doctrine: measurement before machinery; one lever; an automatic guard; a revert path.
Nothing here actuates: every head is **detached** (reads the critic trunk with no gradient back), and nothing feeds collection, GAE, the policy, or any potential. The policy
should therefore be unchanged in expectation; the run is a fresh GCO run with measurement heads attached, compared against the same config without them.

## 1. Why these two (see the scoping discussion)

- **E: event-probability heads.** The live MSE critic returns one blended number for a near-trinomial return (goal for, goal against, the -0.5 timeout, long neutral stretches).
  Heads that predict WHICH event, scored against what actually happened, say where the critic is blind. A null is informative.
- **U: uncertainty critic with a disagreement correction.** A head that predicts where V is unreliable. Raw error is mostly aleatoric noise (the far rows, 60-75% of data),
  the noisy-TV trap, so the epistemic part is isolated by an independent second critic. This is the only candidate that points at exploration (the optimism program).
- Not in this run: the distributional head (design in `GCO_DISTRIBUTIONAL_CRITIC.md`, prior H-0), team critics (the fresh run is 1v1), any actuator.
  Context: opponent-source and clock inputs were a measured null (`GCO_CRITIC_CONTEXT_TEST.md`); the policy's gains so far came from the reset mix, not the critic.

## 2. Configuration

Fresh GCO identical to fresh3 (twins OFF, 1v1, pure self-play, mix NearGoal 0.30 / far NearGoal 0.20 / 0.10 each BallNearCar, AirDrill, AirPlay, Kickoff, Random, entropy 0.035,
policy LR 1.5e-4, batch 400k, miniBatch 25k, 2 epochs, 1024 games), plus the heads below. Launch name `--gco-fresh4`, folder `checkpoints_gco_fresh4`, metrics run `gco-fresh4`.
Default OFF behind an env switch (`GGL_CRITIC_HEADS`) like the context test, so the rebuilt library is inert until a process starts with it.
Run to **5.0B steps**, then stop (a clean save).

### 2a. Event heads (E)
- Input: detached `critic_trunk` output. Body 2x256 (GELU, LayerNorm) -> logits.
- Labels (from whole-episode rows, already row-contiguous: the outcome is read at the episode end and the time to end counted back): for a horizon H, class in
  {we score, we concede, timeout, none-within-H}. Horizons 5 s, 15 s and the full episode, three softmax heads. Cross-entropy, own optimizer at criticLR.
- Extra measurement label (never a reward): `touch within 5 s`, a binary head. It exists before the wall breaks, so it shows whether the critic can see the
  success coming earlier than the reward does.
- Control (required): the identical body with a **shuffled-label** or **constant-prior** output, so "the head learned something" is separated from "an extra head on the trunk".

### 2b. Uncertainty heads (U)
- `V_b`: a second value head, same body as the live critic's head but an independent init, trained on the same targets, detached trunk. Epistemic proxy
  `D = (V - V_b)^2`.
- `U_err`: predicts the squared error `e = (target - V)^2` of the live critic (the raw, aleatoric-contaminated version).
- Panels read the two against each other. No actuation.

### 2c. Guards
- LR trap (the `vdag1`/`vdag2` lr=0 incident): every new module named in `SetLearningRates`; `Heads Update Magnitude` must be nonzero after the first iteration, else the window is void.
- Loader: boot log "will be reset" must list the new heads ONLY (any other model in that list is a damaged checkpoint: stop; recovery doctrine).
- Policy untouched: the control panels below stay inside fresh3's / run 1's bands; any excursion voids the "measurement-only" claim.
- Revert on NaN/inf, a persistent 3x critic-loss jump, or entropy below half its baseline.

## 3. Panels (every 10th iteration, scored out of sample: predict before the update)

Per time-to-end bin (0-2, 2-5, 5-10, > 10 s) and pooled:
| Panel | Meaning |
|---|---|
| `Heads/E LogLoss`, `E Skill` | log-loss of each event head and skill = 1 - logloss / logloss(climatology of the same window) |
| `Heads/E AUC Score15`, `AUC Concede15` | AUC of P(score within 15 s) and P(concede within 15 s) against the realised outcome |
| `Heads/E ECE` | expected calibration error |
| `Heads/E Touch5 AUC`, `Touch5 Brier` | the touch-within-5-s head (the wall-break view) |
| `Heads/E Ctrl Skill` | the control head's skill (must be about 0) |
| `Heads/U D Mean`, `U Err Mean` | mean epistemic proxy and mean predicted error |
| `Heads/U Corr D Err`, `U Corr Pred Err` | correlation of D (and of the error head's prediction) with the realised squared error, by bin |
| `Heads/U Decile Ratio D`, `U Decile Ratio Pred` | mean realised error of the live critic in the top decile of D (or of the predicted error) over the bottom decile (1 = the score does not rank error), by bin |
| `Heads/U Pred Err Mean`, `U Err Mean`, `U D Mean` | the predicted error, the realised error and the mean epistemic proxy, by bin |
| `Heads/Update Magnitude ev/ctl/vb/uerr`, `Heads/Loss` | the LR-trap check and health |

## 4. Pre-registered predictions

Read at 1B, 2.5B and 5.0B steps (median over the last 100M, bootstrap CI over iterations). A result counts only if its CI excludes the null.
- **E has information:** `AUC Score15` > 0.70 and `E Skill` > +0.05 in the 2-10 s bins after the wall breaks; the control's skill within +-0.01 of 0.
- **E sees success before the reward:** `Touch5 AUC` > 0.70 BEFORE the wall breaks (first 500M), and it rises as the touch ratio rises.
- **U separates epistemic from aleatoric:** `U Decile Ratio D` is above `U Decile Ratio Pred` in the > 10 s bin (D ranks the live critic's error better than the raw error head,
  which is dominated by outcome noise) and `U Corr D Err` > 0.1 there; if the raw error head ranks the far-bin error as well as D, the disagreement correction added nothing.
  (Implementation note: the "progress over the next 100M" form of this test needs per-state tracking across iterations and was replaced by the same-iteration decile ranking.)
- **Falsifiers:** an E skill that only appears in the 0-2 s bin (it memorises near-terminal states); a gain that appears and decays across the window; AUC without calibration
  (ECE > 0.05); a decile ratio of D at or below 1 in the far bins.
- **Policy guard (the run must not move):** wall-break window inside fresh1-3's spread (580-860M start of the window), ball-touch ratio at 1B within 0.0085-0.011,
  policy entropy within 30% of fresh3's band, and the 5B read inside run 1's band for the gauntlet. A breach is reported as "the heads were not measurement-only".

## 5. Control and comparison

- **Control run:** resume `checkpoints_gco_fresh3` (`--gco-fresh3`, final 1,001,613,312, the same config without heads) to 5.0B. Paired by config and by lineage to 1B.
  About 4B steps, about 20 h. Run it only if the policy guard in section 4 is borderline at 1B or 2.5B, or if the heads show a result worth a clean comparison;
  otherwise the guard against fresh1-3 (three wall-break windows) and run 1's 5B curve is the comparison.
- Comparison for policy effect: gauntlet mean win (500M windows, noise SD about 0.45), and the offline head-to-head tool (`GCO_H2H_EVAL.md`) between the 5B checkpoints
  of fresh4 and the control, which is far more sensitive than the gauntlet.
- Single seed on both sides: a policy difference below the head-to-head noise floor (ctrl vs ctrl2 of the fork arms) is not a result.

## 6. Cost and schedule

- Code: the heads, labels from row-contiguous episodes, loss blocks beside the critic's, panels, exclusion from policy versions and snapshots. No per-row buffer needed.
  Throughput cost expected small (no twins): measured by the first 100M steps against fresh3's rate.
- Run: 5B steps at about 57k steps/s is about 24 h of GPU. Health reads every 100M steps (the same script pattern as the fork chain).
- Needs a rebuild and a fresh launch; the trainer is occupied by the fork stack until it finishes (see the stack in the session record).

## 7. Revert
`--gco-fresh4` is its own folder; stop the process, delete nothing (quarantine). The env switch off restores the previous binary's behavior exactly.

## 8. Implementation record (2026-11-20)

Built behind `GGL_CRITIC_HEADS=1` (set automatically by `--gco-fresh4`): `PPOLearner.cpp` (four heads on the detached critic-trunk read, losses beside the critic's, update-magnitude
panels, LR named in `SetLearningRates`, excluded from policy versions), `ExperienceBuffer.h` (`headLabels`), `Learner.cpp` (per-row touch flag at collection, labels from whole-episode rows,
panels every 10th iteration), `Util/CriticHeadsPanels.h` (the panel math). CPU smoke (`GGL_SMOKE=1`, 4 iterations): all panels print; all four heads' update magnitudes nonzero
(0.09-0.11); first-iteration log-loss exactly ln 4 (zero-initialised heads). The smoke folders are quarantined as `build/checkpoints_gco_fresh4_SMOKE*_20261120`. The AUC handles ties by average rank
(a first version gave a spurious 0.70 on constant scores; fixed before the real run).
Launch: `tools/gco_health/chain_fresh4.sh` (after the ctrl2 arm logs "CTRL2 ALL DONE"); reads: `tools/gco_health/fresh4_read.py`.
