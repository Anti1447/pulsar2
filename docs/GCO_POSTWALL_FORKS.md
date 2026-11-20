# GCO post-wall fork arms: control, kickoff weight, opponent exposure (pre-registration DRAFT, 2026-11-20)

Status: **ARMED (2026-11-20), user-directed: the chain starts automatically when fresh run 3 passes 1B steps** (`docs/GCO_FRESH3.md`). Implemented and built; nothing has run yet.
Branch `gco-goalcritic`. One GPU, so the arms run one after another: run 3 is stopped cleanly at 1B, then ctrl, kick, opp each run +500M steps.

## 1. Why this, and what changed in the picture

Runs 1, 2 and 3 (`docs/GCO_PURE_SELFPLAY_ARM.md`, `GCO_FRESH2.md`, `GCO_FRESH3.md`) all get past the acquisition wall (run 1 at about 620-720M steps, run 2 at about 760-860M); the
wall is no longer the bottleneck. The open question is the rate of skill gain AFTER the wall. Run 1 (single seed, 5B steps) is the only long record, and reading it again with the
500M-window means shows it is closer to flat than I reported earlier:

| Run 1 window | Gauntlet mean win % | Gauntlet mean first-touch % | Nexto win % | Goals / 10k | Touch ratio |
|---|---|---|---|---|---|
| 3.0-3.5B | 0.95 | 8.5 | 1.8 | 6.10 | 0.01356 |
| 3.5-4.0B | 1.58 | 4.6 | 4.2 | 7.33 | 0.01537 |
| 4.0-4.5B | 0.79 | 8.2 | 1.6 | 6.53 | 0.01405 |
| 4.5-5.0B | 1.65 | 10.8 | 3.8 | 6.34 | 0.01420 |

100M-window means of the gauntlet mean win % from 3.0B to 5.0B: 0.99, 0.73, 0.89, 0.89, 1.25, 1.77, 1.77, 1.20, 1.51, 1.67, 1.20, 1.20, 0.57, 0.36, 0.62, 1.25, 1.61, 2.66, 1.41, 1.30
(range 0.36-2.66; the standard deviation of the 500M-window means across 3.5-5.0B is about 0.45 points). So by 3.5-5.0B run 1 is roughly flat at 1-1.6% with an oscillation of about +-0.5
points; the "still rising" reading I gave from 2.0 to 4.0B was driven by the early windows. By the section-14 rule of `GCO_PURE_SELFPLAY_ARM.md` the 4.5-5.0B window versus 4.0-4.5B
passes all three plateau tests (win +0.85 points, goals/10k -3%, touch ratio +1%), but the window before it did not (goals -11%, touch -9%), and the rule needs two consecutive windows.
The run is plateau-adjacent, not called.

Consequence for design: the baseline is NOISY and not a clean rising line, so a fork arm cannot be judged against "run 1's slope". Every arm needs a CONTROL from the same checkpoint.

## 2. Common setup

- Start: run 1's final checkpoint, `build/checkpoints_gco_fresh/4995887104` (step 4,995,887,104). Each arm gets its OWN folder holding a FULL copy (network, all optimizers, running stats and the
  `policy_versions` ring and references, about 1.05 GB each); run 1's folder is never written to. No hybrid checkpoints (recovery doctrine).
- Length: 500M steps per arm (to about 5.5B), about 2.1-2.3 h each at the no-twins throughput (about 64k steps/s); three arms about 6.5-7 h.
- Everything not named below equals run 1's configuration at its stop: 1v1 pure self-play, goal +-1 plus the -0.5 no-touch timeout, entropy 0.035, policy LR 1.5e-4, batch 400k, minibatch 25k,
  2 epochs, 1024 games, goal critic on, twins off, and the reset mix of run 1's stop: BallNearCar 0.19, AirDrill 0.09, AirPlay 0.09, Kickoff 0.19, Random 0.14, NearGoal 0.20,
  far NearGoal 0.10.
- Metrics and gauntlet unchanged (gauntlet: 96 kickoff games per bot every 50 iterations, seed 20261105, four frozen bots; held out from training in every arm).

## 3. Arms

**C. Control.** Same checkpoint, same config, nothing changed. It measures the resume effect plus seed noise over 500M steps, which is the yardstick for the other two.

**K. Kickoff weight.** Kickoff 0.19 -> 0.35; every other slice scaled by 0.65/0.81 and rounded: BallNearCar 0.16, AirDrill 0.07, AirPlay 0.07, Random 0.11, NearGoal 0.16, far NearGoal 0.08 (sum 1.00).
Hypothesis: the bot's kickoff play is the weak link behind the gauntlet (every gauntlet game starts at a kickoff; first-touch is 4.6-10.8% in run 1). The prior evidence is from the older
GCO lineage (`docs/GCO_RESET_MIX.md`: the kickoff phase was 1.1% of training rows against 9.2% of match time, 7-9x under; it was also the least informative start for the critic: mean |V(s0)| 0.04-0.06).
It has NOT been re-measured on the fresh runs; the fresh runs' kickoff row share should be read from the time-to-end panels before the arm starts.

**O. Opponent exposure (old versions only).** `trainAgainstOldVersions = true`, `trainAgainstOldChance = 0.30` (the main run's value), `externalOpponent.serveFrac = 0`; the opponent for one whole
iteration is drawn uniformly from the version ring, otherwise self-play. Hypothesis: pure self-play is what flattens the curve (it only ever plays its current style); a ring of past selves adds diversity.
Design choices stated up front:
- The four gauntlet bots are NOT used for training here: serving them would turn the gauntlet from a held-out measure into a training distribution, and the gauntlet is the only real-skill
  metric we have. (A Nexto-served arm is a possible later arm only with a different, still-held-out measure.)
- Known weakness: at 5B the ring spans only the last about 800M steps of near-identical selves (`CLAUDE.md`: pool myopia accepted). A small effect is the expected outcome; a larger one
  would be informative. The log-spaced reference set is never trained against and stays that way here.

## 4. Pre-registered measurements and decision rules

Primary (each arm, last 250M steps of its 500M, and the whole 500M window, against the control arm's same windows): gauntlet mean win %, and per-bot win % (Nexto is the only bot with
regular wins). Secondary: gauntlet mean first-touch %, goals per 10k rows, touch ratio, aerial touch ratio, entropy, critic EV, steps per second.

Noise: from run 1 the 500M-window SD of the gauntlet mean win % is about 0.45 points and first-touch window means swing 4.6-10.8%. So:
- An effect on the gauntlet is REAL only if the arm exceeds the control by at least **1.0 point** of gauntlet mean win % over the 500M window AND by at least 0.5 points over the last 250M.
- For arm K only, the targeted measure is gauntlet first-touch: real if at least **+5 points** above the control over the 500M window.
- Anything smaller is reported as inconclusive, not as a trend. Goals per 10k rows and touch ratio are NOT skill measures in arm K (the mix moves them); read them only as guards.
- If the control itself moves by more than 1.0 point from run 1's 4.5-5.0B window (1.65%), say so: the resume is not neutral and every arm comparison is made against the control, not run 1.

Guards (manual, each 100M): stop and report if entropy leaves 0.56 +-30%, the gauntlet mean win falls three evaluations in a row to below half of the control's, a crash loop or OOM, or
(arm O) the ring cannot serve opponents. Each arm resumes only from its own full checkpoint.

## 5. Implementation (done 2026-11-20)

- `ExampleMain.cpp`: flags `--gco-fork-ctrl`, `--gco-fork-kick`, `--gco-fork-opp`, folder `checkpoints_gco_fork_<arm>`, W&B run name `gco-fork-<arm>`, all built on `ConfigureGCOFresh`
  (1v1, goal-only, twins off) via `ConfigureGCOFork`. ctrl and opp use run 1's final reset mix (the default fresh branch: 0.19/0.09/0.09/0.19/0.14/0.20/0.10); kick uses the section-3 mix (a separate
  block, sums to 1.00). Arm O sets `trainAgainstOldVersions = true`, `trainAgainstOldChance = 0.30`, `externalOpponent.serveFrac = 0`.
- The three folders were prepared from run 1's final checkpoint: `checkpoints_gco_fork_{ctrl,kick,opp}/4995887104` plus a full copy of `policy_versions` (1.1 GB each). The ONLY edit to the copied
  checkpoints is deleting the `run_id` key from `RUNNING_STATS.json` (it holds the W&B run id; left in place each arm would resume run 1's W&B run and mix the curves). Weights, optimizers and running stats are
  byte-identical to run 1's (checked on `POLICY.lt`). Run 1's folder is untouched. `metrics.jsonl` was NOT copied, so each arm's log starts at 4,995,887,104 and holds only its own 500M steps.
- Chain: `systemd --user` unit `gco-forks-chain` (script `tools/gco_health/chain_forks.sh`, independent of any Claude session). It waits for fresh3 >= 1,000,000,000 steps, stops it cleanly, then for each arm in the order
  ctrl, kick, opp: starts it, checks the boot log shows `Loading checkpoint "checkpoints_gco_fork_<arm>/4995887104"` (otherwise ABORTS and starts nothing further), writes a health read every 100M steps, runs to
  4,995,887,104 + 500,000,000 = 5,495,887,104, stops it cleanly, and starts the next. Status: `build/forks_status.log`; health reads (arm vs control vs run 1 at 4.5-5.0B): `build/forks_health.log`.
  It aborts (rather than starts anything) if a trainer dies or will not stop. Stop the whole thing with `systemctl --user stop gco-forks-chain` (the running arm's trainer keeps going until stopped separately).
- NOT automated: the guards of section 4 are read by hand from `forks_health.log`; the chain does not stop an arm on a guard.
- Not tested before the first arm boots: no second trainer can run beside run 3 on the one GPU, so a bad flag would surface at 1B as an ABORT line in the status log with no trainer running.

## 5b. Pre-registered decision table (written 2026-11-20, BEFORE any arm has run)

Read after all arms finish (or after each, for the control). "Margin" is section 4's: 1.0 point of gauntlet mean win % over the 500M window AND 0.5 over the last 250M versus the control;
for arm K's first-touch measure, +5 points. All verdicts are single-seed.

| Observation | Reading | Next step (decided now) |
|---|---|---|
| Control's 500M gauntlet mean win differs from run 1's 4.5-5.0B window (1.65%) by more than 1.0 point | The resume is not neutral (or noise is bigger than 0.45 points) | Stop. Do not read the arms against run 1. Investigate the resume (optimizer state, version ring, running stats) before any further arm; compare arms against the control only. |
| Control stays within about 1.0-1.6% and neither K nor O clears the margin | Run 1's plateau is real; kickoff weight and old-version exposure do not move it | Report INCONCLUSIVE-to-NULL for both. Next: the section-14 decision menu of `GCO_PURE_SELFPLAY_ARM.md` (2v2/3v3, resume run 2 for an aerial-touch comparison, V-dagger, or a restart), chosen with the user. Do not stack more small mix tweaks. |
| K clears the first-touch margin (+5 points) but not the gauntlet win margin | Kickoff play improves but does not convert into goals on this gauntlet | Do not adopt K as a win. Look at what happens after the first touch (post-kickoff possession) before another kickoff run; consider a longer K run only if first-touch keeps rising. |
| K clears the gauntlet-win margin | Kickoff weight helps | Adopt Kickoff 0.35 as the working mix for later arms; next test is whether more (0.50) helps further, one lever, from the same checkpoints. |
| O clears the margin | Pure self-play myopia is real | Next arm: larger exposure (chance 0.50 and/or draw from the log-spaced reference set as well), still gauntlet-held-out; compare against this arm, not the control. |
| O is worse than the control by more than the margin | Old-version opponents hurt (stale styles dilute the signal) | Revert; record. Do not retry with the same ring. |
| Any arm worse than the control by more than the margin | The lever hurts | Revert, record, and do not combine it with another lever. |
| Both K and O clear the margin | Two independent helps | One combined arm (K+O) against the better single arm before adopting both. |
| All three arms and run 1's late windows overlap | Gauntlet noise dominates | State that the measure cannot resolve effects below about 1 point at 500M; the next arms need either 1B steps or more gauntlet games per evaluation (96 per bot now) before any more lever tests. |

Rules that apply to every row: only the control is a valid comparison for the arms (never run 1's slope); first-touch and goals per 10k rows are not skill measures for arm K; a result inside the margin is
reported as inconclusive and not as a trend; no new lever is added to an arm mid-run.

## 6. Order and cost

Run 3 finishes first (about 2.5 h from 01:25 on 2026-11-20 at 64k steps/s). Then C, K, O in that order, about 2.1-2.3 h each: the control first so a broken resume is found before spending on the other two. Total about 6.5-7 h
after run 3. If the control shows the gauntlet is simply oscillating at 1-1.6%, a single arm's result needs the 1.0-point margin above to count.

## 7. Open questions for the user

1. Is 500M steps per arm enough? Three arms of 1B would be about 14 h; the margin above is built for 500M.
2. Arm K's kickoff share (0.35) and arm O's old-version chance (0.30) are my picks.
3. Should the control be dropped to save about 2 h? I advise against it, for the reason in section 1.

## 8. Record (fill in)

| Item | C | K | O |
|---|---|---|---|
| Start step / time | | | |
| Gauntlet mean win, 500M and last 250M | | | |
| Nexto win % | | | |
| Gauntlet first-touch | | | |
| Guards | | | |
| Verdict | | | |
