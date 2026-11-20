# GCO post-wall fork arms: control, kickoff weight, opponent exposure (pre-registration DRAFT, 2026-11-20)

Status: **DRAFT, nothing implemented, nothing run.** Branch `gco-goalcritic`. To be started only after fresh run 3 finishes its 1B steps (`docs/GCO_FRESH3.md`) and the user approves.
One GPU, so the arms run one after another.

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

## 5. Implementation notes (not done)

- New flags in `ExampleMain.cpp`, each its own folder: `--gco-fork-ctrl` -> `checkpoints_gco_fork_ctrl`, `--gco-fork-kick` -> `checkpoints_gco_fork_kick`, `--gco-fork-opp` -> `checkpoints_gco_fork_opp`,
  all built on `ConfigureGCOFresh` (the fresh-run machinery: 1v1, goal-only, twins off) and the same reset mix as run 1's stop for C and O.
- The copy is a plain `cp -a` of `build/checkpoints_gco_fresh/<final step>` plus `policy_versions`, done while run 1 is stopped (it is). Run 1's `metrics.jsonl` should be copied too so the arm's
  log continues the run-1 curve; verify the arm loads `Loading checkpoint ... 4995887104` and not a fresh start.
- Arm O needs only the two config lines of section 3 on top of `ConfigureGCOFresh` (which currently sets both false/0).
- A test or check that the arm's reset mix sums to 1.0 and the Kickoff slice is 0.35.

## 6. Order and cost

Run 3 finishes first (about 2.3 h from now). Then C, K, O in that order, about 2.1-2.3 h each: the control first so a broken resume is found before spending on the other two. Total about 6.5-7 h
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
