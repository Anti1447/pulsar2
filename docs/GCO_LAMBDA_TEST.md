# GCO GAE-lambda test (pre-registration, 2026-11-17)

Status: **PROPOSED, nothing enabled.** Branch `gco-goalcritic`. One lever: `cfg.ppo.gaeLambda` 0.95 -> 0.99 under `--gco`.
Follows the lambda-sweep panels (`Learner.cpp`, commit `1c1ebaf`).

## 1. The problem (measured)

`gaeLambda` is never set in `ExampleMain.cpp`, so it runs at the library default 0.95, and the goal critic uses the same
value (`Learner.cpp` ~3609). With gamma 0.9969, gamma*lambda ~ 0.947: the advantage reaches ~19 steps (~1.3 s). On rows far
from a goal the reward is zero until the end, so the advantage is ~ V(s') - V(s), the critic's own noise, and EV beyond 10 s
is negative (about -0.2 to -0.3 in every mode). 54% (1v1) to 69% (3v3) of rows are beyond 10 s.

Lambda-sweep panel, far rows (>10 s from the episode end), medians over 8 history lines, current build, same buffer and V_real:

| lambda | corr(A, outcome) | std(A) | corr(A, MC advantage) |
|---|---|---|---|
| 0.90 | 0.01 | 0.12 | 0.21 |
| 0.95 (live) | 0.026 | 0.167 | 0.29 |
| 0.99 | 0.28 | 0.275 | 0.71 |
| 1.00 (MC) | 0.78 | 0.69 | 1.0 |

At 0.95, far-row advantages carry almost no information about the outcome. At 0.99 they carry some at 1.7x the std; MC
carries it fully at 4.1x the std. Corr(A, outcome) is not a quality score (MC correlates by construction, luck included).

## 2. Hypotheses

**H-L (helps).** lambda 0.99 gives far rows an outcome-linked advantage, the policy gradient on the majority of rows gets
informative, and learning against fixed opponents speeds up. It also trains V toward targets closer to the MC return, so
the far-bin EV (scored against the MC return) rises.

**H-0 (null).** The extra outcome signal is outweighed by its variance, or the policy-LR gate (policy LR 2.85e-5) is too low
for either to show in 300M steps.

**H-H (hurts).** The advantage filter (`advFilterMode = MAGNITUDE`, top 50% by |A|) switches to far rows once their |A|
grows, 3v3 advantages get noisier, and play degrades.

## 3. The lever and its side effects (exactly one config line)

`cfg.ppo.gaeLambda = 0.99f;` in `ConfigureGCO`. Everything else unchanged. Known side effects of that single value:
1. It changes V_real's targets (V + A_lambda) AND the goal critic's advantages and targets (both use `gaeLambda`).
2. It changes which rows pass the magnitude filter (section 2, H-H).
3. The sweep panels stay valid in both arms (they recompute fixed lambdas on the buffer).

Revert: delete the line (back to the 0.95 default), rebuild, restart from the control arm's checkpoint.

## 4. Baseline noise (measured, so the effect can be judged)

Frozen external bots, goals-for share per 100M-step window over 800M steps (`<Bot>/Goals For` / (For + Against)):

| bot | range | SD of a 100M window |
|---|---|---|
| Nexto | 0.50-0.57 | ~0.025 |
| Dice | 0.25-0.29 | ~0.015 |
| GalactusV2 | 0.20-0.24 | ~0.016 |
| ChronosV2 | 0.22-0.25 | ~0.010 |

Difference of two sequential 300M-step arms, 80% power: **MDE ~2.5-3 points** for the mean share over Dice, Galactus and
Chronos (about 4-7 points for any single bot). The slow trend over 800M steps (Dice 0.27 -> 0.29, Nexto 0.50 -> 0.54) is why
a control arm from the SAME checkpoint is needed.

## 5. Design

Two arms forked from the SAME checkpoint, 300M steps each (~1 hour each at ~80k steps/s), same fleet (`--gco` alone), same
code except the one line. Both arms leave the policy-LR gate as is (it has not stepped for 3,000+ iterations; if treatment EV
crosses 0.41 and the gate steps, record the iteration and read outcomes only before it).

Fork procedure (uses the recovery doctrine's full-checkpoint restore, no hybrids): stop the trainer (saves a checkpoint);
`cp -a build/checkpoints_gco_6.1 <fork dir>` (check free disk first); run the control arm; stop; move that folder aside; `cp -a`
the fork back into place; run the treatment arm. Both arms continue the same wandb run, so tag each arm's steps by
`GCO/LR Step` and the step ranges in the record.

## 6. Pre-registered predictions (treatment minus control, last 150M steps of each arm)

| Measure | H-L | H-0 | H-H |
|---|---|---|---|
| Mean goals-for share vs Dice, Galactus, Chronos | **>= +2.5 pts** | within +-1.5 | **<= -2.5** |
| Far-bin EV (>10 s), pooled over modes | rises by >= 0.10 (from ~-0.22) | within +-0.05 | falls |
| Near-bin EV (0-2 s, 2-5 s) | within +-0.03 | within +-0.03 | falls > 0.05 |
| Pooled EV per mode | up | ~flat | down |
| `Ref/Oldest Share` | within the noise band | same | down > 0.01 |
| Policy entropy | within +-15% of control | same | below half of control |
| `SB3 Clip Fraction` and `Mean KL Divergence` | within 1.5x of control | same | > 2x |

Mechanism check (must hold for the lever to have acted at all): far-bin EV moves in the predicted direction OR the clip
fraction / KL shifts. If neither moves, record it as "no effect on the learner" and stop.

Health: no NaN/inf; value loss and `GoalCritic/Loss` not rising persistently; Rating/2v2 and Rating/3v3 not more than 50
below control for 100M steps.

## 7. Guards

Manual checks every 50M steps (no automatic update-damage guard). Revert on NaN/inf, entropy below half of control, or the
H-H signature (3v3 outcomes down AND KL > 2x control for 50M steps). Offline kickoff test on each arm's final checkpoint.

## 8. Decision rule

- **H-L supported:** keep 0.99; the next test is lambda 0.97 / 0.995 (the sweep panels can predict the better one first),
  then the std-matched long-lambda blend (a second advantage channel) if the single value cannot serve both near and far rows.
- **Inconclusive** (the share effect under the ~2.5-point MDE and EV flat): a null at this power. Do not infer "no effect";
  either extend to ~900M steps per arm or move to the critic-conditioning lever, whichever is cheaper to read.
- **H-H supported:** revert; record that far-row variance outweighs the signal at the current critic quality.

## 9. Order relative to the other tests

One lever at a time. This test needs no code (one config line) so it can run first. The team-critic `Delta` test and the
reset-mix test are separate; do not overlap their windows with this one. If the critic is later improved by conditioning,
re-run the sweep: the best lambda shifts toward smaller values as V improves.

## 10. Not verified

- Whether `advStd` (policy advantage scaling) is renormalized per batch, which sets how the larger lambda-0.99 advantages
  reach the policy. I did not read that part of `Learner.cpp`.
- The step ranges and wandb-run overlap of the two arms, and disk space for the fork (see the `du` / `df` check in the log).
- The sweep numbers come from 8 history lines; they look stable but are small.

## 11. Record (fill in)

| Item | Control | Treatment |
|---|---|---|
| Fork checkpoint / step range | 61,390,223,087 -> 61,690,667,351 (control) | 61,390,223,087 -> 61,692,730,053 (lambda 0.99) |
| Goals-for share Dice / Galactus / Chronos / mean (last 150M) | 0.288 / 0.223 / 0.257 / 0.256 | 0.240 / 0.206 / 0.218 / 0.221 |
| Far-bin EV 1v1 / 2v2 / 3v3 | -0.276 / -0.240 / -0.167 | -0.273 / -0.256 / -0.206 |
| Near-bin EV 0-2 s, 2-5 s (1v1 / 3v3) | 0.938 / 0.861 ; 0.678 / 0.433 | 0.942 / 0.873 ; 0.689 / 0.503 |
| Pooled EV per mode 1v1 / 2v2 / 3v3 | 0.387 / 0.287 / 0.268 | 0.389 / 0.298 / 0.259 |
| Entropy, clip fraction, KL | 0.556, 0.0144, 0.00173 | 0.539, 0.0142, 0.00172 |
| Ref/Oldest Share, Rating 1v1 / 2v2 / 3v3 | 0.9868; 2608 / 1027 / 1479 | 0.9868; 2575 / 999 / 1463 |
| Verdict | see section 12 | H-H signature on outcomes; see section 12 |

## 12. Result (2026-11-17, from `metrics.jsonl` of each arm, medians over the last 150M of each 300M-step arm)

| Measure | control | lambda 0.99 | diff |
|---|---|---|---|
| Goals-for share vs Dice | 0.288 | 0.240 | -4.8 pts |
| vs GalactusV2 | 0.223 | 0.206 | -1.7 |
| vs ChronosV2 | 0.257 | 0.218 | -3.9 |
| **mean of the three (primary)** | **0.256** | **0.221** | **-3.5 pts** |
| vs Nexto (not in the primary) | 0.516 | 0.486 | -3.0 |
| Far-bin EV (>10 s), 1v1 / 2v2 / 3v3 | -0.28 / -0.24 / -0.17 | -0.27 / -0.26 / -0.21 | flat |
| Pooled EV per mode | 0.387 / 0.287 / 0.268 | 0.389 / 0.298 / 0.259 | flat |
| Policy entropy | 0.556 | 0.539 | -3% |
| `SB3 Clip Fraction`, `Mean KL Divergence` | 0.0144, 0.00173 | 0.0142, 0.00172 | unchanged |
| `Rating` 1v1 / 2v2 / 3v3 (n = 5-6, inflated) | 2608 / 1027 / 1479 | 2575 / 999 / 1463 | lower, inside the +-50 band |
| `Ref/Oldest Share` | 0.9868 | 0.9868 | same |

**The lever reached the learner**, though not through the pre-registered mechanism check (far-bin EV and clip/KL did not
move, so by that check alone this would have read "no effect on the learner" - wrong):

| Panel | control | lambda 0.99 |
|---|---|---|
| `AdvFilter/Kept Abs Adv Mean` | 0.286 | 0.674 (2.4x) |
| `AdvFilter/Threshold` | 0.082 | 0.252 (3.1x) |
| `GoalCritic/Loss` | 0.0011 | 0.0053 (5x) |
| `GoalCritic/Adv-Outcome Corr` | 0.28 | 0.56 |
| `GoalCritic/Injected Abs Mean` | 0.033 | 0.080 |

Reading: lambda 0.99 makes the policy's advantages ~2.4x larger and much more outcome-linked, but `Policy Update Magnitude` is
unchanged (0.1014 in both arms; a normalised step), so the larger, noisier advantages move the policy the same distance in a
direction that is noisier. The critic did not improve (far-bin EV flat). The outcome moved the wrong way on all four bots.

**Verdict under section 8: H-H (the outcome row), not H-L.** The primary effect is -3.5 points against a ~2.5-3 point MDE,
so it clears the threshold, but this is ONE pair of sequential arms; the sign is the same on all four bots (a weak sign test),
and Dice and Chronos are below the whole 800M-step baseline range in section 4. The KL > 2x guard did not trip, so the harm
is on outcomes, not on update stability. Confidence: moderate that 0.99 does not help here; lower that it hurts by exactly 3.5.

Decision: **revert to 0.95** (done, `ExampleMain.cpp`); the live checkpoint folder is the control arm's (`checkpoints_gco_6.1`
at 61.69B); the treatment arm's is kept as `checkpoints_gco_6.1_ARM_lambda099` and the fork as
`checkpoints_gco_6.1_FORK_lambdatest` (both can be deleted once this is settled).

Next, per the doc's own rule: not a larger lambda. Candidates: a smaller step in the same direction (0.97) is unlikely to
help given 0.99 hurt; the std-matched long-lambda blend (a second advantage channel scaled to the primary's std) is the
variant that avoids the 2.4x advantage inflation; and the critic-conditioning lever, since far-bin EV did not move at all.
