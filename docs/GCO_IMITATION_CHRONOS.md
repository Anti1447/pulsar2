# Imitation learning from ChronosV2: hypotheses and experiments (plan, 2026-11-23)

Status: **PLAN ONLY. Nothing is implemented or run.** User-directed (2026-11-23): start after the architecture comparison (H-014..H-017, chain `gco-arch3`) finishes. First at tickSkip 8 with a setup
close to Chronos's own, to see how long cloning takes; later at tickSkip 2. Doctrine applies: measurement before machinery, one lever per run, a pre-registered criterion, an automatic guard, a revert path.
Experiment ids below are E0..E8; each becomes a ledger entry (H-0xx) when it is launched, with its pre-registration copied from here.

## 1. Why this is a different question from the GCO runs

The GCO runs ask whether a bot can discover play from a goal-only reward (the acquisition wall). Cloning replaces discovery with copying, so it answers:
(a) how much of a strong bot's behaviour is recoverable from state-action pairs alone, at what cost in steps and wall clock; (b) whether a cloned start removes the wall, and what RL
then does to it; (c) whether behaviour learned at 15 Hz transfers to 60 Hz. It is **not** evidence about the architecture comparison or about optimism, and its runs must not be pooled with them.

## 2. What we have (checked in the repo)

- `GGLBotOpponent` (kind `CHRONOS_V2`, `GigaLearnCPP/src/private/GigaLearnCPP/GGLBotOpponent.{h,cpp}`) already ports Chronos's obs builder (`AdvancedObsPadded(3)`, 225 floats, every player
  x-mirrored onto one half, its boost-pad-timer typo kept), its action mask, and its action table (ours row for row, steer/yaw/roll negated for the mirrored car). Weights: `rlbot-run/chronos_v2/` (`SHARED_HEAD.lt`, `POLICY.lt`).
- Net: shared head 960x2, policy 960-800-640-640, ReLU, layer norm, 90 actions (from `docs/6.0.md`). It is a GigaLearnCPP net, so it loads into our `Model` classes. Inference is deterministic (argmax) as played in RLBot.
- Cadence: trained at **tickSkip 8, actionDelay 7** (the action takes effect on the last tick of the 8). The 6.0 run also uses delay 7, so a ts8 student with delay 7 acts exactly as Chronos does.
  (The GCO runs use delay 0 at ts8; a faithful clone must use 7. The mismatch is a confound to avoid, not to study.)
- The teacher can be **queried at any state**, because we hold its weights and obs builder. That makes DAgger and logit distillation cheap, which most imitation projects do not have.
- Evaluation tools exist: the external-bot gauntlet (`GGL_H2H_BOTS`, `h2h_bots.py`, 1024 games per bot), cross-architecture head-to-head (`GGL_H2H_OPP="<dir>@<sw>x<sd>/<pw>x<pd>"`), the kickoff test.
- Licence: neither Chronos repo nor its weights has a licence file; the weights are not in git (`*.lt` ignored) and must not be redistributed. A student distilled from it inherits that caution: keep student checkpoints out of git and do not package them as a release without the user's decision.

## 3. Method

**Loss.** Distillation on the teacher's full logits: `KL(softmax(teacher_logits / T) || student)`, with the teacher mask applied, plus an optional hard-label cross-entropy term. Hard labels throw away the teacher's second choices
and, because the teacher plays argmax, would make the student more peaked than anything it can verify; logits are free for us, so they are the default. T is a lever (E3).

**Where the states come from (the part that decides whether cloning works).**
1. *Teacher states*: Chronos playing Chronos, and Chronos against our current policy and against Nexto. Plain behaviour cloning on these.
2. *Student states* (DAgger): the student acts, the teacher labels every visited state. Fixes the usual failure where the student drifts into states the teacher never saw. A mixing schedule beta (probability the teacher's action is executed) decays from 1 to 0.
3. Mix the two in the buffer. The reset mix is the same fixed non-adaptive mix as the GCO runs (constraint C1: no dedicated drill instances).

**Implementation proposal (not built).** An `imitation` phase inside the trainer, not an offline Python pipeline: collection already runs 1024 arenas with the teacher wrapper, so the teacher-obs conversion is free, checkpoints are written in the native C++ format
(Python-saved weights would not round-trip into `torch::load`), and the existing evals and health tooling apply. The phase replaces the PPO surrogate with the distillation loss on the policy and shared head; the critic is untouched in the first phase (see E6).
Golden test before anything runs: a student built with Chronos's exact architecture and loaded with Chronos's weights must show KL about 0 and 100% argmax agreement on 100k states; a random-weight student must start at about ln 90 per state.

## 4. Metrics (every experiment reports the same set)

| Metric | Meaning / how it is read |
|---|---|
| `Imit/KL`, `Imit/Top1` | on a fixed held-out set of **teacher-visited** states and a second held-out set of **student-visited** states (the gap between them is the covariate shift) |
| `Imit/Top1 by situation` | kickoff, ground play, aerial, boost pickup, defence; a single mean hides what is not learned (aerial and flip timing are the expected holes) |
| Kickoff test | touches out of 10, median time to touch (healthy about 10/10, 3.4 s) |
| H2H vs Chronos | student vs teacher, 1024 games, both colours, Elo with the 1.5x-inflated standard error; **the primary behavioural metric** |
| Gauntlet | win share and first touch vs Nexto, Galactus, Dice, Chronos (1024 games each) |
| Cost | steps, teacher queries, GPU hours and wall clock to reach each of the criteria below |
| Policy entropy and teacher entropy | the student should converge to the teacher's, not below it |

Agreement is a proxy and is not a gate: argmax agreement of 85% can still lose to the teacher because the 15% are the decisive moments (flip timing, challenges). Gate on H2H and the kickoff test, as the doctrine says for probes: behavioural deltas, not decodability.

## 5. Hypotheses, with pre-registered predictions

- **H-A. A same-architecture, same-obs clone at ts8 reaches parity.** With Chronos's architecture, obs and cadence, distillation on a few hundred million teacher states reaches H2H share >= 0.45 vs Chronos (a clean copy cannot be better than 0.5 in expectation).
  Falsifier: share below 0.35 after 2B teacher-labelled states, or agreement plateauing under 70%.
- **H-B. DAgger beats plain cloning at equal teacher queries.** On student-visited states the DAgger student's KL is less than half the plain student's, and its H2H share is higher by more than the noise floor (E0).
  Falsifier: no difference in H2H, meaning covariate shift is not binding for this teacher.
- **H-C. Logit distillation beats hard labels.** At equal data, the student distilled on logits reaches a lower student-visited-state KL and an equal or better H2H share than argmax cross-entropy.
  Falsifier: no difference, in which case the simpler hard-label loss is kept.
- **H-D. The student needs Chronos's observation.** A student fed our own obs (278, canonical frame) reaches a lower plateau than one fed Chronos's obs, because the teacher's decisions are a function of its obs and ours differs by the x-mirror, the field set and the size (278 vs 225).
  Falsifier: matching plateaus, which would mean our obs carries everything Chronos uses and the teacher-obs wrapper can be dropped from the student path.
- **H-E. Capacity matters for the clone.** Chronos-sized is enough; our larger policy path or the winner of the architecture comparison is not meaningfully better.
  Falsifier: the larger net is better by more than the noise floor.
- **H-F. A cloned start breaks the wall.** A cloned student fine-tuned with the GCO goal-only reward has a no-touch penalty near 0 from step 0 (no wall) and does not lose its gauntlet score in the first 500M steps, if a KL anchor holds it (E6). Without the anchor, entropy collapse or value-mismatch damage appears.
  Falsifier: the gauntlet falls by more than 1.5x the control's noise SD within 200M steps of unanchored RL, as the recovery doctrine's mismatched-critic incident did.
- **H-G. Hold-label cloning transfers to ts2 without loss.** A ts2 student trained on the teacher's decisions held for 4 steps (faithful to how Chronos plays) reaches the same H2H share vs Chronos as the ts8 student when both play at their native rates.
  Falsifier: a ts2 student below 0.4. The competing variant (E8b, per-step queries) predicts a **worse** student, because the teacher queried off its own 8-tick phase flickers.
- **H-H. 60 Hz can exceed the teacher.** After cloning, RL at ts2 finds timing-tight mechanics ts8 cannot express. Pre-registered metric: aerial and flip-reset counts per game vs the ts8 student after the same RL budget. Exploratory; only licensed if H-G holds.

## 6. Experiments

Order is by dependency. Everything is single-seed unless noted; E0 gives the noise floor.

- **E0. Teacher self-consistency and noise floor (CPU, no training).** Chronos vs Chronos H2H (expect 0.5), the kickoff test, the 1024-game gauntlet of the teacher against Nexto, Galactus, Dice, and a repeat at a second seed to size the noise.
  Dump 1M teacher states and logits; confirm a student built from its weights has KL 0 (the golden test). Measure the teacher's entropy on its own states, which is the target the student should match. Exit criterion: the golden test passes.
- **E1. Plain cloning at ts8, Chronos architecture and obs, hard-label vs logit loss.** Teacher states only (Chronos vs Chronos and vs us). KL / agreement against data size (10M, 30M, 100M, 300M states) and wall clock; H2H vs Chronos at each size.
  Answers H-A and H-C and gives the headline cost number: how long cloning takes. Revert path: nothing downstream is changed.
- **E2. DAgger at ts8.** Same student, same data budget, beta decayed from 1 to 0 over the first third. Compare with E1 at equal teacher queries. Answers H-B.
- **E3. Distillation temperature.** T in {0.5, 1, 2} on E2's best configuration. Student entropy vs teacher entropy; whether a flatter student keeps second-choice behaviour that RL can later use.
- **E4. Observation and architecture ablations (one at a time).** (a) the student reads our 278-dim obs instead of Chronos's 225 (H-D); (b) our shared/policy sizes instead of Chronos's (H-E); (c) the architecture-comparison winner, if there is one. Each against E2.
- **E5. Where does the clone fail.** Top-1 by situation, and a replay review of the student's lost games against Chronos. Likely holes: air-roll and flip timing, boost management, demos. Output: a table of the five worst situations and their agreement.
- **E6. Fine-tune the clone with the goal-only reward (GCO config).** Student from E2 or E4. The critic is first warmed on the frozen student's returns for about 100M steps with the policy not updating (the recovery-doctrine rule against a mismatched critic), then PPO with a decaying KL penalty to the teacher logits.
  Arms: no anchor, anchor decaying over 500M, anchor kept. Compare with the scratch runs (`nexchron`, ts2) on the wall check and the gauntlet. Guards: entropy within 30% of the teacher's before RL starts; the gauntlet falling three evaluations in a row stops the run. Answers H-F.
- **E7. Optimism on a cloned base (conditional).** The composition critic (V-dagger twins) on top of E6's best arm, only if E6 shows the clone plateaus because the policy never tries new things. This is the deferred memory item ("V-dagger on a clean GCO base"); decide once E6 is read.
- **E8. Down to ts2.** (a) hold labels: ts2 student trained on teacher decisions repeated over 4 steps (the reverse of `DecimatedOpponent`); (b) per-step queries of the teacher at every ts2 step; (c) initialise the ts2 student from E2's weights, then train on (a). Evaluate at native rates, and also with the student's actions decimated to 15 Hz as a control for the rate itself. Answers H-G. Then H-H: RL at ts2 from the cloned start.

## 7. Costs and schedule

- The GPU is occupied by the architecture chain for about three more days; E0 needs only the CPU. Everything else starts after "ARCH ALL DONE" in `build/arch3_status.log`.
- Teacher-labelled data comes from the existing collection path, so it costs a normal rollout (about 55-65k steps/s here); the distillation loss over the teacher logits adds a small share. Wall clock for 300M states is therefore roughly 1.5-2 hours, and E1 and E2 together about half a day. These are estimates, not measurements; E1 measures them.
- The code is the larger cost: the imitation phase, the teacher-logit pass in collection, the `Imit/*` panels, the golden test and a test target in the style of `tests/test_decimated_opponent.cpp`.

## 8. Risks and what we will not do

- **Entropy collapse and value mismatch** after the switch to RL (E6): the anchor and the critic warm-up exist for this; no hybrid checkpoints, per the recovery doctrine.
- **Covariate shift** on near-goal and aerial reset states that Chronos rarely visits (the GCO mixes put the ball there): the student sees those only through DAgger, so E2's student-visited KL is measured on the GCO mix as well as on teacher-played states.
- **The teacher is not ours to redistribute** (section 2).
- **Per-step panels at ts2 are not comparable to ts8**; H2H and the gauntlet are, and are the comparisons used.
- Not doing: mixing cloning and RL in the same loss from step 0, reward shaping for the cloned run, or any opponent-source change. One lever per run.

## 9. Record (fill in as the experiments run)

| Experiment | Launched | Result |
|---|---|---|
| E0 teacher noise floor | 2026-11-23 | Part 1 done (section 10); the golden test needs the student code, so it waits for the implementation |
| E1 plain clone ts8 | | |
| E2 DAgger ts8 | | |
| E3 temperature | | |
| E4 obs / arch ablations | | |
| E5 failure map | | |
| E6 RL fine-tune | | |
| E7 optimism on clone | | |
| E8 ts2 transfer | | |
| E9 Chronos vs Galactus disagreement | 2026-11-23 | Done (section 12): argmax agreement 0.23-0.27 (the pre-registered H-I falsifier as written fires), but the teachers are high-entropy so the metric is weak; distribution overlap about 0.6 |
| E10 two-teacher variants | | |

## 10. E0 part 1 result (2026-11-23): the teacher's own match record

Tool: `GGL_H2H_TEACHER=ChronosV2 GGL_H2H_NATIVE=1` on the `GGL_H2H_BOTS` path (`GauntletEval::RunBots`, `Learner.cpp`), run by `tools/gco_health/e0_teacher.sh`; results in `build/e0_teacher.jsonl`.
Chronos plays as the subject at its trained cadence (tickSkip 8, actionDelay 7), argmax both sides, 1024 games per pairing, real kickoffs, a game ends at a goal or after 10 s without a touch, sides alternate. Two seeds.
(The loaded checkpoint is the `nexchron` arm's and is unused; CPU, niced; a 64-game smoke test ran first and its results were discarded.)

| Opponent | Seed 777001 (W-L-D, share, Elo) | Seed 777002 |
|---|---|---|
| ChronosV2 (itself) | 472-474-78, 0.499, -1 +-16 | 472-473-79, 0.500, 0 +-16 |
| GalactusV2 | 479-529-16, 0.476, -17 +-16 | 505-505-14, 0.500, 0 +-16 |
| Dice | 744-243-37, 0.745, +186 +-19 | 759-240-25, 0.753, +194 +-19 |
| Nexto | 968-51-5, 0.948, +503 +-37 | 957-62-5, 0.937, +469 +-34 |

Standard errors are the Elo 1.5x-inflated ones used elsewhere in this program.

Readings:
- **Self-play is 0.50** (0.499, 0.500), so the harness is symmetric: no side or kickoff bias in this mode. Chronos is the same object on both sides, so this also shows two `Act` calls on one bot do not interfere.
- **Seed noise** is about +-0.02 in share (Galactus moved 0.476 to 0.500 between seeds; Nexto 0.948 to 0.937). The parity criterion of H-A therefore needs a margin: "share >= 0.45 vs Chronos" is inside about 2 sigma of 0.5 for 1024 games, so a student at 0.45 is distinguishable from the teacher only just. Use 2048 games for the final verdict, or two seeds.
- **Chronos is about even with Galactus, well ahead of Dice (about +190) and far ahead of Nexto (about +480).** Targets for the clone's gauntlet: a student at parity with Chronos should show the same pattern against these bots (roughly 0.75 vs Dice, 0.94 vs Nexto, 0.5 vs Galactus).
- **First touch is lopsided against Galactus and Dice** (Chronos never first at the kickoff, 0/1024) and mostly shared or lost against Nexto. A clone's kickoff test should therefore use the real kickoff outcome vs these bots, not only "touches within 10 s".
- Games last 15-26 s on average, so a 1024-game pairing is about 16-27k teacher decisions per side; the full table took about 7 minutes of CPU per seed.

Not yet done in E0: the logit dump of 1M teacher states, the teacher's entropy on its own states, and the golden test (a student built with Chronos's architecture and weights must show KL about 0). These need the imitation phase and are part of E1's implementation.

## 11. Galactus as a teacher, and imitating both (2026-11-23)

Same E0 measurement with GalactusV2 as the subject (`TEACHER=GalactusV2 tools/gco_health/e0_teacher.sh`, `build/e0_teacher_GalactusV2.jsonl`; native cadence, argmax, 1024 games, seeds 777001 / 777002).

| Subject vs | Chronos as subject | Galactus as subject |
|---|---|---|
| ChronosV2 | (itself) 0.499 / 0.500 | 0.491 / 0.511 |
| GalactusV2 | 0.476 / 0.500 | (itself) 0.487 / 0.492 |
| Dice | 0.745 / 0.753 (+186 / +194) | 0.771 / 0.772 (+211 / +212) |
| Nexto | 0.948 / 0.937 (+503 / +469) | 0.906 / 0.899 (+394 / +381) |

- **Chronos and Galactus are the same strength** against each other (0.49-0.51 both ways, inside noise of 0.02). Galactus is a little better against Dice (+25 Elo, about 1.2 sigma) and a little worse against Nexto (-100 Elo: it
  draws and times out more, `ft`/`mean_s` below); neither dominates, so strength alone does not choose the teacher.
- **Style differs.** Galactus-vs-Galactus games are long (31 s) and 16% draws (a no-touch or 60 s timeout), Chronos-vs-Chronos 26 s with 8% draws; Galactus wins the kickoff first touch against Chronos in 1024/1024 games and Chronos
  the opposite against Galactus, yet the goals come out even. The two are different policies of equal strength, not two copies of one.
- **Practical differences for cloning (from `GGLBotOpponent.h`, `docs/6.0.md`).** Chronos: 225-float obs, x-mirrored (the student sees half the symmetric state space, which makes cloning easier), 960x2 / 960-800-640-640 ReLU net, no ball prediction.
  Galactus: 258 floats including five ball-prediction samples (0.25, 0.5, 1, 1.5, 2 s) that our `BallPredictor` produces every step (extra CPU per teacher query), 1024x2 / 1024x4 LeakyReLU net, about 25% more parameters, trained to 201B steps.
  Neither is licensed for redistribution.

**Recommendation.** Start with Chronos as planned: equal strength, a smaller net, a mirrored (easier) obs, no per-step ball prediction cost. Galactus is the second teacher once the pipeline is validated on Chronos (E1 golden test passing),
because the golden test and the cost measurements transfer unchanged. Doing both is a real option, but it has a specific failure mode that must be measured before it is built.

**Imitating both: the options and the failure mode.**
The student's label at a state is a function of the state only. Where the two teachers disagree (they are different styles), minimising KL to a mix of the two targets trains the student to put mass on **both** modes. If the choices are
coherent over time (e.g. take the early ball vs hold the defensive line) a student that blends them can play a plan neither teacher would, usually worse. Three variants, in order of how much I would trust them:
1. **Two separate students**, one per teacher. No mixing, a clean comparison between teachers, and the cheapest to reason about.
2. **One student with a teacher-id input** (a flag in the obs, or two policy heads on one trunk), trained on each teacher's labels with its id; at play time one id is chosen. It shares features between the teachers without blending their policies.
3. **An averaged-logit ensemble teacher.** The standard ensemble-distillation recipe; it can beat both teachers when their errors are independent, but it is the variant exposed to the mode-blending problem above, so it is only licensed if the disagreement measurement (E9) says the teachers mostly agree.

**New hypotheses**
- **H-I. The teachers disagree on a minority of states and mostly on style decisions.** Argmax agreement between Chronos and Galactus on the same states is between 55% and 85% (below that, a mix is incoherent; above, they are nearly interchangeable). Falsifier: agreement above 90% (use one teacher) or below 40% (never mix).
- **H-J. A teacher-id student matches each teacher's own clone within noise** (H2H share against each teacher >= the separate students' minus 0.03), at 1.0x the data per teacher. Falsifier: either id's student is below its separate clone by more than 0.05.
- **H-K. An ensemble clone does not beat its better single-teacher clone.** If H-I puts agreement under about 70%, the ensemble student loses to both separate clones in H2H; above about 85% it ties them. This is a prediction that blending hurts, so a positive ensemble result would be a surprise worth replicating.

**New experiments**
- **E9. Teacher disagreement (CPU, needs only the existing wrappers).** Play both bots, one acting on the arena, and at every decision record both teachers' argmax and the KL between their action distributions, on the same state. Report agreement by situation (kickoff, ground, aerial, boost, defence), the state-conditional KL histogram, and the fraction of high-disagreement states that precede goals. This is cheap enough to run during the architecture chain (the same CPU path as E0) and decides H-I before any student code is written.
- **E10. Two-teacher variants (after E2).** The three variants above, each at ts8, each against the single-teacher DAgger clone (E2) at equal total teacher queries; H2H against both teachers and the gauntlet.

## 12. E9 result (2026-11-23): Chronos and Galactus on the same states

Tool: `GGL_H2H_DISAGREE=1 GGL_H2H_NATIVE=1` (`TeacherDisagree.h`, `GGLBotOpponent::recordProbs`, `Learner.cpp`), `tools/gco_health/e9_disagree.sh`, results `build/e9_disagree.jsonl`. Native cadence (ts8, delay 7), 512 games per set, seed 777001, CPU.
Both bots build their own obs from the same game state; one acts, the other is queried on that state and scored (nothing it outputs is executed). Four sets of 460,800 decisions each: Chronos vs Galactus (each side observed by the other), Chronos vs Chronos, Galactus vs Galactus
(arenas keep playing after a goal, so each set includes the post-goal kickoffs, 13% of rows). Self-check: a teacher observed by itself reads agreement 1.000, JS 0.000, TV 0.000, on both bots (the probability mapping and row alignment are right).

All rows (all four sets within 0.01 of each other on every figure, so the figures below are the pooled picture; per-situation rows are Chronos acting in Chronos-vs-Galactus):

| Situation | argmax agree | total variation | JS (nats, max 0.69) | share of rows |
|---|---|---|---|---|
| kickoff | 0.20 (Galactus acting: 0.38) | 0.55 / 0.50 | 0.27 / 0.23 | 13% |
| air | 0.13 | 0.30 / 0.34 | 0.12 / 0.14 | 46% |
| ground near the ball | 0.36 | 0.41 / 0.43 | 0.16 / 0.17 | 17-24% |
| ground far | 0.36-0.42 | 0.39 / 0.36 | 0.15 / 0.13 | 18-23% |
| **all** | **0.23-0.27** | **0.37-0.39** | **0.15-0.16** | |

**The most important property is not the disagreement but how flat both policies are.** Mean action entropy is 2.6-2.7 nats (a uniform choice over 90 actions is 4.5) and the top action carries only 0.21-0.27 of the mass (0.11-0.16 in the air, 0.30-0.38 at kickoff and near the ball).
Two independent samples from the same teacher agree only 15-19% of the time. So:
- **The pre-registered H-I falsifier fires as written.** H-I predicted argmax agreement of 55-85% and called a figure below 40% "never mix". Measured: 0.23-0.27. I am recording that as a prediction that failed. The reading below that argmax agreement is a weak yardstick for flat policies is a post-hoc reinterpretation and is labelled as such.
- A fairer yardstick is overlap relative to a teacher's overlap with itself: the cosine of the two probability vectors is `sampleAgree / sqrt(selfP x selfO)` = 0.094 / sqrt(0.167 x 0.147) = **0.60** (Chronos acting; 0.109 / sqrt(0.146 x 0.193) = 0.65 for Galactus acting). The teachers share most of their probability mass structure but not all of it: TV of about 0.38 means that if you moved one distribution onto the other, 38% of the mass would have to move.
- **They differ most at kickoff** (TV 0.50-0.55, JS 0.23-0.27), consistent with the first-touch asymmetry in section 11 (Galactus gets the first touch against Chronos in 1024 of 1024 games), and least, in JS, in the air, where both are almost uniform (entropy about 3.1-3.2).
- JS bins (fractions of rows below 0.01 / 0.05 / 0.15 / 0.3 / 0.5 / above): 0.09 / 0.28 / 0.29 / 0.17 / 0.10 / 0.07. About 17% of states differ strongly (JS above 0.3) and 9% are near-identical.

**What it changes in the plan**
- **Blending is not safe by default.** The teachers are different policies of equal strength with only about 60% distributional overlap, and the strongest divergence is at the kickoff, where the plan commits to a line. The ensemble variant (E10 variant 3) is therefore lowest priority; separate students (variant 1) and the teacher-id student (variant 2) stay first. H-K's prediction (the ensemble student does not beat the better single clone) is now more likely than not, but it is still untested.
- **Metrics for E1 must account for flatness.** Top-1 agreement against a teacher with a top-1 mass of 0.24 is a noisy target: small logit errors flip the argmax. E1 reports KL/JS/TV and cosine alongside agreement, and the student is played both ways (argmax, as the teacher is shipped; and sampled), because H2H parity may need the clone's logits to reproduce **argmax choices**, not only the distribution. The margin between the teacher's top-1 and top-2 probabilities has not been measured; it should be added to E0's logit dump, since it sets how precisely logits must be matched.
- **The teachers' own obs matter for the unlabelled half of the question.** Both bots act from different observations (225 mirrored vs 258 with ball prediction) on the same state, so a TV of 0.38 mixes "different policies" with "different information". This experiment cannot separate them; E4(a) (a student on our obs) and E10 variant 2 would.

Caveats: single seed (777001) and one set of games (the four sets agree with each other); the situation split is coarse (four classes from position and ground contact only); states are those the teachers themselves visit, which is the right set for cloning but not the one DAgger would add.
