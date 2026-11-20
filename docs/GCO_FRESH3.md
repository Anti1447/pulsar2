# GCO fresh run 3: the no-twins ablation of fresh run 2 (pre-registration, 2026-11-19)

Branch `gco-goalcritic`. Launch: `cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco-fresh3`. Checkpoints: `build/checkpoints_gco_fresh3`. Metrics run `gco-fresh3`.

## 1. Why

Run 2 (`docs/GCO_FRESH2.md`) changed two things against run 1 at once: the near-goal-heavy reset mix and the V-dagger twins. A difference could not be attributed. Run 3 is run 2
with ONLY the twins removed. Then: run 1 -> run 3 isolates the MIX; run 3 -> run 2 isolates the TWINS. Single seed per arm, so seed noise is not measured (section 4).

Run 2 is stopped at 1B steps (about 1.0B) and its folder `checkpoints_gco_fresh2` is KEPT (resumable with `--gco-fresh2`, e.g. for a later aerial comparison, where the twins
are expected to matter more than on the ground-touch wall measured here).

## 2. Configuration

Identical to run 2 (docs/GCO_FRESH2.md section 2): NearGoal 0.30, far NearGoal(1600,3200) 0.20, 0.10 each BallNearCar/AirDrill/AirPlay/Kickoff/Random; 1v1 pure self-play,
goal +-1 plus the -0.5 no-touch timeout, entropy 0.035, policy LR 1.5e-4, batch 400k, minibatch 25k, 2 epochs, 1024 games, goal critic on. Only change: `vdagEnabled = false`.
Expected throughput about 62-65k steps/s (run 2 ran about 49k), so 1B steps takes about 4.3 h.

## 3. Reference points at matched steps (from run 1 and run 2 logs)

| Step | Run 1 touch / episode len | Run 2 touch / episode len |
|---|---|---|
| 500M | 0.0021 / 301 | 0.0019 / 282 |
| 700M | 0.0049 / 786 | 0.0026 / 320 |
| 800M | 0.0075 / 939 | 0.0038 / 592 |
| 900M | 0.0088 / 936 | 0.0072 / 858 |
| 1B | 0.0094 / 965 | (filled in from the 1B read) |

Wall-break step, defined for comparison: the first 100M window whose median episode length exceeds 600 steps AND whose median no-touch penalty is 0.
Measured with a 100M window at 20M stride: run 1 first qualifies in the 620-720M window (median episode length 786, penalty about 0); run 2 in the 760-860M window (median 769, penalty 0). So the run-2 lag
is about 140M steps, INSIDE the 250M margin of section 4, i.e. already within what this rule calls inconclusive.

## 4. Decision rule (stated before the result, with the noise caveat)

Seed noise in the wall-break step is UNMEASURED; run 1 vs run 2 differed by about 100M steps in an apparent lag that mostly closed by 900M. So:
- Call an effect real only if two arms differ in wall-break step by MORE THAN 250M steps AND the gap holds in touch ratio at 1B (more than 25% apart).
- Otherwise the result is INCONCLUSIVE / within the plausible seed spread, and the three runs together are the best estimate of that spread.
- If run 3 is slower than run 1 by more than that margin: the mix (not the twins) is implicated. If run 3 is faster than run 2: the twins slowed the wall. If run 3 is slower than run 2:
  the twins helped. Anything inside the margin is not an effect.
- The twins' intended use (conducts never performed as wholes, e.g. aerials) is NOT what this wall measures; "no difference at 1B" is not evidence against them. Aerial touch ratio at 2B+
  is the later comparison, and needs run 2 resumed.

## 5. Health checks and guards

Every 100M steps, run 3 beside runs 1 and 2 at the same step: touch / aerial / in-air ratios, speed toward ball, goals per 10k, episode length, no-touch penalty, entropy, EV,
update magnitude, gauntlet win and first-touch per bot, steps per second. Stop and report if entropy leaves 0.77 +-30% before the wall breaks, the gauntlet falls three evaluations
in a row after it is nonzero, or a crash loop / OOM. Restart only ever resumes from this run's own full checkpoint.

## 6. Record (fill in)

| Item | Value |
|---|---|
| Start (automatic, chained to run 2's 1B) | |
| Wall-break step run 1 / run 2 / run 3 | |
| Touch ratio at 1B run 1 / 2 / 3 | |
| Verdict (inside or outside the 250M margin) | |
