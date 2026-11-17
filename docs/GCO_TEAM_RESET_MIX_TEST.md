# GCO team-arena reset-mix test (pre-registration, 2026-11-17)

Status: **PROPOSED, nothing enabled.** Branch `gco-goalcritic`. One lever, measured against a control arm forked from the
same checkpoint. Builds on `docs/GCO_RESET_MIX.md` (the setter diagnostics) and `docs/GCO_TEAM_CRITIC_TEST.md` section 12.
Written because the critic gap is largest where rows are far from a goal, and team modes have the fewest goals per row.

## 1. The problem (measured)

`GCO/EV` by time-to-episode-end (screenshots from wandb, smoothed lines read by eye, approximate; ~30 points each).
The panel names say "<=2s / <=5s / <=10s" but the bins are DISJOINT (0-2, 2-5, 5-10, >10 s):

| bin | 1v1 | 2v2 | 3v3 |
|---|---|---|---|
| 0-2 s | ~0.94 | ~0.88 | ~0.85 |
| 2-5 s | ~0.69 | ~0.52 | ~0.47 |
| 5-10 s | ~0.23 | ~0.15 | ~0.12 |
| >10 s | ~-0.25 | ~-0.25 | ~-0.3 |

(The >10 s lines also show one deep dip near step 37.82k in all three modes, to about -0.5 / -0.7 smoothed, unexplained.)

From `docs/GCO_RESET_MIX.md`: episodes last ~30 s (1v1), ~55 s (2v2), ~65 s (3v3); goals come every 35 / 74 / 99 s, i.e.
about 1.0k / 4.4k / 8.9k rows per goal (rows = 2 x teamSize per step at 15 Hz). Only 14-46% of resets have a goal within
20 s. Rows scale with players, so 3v3 is ~50% of the buffer with ~1/9 of 1v1's goal density.

Reading:
1. EV is high only near the end of an episode and is **negative beyond 10 s**: for far rows the critic's output varies more
   than the returns do. Most rows are far rows.
2. At every bin the order is 1v1 > 2v2 > 3v3, so the team-mode gap is not horizon composition alone
   (`GCO_TEAM_CRITIC_TEST.md` H1 stays alive).
3. **Caveat on the bins.** They are defined by the episode's REALISED end, so they condition on the outcome (a row 1 s from
   the end is one where a goal is about to land). They are a diagnostic, not a partition the critic can identify at run
   time, so a high near-end EV is partly built in.

## 2. Hypotheses

**H-A (events per row).** Team arenas give too few goal events per row and too large a share of far rows. Shifting the
team-arena reset mix toward decided starts (the air-carry setter, the most decided cell: goal within 20 s in 39-41% of
team resets, mean |V(s0)| 0.29-0.39, critic EV 0.26-0.31 vs 0.19-0.24 elsewhere) raises goals per 10k rows and the share
of near-end rows, which is signal per step.

**H-B (dilution, the null from `GCO_RESET_MIX.md` section 0.2).** A start is forgotten within seconds. Even carry
episodes last 45-51 s in team modes (vs 56-70 s elsewhere), so a reset-mix shift moves the row distribution by a few
percent at most and nothing is measurable.

Honest power estimate (from those numbers): moving ~0.25 of team resets onto carry shortens the mean team episode by
about 0.25 x 12 s = ~3 s of ~60 s, i.e. **goals per 10k rows +5% to +7%**. That is near the noise floor of a single
200M-step window. The test is therefore built to say so: Phase 0 measures that noise first, and the decision rule has a
"too weak to read" outcome that points at the episode boundary, not at more reset weight.

## 3. Phase 0: baseline with the new panels (no training change)

New panels (`Learner.cpp`, report only; stdout lists Goals/10k, row fractions are wandb-only):
- `GCO/Goals/10k Rows 1v1|2v2|3v3` every iteration (goal terminals |reward| > 0.75, divided by 2 x teamSize).
- `GCO/Row Frac TTE <bin> <mode>` every 10th iteration: share of a mode's rows in each time-to-end bin.
- (Already live: `GCO/Games <mode>`, `GCO/Truncated Frac`, `GCO/EV SrcCorr *`, `GCO/EV TTE *`, `GCO/EV Cell *`.)

Run >= 100M steps on the current build (including the reset-mix P1/P2/P3 edits and 400k rows per iteration) and record:
median and iteration-to-iteration SD of Goals/10k and of each Row Frac, per mode. **Minimum detectable effect = the SD of the
200M-step mean difference estimated from this noise.** If +5% is below it, go straight to section 9 (boundary lever) and do
not run this test.

## 4. The lever (exactly one)

In **2v2 and 3v3 arenas only** (`MakeEnv` with `playersPerTeam > 1`; 1v1 untouched), swap weights with EXISTING setters,
no new setter code. Control = the live team mix (after P1: BallNearCar 0.20, AirDrill 0.20, AirPlay 0.20, Kickoff 0.20,
Random 0.20). Treatment:

| setter | control | treatment |
|---|---|---|
| BallNearCar | 0.20 | 0.10 |
| AirDrill | 0.20 | 0.15 |
| AirPlay (existing, wall/flip/carry) | 0.20 | 0.20 |
| AirPlay, carry only (a second instance, resetFrac 0, wallFrac 0 - verify the constructor semantics) | 0 | 0.25 |
| Kickoff | 0.20 | 0.20 (kept, P1 is in flight) |
| Random | 0.20 | 0.10 |

Of team resets, carry goes from ~7% to ~32%. Donors are the lowest goal-within-20 s cells (BallNearCar 19-20%).
Revert: delete the team-arena branch (one `if` in `MakeEnv`), rebuild, restart from the control arm's checkpoint.

Prerequisite (the other session's open item): run `ResetDiag` on carry after P2 and confirm blue-closer 46-54% and carry
goals within 46-54% blue. A 0.25 weight on a still-lopsided setter would add a colour bias to every team game.

## 5. Design

Two arms forked from the SAME checkpoint, run sequentially on the same fleet (`--gco` alone), same code except the
team-arena branch. Both arms freeze the policy-LR gate (`cfg.exploiter.explainedVarThreshold` set above reach) so the
policy LR is identical and a reset-mix effect on EV cannot move the gate (Goodhart guard: shortening horizons raises
pooled EV by construction). Window 200M steps each; read the last 100M.

## 6. Pre-registered predictions (treatment minus control, team modes, last 100M)

| Measure | H-A | H-B |
|---|---|---|
| Goals/10k Rows | +5% to +10% (>= the Phase 0 MDE) | within +-3% |
| Row Frac 0-10 s (sum of bins) | +2 to +4 points | within +-1 point |
| Pooled EV per team mode | up (expected by construction, NOT a success criterion) | ~flat |
| Matched-bin EV (each bin, each team mode) | not down by more than 0.02 | not down by more than 0.02 |
| `Rating/2v2`, `Rating/3v3`, `Ref/Oldest Share` | within the +-50 noise band | within the band |
| 2v2/3v3 gauntlet results vs external bots | not below the control mean | same |

Success needs Goals/10k and Row Frac to move together AND matched-bin EV not to fall (EV rising only because rows got
easier does not count). Failure modes: aerial overcommit (policy entropy falling, Rating/3v3 down), a colour bias from
carry, matched-bin EV down.

## 7. Guards, checks before the run

- Manual checks every 50M steps (no automatic update-damage guard exists): revert on NaN/inf, entropy below half of the
  control's, Rating/2v2 or 3v3 more than 50 below control for 100M steps.
- Kickoff behaviour is unchanged by construction (Kickoff weight kept); the offline kickoff test on the final checkpoint.
- Confirm in the first iterations that the team arenas really draw the new mix (a short `ResetDiag` run, or the
  `GCO/Row Frac` panels shifting).

## 8. Decision rule

- **H-A supported:** keep; next test the staged expansion in section 10.
- **Too weak to read** (inside the Phase 0 MDE): reset weight is not the lever; go to section 9.
- **H-B supported with a clean null:** same as above.
- **Harm** (guard trips): revert.

## 9. If the reset mix is too weak: the episode boundary (NOT part of this test)

The dilution is structural: play after the start is ordinary, so only a shorter episode changes the row distribution.
That is an episode-boundary change (for example, ending the episode once a possession or attack resolves), the class
that caused two critic-aliasing collapses (CLAUDE.md, `research/reports/STEERED_PRACTICE.md`) and needs its own
pre-registration with the critic-aliasing rule.

## 10. Staged expansion (your idea, Phase 2, only if Phase 1 moves Goals/10k)

Start decided-heavy and widen toward the match mix as the critic catches up. Gate it on a matched-bin EV the critic can
earn, not on pooled EV: for example advance a stage when the 5-10 s team-mode EV reaches the 1v1 level (~0.23) over a
window. Needs live-adjustable weights in `CombinedState` (AirDrill already has a live-annealed parameter as precedent) and
an explicit return to the match mix at the end, evaluated on normal-reset play.

## 11. Next critic candidate this data suggests (separate lever)

EV < 0 beyond 10 s means the value baseline adds variance to far-state advantages. Candidates, none tested: shrink
critic outputs for far states, lower variance in the value fit (more critic epochs, an ensemble), episode-elapsed time as
a critic input (that IS known at run time, unlike time-to-end).

## 12. Record (fill in)

| Item | Phase 0 baseline | Control | Treatment |
|---|---|---|---|
| Goals/10k 2v2 / 3v3 (median, SD) | | | |
| Row Frac 0-10 s 2v2 / 3v3 | | | |
| Matched-bin EV (4 bins x 2 modes) | | | |
| Rating/2v2, 3v3, Ref/Oldest Share | | | |
| Verdict | | | |

## 13. Phase 0 results (2026-11-17, 114M steps on the current build; mixed team-mode reset mix as live)

Sources: `build/checkpoints_gco_6.1/metrics.jsonl` (27 history lines, 60.89B -> 61.01B steps; the every-10th-iteration
panels have n = 12) and the per-iteration trainer logs from 16:49 on (323 iterations, for the goal-rate noise). Several
restarts are inside this span; none changed the training config.

**Goals per 10k rows** (per-iteration, 323 iterations): mean / SD / CV = 1v1 18.1 / 6.0 / 0.33, 2v2 6.6 / 3.1 / 0.47,
3v3 3.6 / 1.8 / 0.50. Rows per goal: ~550 / ~1.5k / ~2.8k, so 3v3 has ~5x fewer goals per row than 1v1.

**Row share by time to episode end** (n = 12 lines, mean +- SD of a line):

| bin | 1v1 | 2v2 | 3v3 |
|---|---|---|---|
| 0-2 s | 0.112 | 0.084 | 0.072 |
| 2-5 s | 0.150 | 0.113 | 0.097 |
| 5-10 s | 0.200 | 0.161 | 0.140 |
| >10 s | 0.539 (+-0.13) | 0.642 (+-0.12) | 0.691 (+-0.14) |
| 0-10 s total | 0.461 | 0.358 | 0.309 |

**EV by bin** (median of the 12 lines; SD in the last column is for 3v3 >10 s only because of one outlier):

| bin | 1v1 | 2v2 | 3v3 |
|---|---|---|---|
| 0-2 s | 0.940 | 0.901 | 0.876 |
| 2-5 s | 0.694 | 0.551 | 0.506 |
| 5-10 s | 0.251 | 0.120 | 0.152 |
| >10 s | -0.262 | -0.272 | -0.206 (SD 0.39) |

Pooled mode EV on these lines: 1v1 0.457, 2v2 0.344, 3v3 0.267.

### Where the team gap comes from (rough, descriptive)

Weighting each mode's per-bin EV by its row shares (this ignores between-bin variance, so it is a decomposition of the
bin-weighted EV, not of the pooled EV): 1v1 = +0.118, 3v3 = -0.009, gap 0.127. Putting 3v3's per-bin EVs on 1v1's row
shares gives +0.093.
- **Row-share (horizon) composition: ~0.102 of the 0.127 (~80%).**
- **Per-bin critic quality: ~0.025 (~20%).**
So the team-mode EV deficit is mostly that 3v3 has 69% of its rows beyond 10 s (where EV is negative) against 54% in 1v1.
The per-bin ordering 1v1 > 2v2 > 3v3 is real but second-order, which downgrades the `Delta` test
(`GCO_TEAM_CRITIC_TEST.md`) below this one in priority.

### Noise and the minimum detectable effect (section 3)

- Goal rate by opponent source: iterations where the whole fleet served ONE external bot run ~2x the goals per row
  (1v1 ~30 vs ~17, 3v3 ~7.6 vs ~3.1), because the bots end episodes quickly. The opponent source explains 20% / 22% /
  **43%** of the variance in Goals/10k (1v1 / 2v2 / 3v3).
- Lag-1 autocorrelation of 3v3 Goals/10k is 0.34 (effective sample ~0.49 x iterations).
- For one arm of 200M steps (~500 iterations; ~420 mixed-source iterations after dropping single-bot ones; effective n ~206),
  difference of two sequential arms, 80% power, alpha 0.05: **MDE ~12% (3v3) and ~13% (2v2)** on Goals/10k.
- Detecting 5% needs ~2,400 iterations per arm, ~950M steps (about 3.5 hours at ~80k steps/s) per arm.

### Verdict under the section 8 rule

The pre-registered effect (+5% to +10%) is **below the MDE (~12%)** for a sequential 200M-step A/B. As written, the test
would be unreadable. The decision rule says go to section 9 (the boundary lever) unless the design changes. The data
suggest the design can change cheaply:
1. **Interleave the arms inside one run** (some team arenas on the treatment mix, the rest on control, tag the arm in
   `rowTag`). Both arms then see the same iteration, the same opponent source and the same policy, which removes the
   source-mix and drift noise that dominate here. Likely a large variance cut, but UNMEASURED.
2. Or analyse only mixed-source iterations and stratify by source.
3. Or accept ~950M steps per arm.
4. Or a stronger lever (carry share above 0.25), at more risk to play style.

Not verified: the interleaving needs a one-bit arm tag per row (the tag is currently source x 4 + teamSize - 1 in a
uint8, so there is room) and a per-arena arm index in `MakeEnv`.
