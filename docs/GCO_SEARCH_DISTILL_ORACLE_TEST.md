# GCO search: oracle-opponent test and distillation test (pre-registration, 2026-11-18)

Status: **Test A run on Dice and GalactusV2 (sections 8, 9); Test B run (section 10): no gain, controls partly failed; label test-retest (section 11): labels are mostly noise.** Branch `gco-goalcritic`. Follows `GCO_LOOKAHEAD_TEST.md` (sections 12-14). Doctrine:
measurement before machinery; one lever; pre-registered criteria; a guard; a revert path. Offline, CPU only; the trainer, its
checkpoints and the GPU are not touched. User decision (2026-11-18): search is for **training only**, not for live play.

## 1. Where we are (measured, `GCO_LOOKAHEAD_TEST.md`)

Checkpoint `best_r2721`, 1v1, lookahead H=30 steps (2 s), K=8 candidates, leaf = a*V+b.

| Opponent in the real game | Raw policy | Search | Gap |
|---|---|---|---|
| Our own raw policy (sampling) | 0.500 by construction | 0.770 | +0.27 |
| GalactusV2 | 0.220 (100 games) | 0.150 (200) | -0.07 |
| Dice | 0.365 (100) | 0.205 (200) | -0.16 |
| ChronosV2 | 0.270 (100) | 0.190 (200) | -0.08 |
| Nexto | 0.730 (100) | 0.703 (200) | -0.03 |

Standard errors: about 0.05 for the 100-game raw arms, 0.035 for the 200-game search arms. Leak checks in section 13 of that doc
(worst candidate 0.005, random-top8 0.150, argmax opponent 0.728) say the self-play 0.770 is not a labelling or side artifact.
The argmax control (the policy's own argmax instead of sampling) scored 0.555, so sharpening alone is worth about +5.5.

Reading: search helps against our own policy and does not help, probably hurts, against four different bots. **Hypothesis H-mismatch**
(untested): the rollouts model the opponent as our own policy, which is wrong for those bots.

## 2. Test A: oracle-opponent search (diagnostic only)

**Question.** Is the opponent model the whole problem? Identical search, except that inside the rollouts the opponent is the real
bot's own policy instead of ours.

**Design.** Same harness, checkpoint, seeds, K, H, M, leaf calibration and scoring as section 14. Change only the rollout opponent:
at every rollout step the bot's `Act` runs on the rollout arena's own `GameState` (argmax, as it plays in the real game). Mid-rollout
bot state: Nexto keeps per-player previous-action memory, the GGL bots keep per-arena ball predictors, so each rollout arena needs its
own bot state copied from the real game at snapshot time (or, for stateless bots, nothing). The first-step opponent action is also the
bot's own. Start with **Dice** (simplest state, largest search deficit), then GalactusV2; Nexto and ChronosV2 only if the first two read.

**Guard (the oracle must be right before it means anything).** Selftest: snapshot a real game against the bot, replay the bot's real
actions through a rollout arena, and require identical obs and end state (same bar as the section 12 selftest: end state identical,
obs within 1e-4). A bot-state copy bug would look like a failed oracle.

**Games.** 200 per bot for search, and the matching raw arm extended to 200 games on the same seeds (the section 14 raw arms have
only 100 games each).

**Predictions.**

| Outcome | H-mismatch true | H-mismatch false |
|---|---|---|
| Oracle search minus raw (Dice, GalactusV2) | >= +0.08 on at least one | within +-0.05 or negative |
| Oracle minus section-14 search | >= +0.10 | within +-0.05 |

A result counts only if it clears the floor AND the interval (about 2 se, i.e. 0.10 at these sizes) excludes the baseline.
Floors are set at the sample size's resolution; effects smaller than about 0.08 cannot be called from 200 games.

**Decision.**
- Supported: the search is sound given a good opponent model. Distillation (Test B) is worth its cost; an opponent-model component is
  a separate, later project (not part of this document).
- Not supported: search does not help this policy against strong opponents even with a perfect model. Test B is then very unlikely to
  give a bot gain; run it only as a cheap check or drop it.
- Mixed (one bot only): report per bot, do not average.

The oracle is NOT deployable (it needs the opponent's weights) and is never used in training. It is run only to read H-mismatch.

## 3. Test B: distilling search into the policy (the training-only question)

**Question.** If a copy of the policy is trained toward the search's chosen actions, does the copy play better in RAW play (no search
anywhere at test time)?

**Labels.** Self-play games with the section-12 search (K=8, H=30, calibrated leaf) on both sides' decisions or on one side (to
decide in the build; record which). For every decision store: obs (278), legal mask (90), the K candidate actions, their search
scores, and the chosen action. Both soft labels (softmax over candidate scores, temperature chosen on a held-out slice before the
runs) and hard labels (chosen action) are trained as separate arms. Opponent-model note: self-play labels are the setting where the
rollout opponent model is nearly exact (section 13), so Test B does not depend on Test A to be well-defined.

**Size.** About 12 s per game on 4 cores (self-play games are shorter than the 17 s mean, so this is an upper bound). Plan 3,000
games, roughly 100k-200k labelled decisions, about 10-12 hours; stop early at a held-out agreement curve plateau. Hold out 10% of
games (by game, not by row; adjacent frames are near-duplicates).

**Fine-tune.** A copy of `best_r2721`. Loss: cross-entropy to the label distribution over legal actions, plus
`beta * KL(copy || original)` to bound drift (beta fixed before the run, small grid on the held-out games only). Optimizer and LR
chosen on held-out label agreement, never on match results. CPU libtorch; the layer config is the one in `search_match.cpp` (`Nets`).
Policy head only; the shared trunk is trained too (it is the same module the policy reads) but the critic heads are not used and not
saved with the copy (this is a probe, not a checkpoint to deploy; recovery doctrine: no hybrid checkpoints enter the trainer).

**Arms (all evaluated RAW, sampling at temperature 1, 1v1).**

| Arm | Description |
|---|---|
| S-soft, S-hard | trained on search labels (soft, hard) |
| C-argmax | trained toward the original policy's own argmax (the sharpening control) |
| C-shuffle | search labels shuffled across rows (fine-tuning alone, no information) |
| O | the unmodified checkpoint (reference) |

**Evaluation.** Each arm vs O, and vs each of GalactusV2, Dice, ChronosV2, Nexto, on the same seeds and sides, **300 games per
cell** (se about 0.03 each; the O-vs-bot raw baselines are re-run at 300 games). Raw games are cheap (a few minutes per 100).
The bar is against the BOTS, not against O: self-play is exactly where search is already known to help.

**Predictions.**

| Measure | Distillation works | Distillation does not |
|---|---|---|
| S-soft/S-hard vs O | > 0.55 | within +-0.05 |
| S vs each bot, minus O vs the same bot | >= +0.05 on at least 2 of 4 bots, none worse than -0.05 | within +-0.05 or worse |
| S minus C-argmax vs the bots | >= +0.04 | within +-0.04 (it is just sharpening) |
| C-shuffle vs O and vs bots | within +-0.03 (control reads null) | otherwise the pipeline moves results by itself |

**Guards.** Held-out label agreement reported for every arm and above the original policy's agreement; no NaN/inf; entropy of each
copy reported (a collapsed copy would pass "beats O" on sharpening alone, which C-argmax catches); the seed/side assignment identical
across arms; C-shuffle must read null or the evaluation is void.

**Decision.**
- Works (floors met, beats C-argmax): pre-register a training integration separately (how labelled self-play enters PPO, the
  critic-aliasing and clipping-ratchet rules both apply because the data is off-policy). This document does not authorise any change
  to the trainer.
- Sharpening only (S about C-argmax): drop search for training; the cheaper lever is entropy/temperature schedule.
- No effect: drop it.

## 4. Order and cost

1. Selftest for the oracle's bot-state copy, then Test A on Dice (about 40 min per 200-game arm, raw arm extended to 200: minutes).
2. Test A on GalactusV2 if Dice reads either way.
3. Test B only after A (or immediately, if the user prefers: B is well-defined without A, but A decides how much to hope for).
Compute: 4 cores niced, trainer untouched. Build effort: A is about half a day (bot-state copies), B about a day (label writer,
fine-tune loop, evaluation driver).

## 5. Revert

Everything is a new offline tool and doc. Nothing in `ExampleMain.cpp`, `Learner.cpp`, the trainer or any checkpoint changes.
Delete the tools and the doc to revert.

## 6. Known unknowns / not verified

- Whether the self-play gain survives being distilled at all is unknown; the 0.770 may be mostly sample-noise filtering (the argmax
  control was +5.5; best-vs-worst candidate spread 0.77 vs 0.005 says the search distinguishes actions, not that a student can learn
  the distinction from 100k labels).
- The soft-label temperature and KL weight are free parameters; they are chosen on held-out agreement only.
- 1v1 only. 2v2/3v3 rollouts cost 2-3x the sim step and the critic is weaker there (section 1 of `GCO_CRITIC_CONTEXT_TEST.md`).
- A distilled copy's gain, if any, is measured offline against frozen opponents; it does not show that the same labels help when
  mixed into live PPO.
- The raw baselines vs the bots are only 100 games each in section 14 and are re-run in this plan.

## 7. Record (fill in)

| Item | Value |
|---|---|
| Oracle selftest (Dice, GalactusV2) | |
| Test A: oracle search vs raw, per bot | |
| Test A verdict (H-mismatch) | |
| Labelled decisions / games, held-out agreement | |
| Test B arms vs O | |
| Test B arms vs each bot | |
| Controls (C-argmax, C-shuffle) | |
| Verdict | |

## 8. Test A result: Dice (2026-11-18)

Harness: `ORACLE=1 EXT_BOT=dice` (rollout opponent = Dice's own policy, called on the rollout arena's state). Guard: `oracletest`
(GGL_BOT_SHUFFLE=0), 6 trials x 30 steps: 0 bot-action mismatches, end states identical (PASS). The test itself runs with the shuffle at
its default, as section 14 of `GCO_LOOKAHEAD_TEST.md` did.

| Arm (vs Dice, 200 games, same seeds) | Score | W/L/D |
|---|---|---|
| Raw policy | 0.260 | 37/133/30 |
| Search, opponent modelled as our policy (section 14) | 0.205 | 32/150/18 |
| **Search, oracle opponent** | **0.805** | 153/31/16 |

Oracle minus raw = +0.545; oracle minus modelled-opponent search = +0.600 (se about 0.035 per arm). Far above the +0.08 floor.
The earlier 100-game raw baseline (0.365) and this 200-game one (0.260) differ by about 2 se, which is noise-sized next to the effect.

Reading: **H-mismatch supported for Dice.** The search is sound when its opponent model is right; the section 14 deficit was the model.
Caveats that bear on how far this reads: (1) Dice plays argmax, a deterministic function of the state, so the oracle can predict its
reply exactly, which is stronger than a good approximate model, and the gain is an upper bound on what a learned model could reach;
(2) the oracle is not deployable (needs the weights); (3) one bot only, GalactusV2 not yet run; (4) 1v1.

Consequence for Test B: distillation labels from self-play use a near-exact opponent model (section 13 of the lookahead doc), and
this result says exact models give large gains, so Test B is worth its cost. Next: GalactusV2 oracle (selftest first), then Test B.

## 9. Test A result: GalactusV2 (2026-11-18)

Guard: `oracletest` on GalactusV2 (per-arena ball predictor), 6 trials x 30 steps, 0 bot-action mismatches, end states identical.

| Arm (vs GalactusV2, 200 games, same seeds) | Score | W/L/D |
|---|---|---|
| Raw policy | 0.307 | 48/125/27 |
| Search, opponent modelled as our policy (section 14, earlier raw baseline was 0.220 at 100 games) | 0.150 | 25/165/10 |
| **Search, oracle opponent** | **0.680** | 123/51/26 |

Oracle minus raw = +0.373; oracle minus modelled-opponent search = +0.530 (se about 0.035 per arm). Both floors cleared.
Two bots now agree: with the right opponent model, search gains +0.37 to +0.55 in 1v1; with our policy as the model it loses.
Verdict on Test A: **H-mismatch supported on Dice and GalactusV2.** The oracle remains an upper bound (deterministic argmax opponents,
exact weights) and is not deployable. ChronosV2 and Nexto were not run (not needed for the decision). Next: Test B.

## 10. Test B result (2026-11-18)

**Labels.** 3 processes, same checkpoint and search settings (H=30, K=8, M=1, leaf a=0.4618 b=-0.0033), seeds 101/102/103, 204,913 rows from
774 games; search side scored 0.760 / 0.798 vs the raw policy (A: 350 games; C: 250; B was cut at about 140 games when the trainer slowed
16%, past a self-set 15% guard; the user then accepted the slowdown with a 50% hard limit, enforced by a watchdog that never fired).
Held out by game (10%). Label alignment check: the original's argmax equals the search's chosen candidate on 22% of rows and the
policy-top candidate (index 0) is chosen on 21%, so labels and observations line up.

**Deviations from the pre-registration (all made after seeing the first grid, none from match results).**
1. The first grid (soft labels, temperature 0.1, argmax-agreement metric) made every copy worse: agreement 0.219 -> about 0.17, entropy
   1.94 -> 2.5-2.66. Cause found in the labels: median best-vs-second-best score gap is 0.0076 and median best-worst range 0.088, so at
   temperature 0.1 the targets have 1.84 of a possible 2.08 nats of entropy (top weight 0.26). With M=1 the "chosen" action is mostly rollout
   noise on rows where the candidates tie.
2. Changes: label temperature 0.01/0.03; rows with best-worst range below 0.05 dropped from training and from the informative held-out
   metrics (114,210 train rows, 13,467 held-out informative rows); the held-out metric became the search advantage (expected candidate
   score under the policy, renormalised over the 8 candidates, minus the candidates' mean) because argmax agreement against a noisy single
   pick is a poor metric. Original: +0.0136.
3. Second grid (lr 1e-6/3e-6/1e-5, T 0.01/0.03, beta 0.1, 2 epochs): no setting raised the advantage (+0.0091 to +0.0126); every setting
   raised entropy. No setting qualified, so the two ends of the lr range (1e-6, 1e-5) were evaluated.
4. Print bug: "policy mass inside candidates 0.000" for the original is an argument-evaluation-order bug; the copies' numbers are right.

**Match results, raw play, 300 games, same seeds (se about 0.029 per cell; a gap between two arms has se about 0.04).**

| Arm | vs ORIGINAL | vs Dice | vs ChronosV2 | vs GalactusV2 | vs Nexto |
|---|---|---|---|---|---|
| ORIGINAL | 0.510 | 0.307 | 0.260 | 0.333 | 0.697 |
| S-soft lr 1e-6 | 0.433 | 0.227 | 0.250 | 0.198 | 0.622 |
| S-soft lr 1e-5 | 0.318 | 0.138 | 0.080 | 0.100 | 0.427 |
| C-argmax lr 1e-5 | 0.527 | 0.328 | 0.330 | 0.327 | 0.698 |
| C-shuffle lr 1e-5 | 0.005 | 0.007 | 0.000 | 0.003 | 0.020 |

**Verdict: distillation of these labels does not work. Bar not met** (needed >= +0.05 on at least 2 of 4 bots and none worse than -0.05;
the soft copies are worse everywhere, monotonically with lr). The argmax control is within noise of the original (largest gap +0.07 on
ChronosV2, se of the gap 0.04), so sharpening is not clearly better either.

**The null control failed:** the pre-registration required C-shuffle to read within +-0.03 or the evaluation is void. It collapsed the
policy (0.005 vs the original). Shuffled labels are wrong actions for the state, not zero information, so this control is too strong and
does not isolate "fine-tuning alone". Strictly the evaluation is void under the stated guard; I report it anyway and do not treat the soft
arms' degradation as a clean statement about label quality. A proper null (labels permuted within the candidate set) was not run.

**What this does and does not show.** It shows this recipe (hard fine-tune of the policy toward single-rollout, noisy search targets) is
harmful, increasingly so with lr. It does not show search is useless for training: the oracle result (section 8-9) stands, and the
labels' noise (M=1, single +-1 outcomes, tied candidates) is the likeliest cause but is a hypothesis. Untested alternatives: M >= 4 rollouts
per candidate (lower-variance labels), a search-advantage-weighted policy-gradient update with PPO's clip instead of cross-entropy
(the form that would actually enter the trainer), training the critic on search values, a proper within-candidate-set null.

## 11. Label test-retest (2026-11-18)

Harness: `RETEST=1` searches every decision twice, same state/candidates/settings (K=8, M=1, H=30), different ROLLOUT randomness; pass 0 is played
(trajectories identical to a normal run), pass 1 only feeds the statistics. 60 self-play games, 15,881 decisions; search side scored 0.842
(46/5/9, se about 0.065). Chance agreement for the same pick is 1/8 = 0.125.

| Pass-0 score range (best-worst) | Rows | Same pick | Score correlation across passes | Regret of pass 1's pick under pass 0's scores |
|---|---|---|---|---|
| < 0.05 (ties) | 5,768 | 0.317 | 0.027 | 0.0093 |
| 0.05 - 0.2 | 4,739 | 0.154 | 0.039 | 0.0510 |
| 0.2 - 0.5 | 2,758 | 0.175 | 0.073 | 0.1478 |
| >= 0.5 | 2,616 | 0.182 | 0.137 | 0.3202 |
| informative (>= 0.05) | 10,113 | 0.167 | | |

Reading: on the rows that carry any spread, two independent searches of the same decision pick the same action 15-18% of the time (chance 12.5%),
and their candidate scores correlate 0.04-0.14. **Per-decision labels are mostly noise**, which explains the Test B failure (section 10): the soft
and hard targets were rollout noise with the policy's own sampling noise on top. (The tie bin's 0.317 is the tie-break favouring the first
candidate, which is the policy's top action, not information.)

Spearman-Brown estimate (assumes the same noise structure; indicative only): to reach a 0.8 test-retest reliability from a single-rollout
correlation of 0.07-0.14, each candidate needs roughly 25-50 rollouts (M), i.e. 25-50x the search cost. Not affordable at this scale.

**Puzzle, not explained:** the search as a whole still wins (0.76-0.84 vs raw; worst-candidate arm 0.005, random-of-top-8 arm 0.150), yet its
"best" pick is near-chance reproducible. Hypothesis (untested): the useful signal is a VETO of rare catastrophic candidates (a conceded goal inside
2 s is consistent across passes), not a ranking of the good ones. The test that would show it: agreement of the WORST candidate across passes,
and of the top-3 / bottom-3 sets. If true, distillation should learn "avoid these actions" (a penalty or a clipped negative-advantage update),
not "play this action" (cross-entropy toward the best). Not run.
