# GCO fresh run 2: near-goal-heavy reset mix plus the V-dagger twins (pre-registration, 2026-11-19)

Branch `gco-goalcritic`. Launch: `cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco-fresh2`. Checkpoints: `build/checkpoints_gco_fresh2`
(its own folder; the first fresh run's `checkpoints_gco_fresh` is untouched and resumable with `--gco-fresh`). W&B / metrics run name `gco-fresh2`.

## 1. Why

Run 1 (`docs/GCO_PURE_SELFPLAY_ARM.md`, `--gco-fresh`) broke the acquisition wall between 500M and 1B steps from a goal-only reward, then climbed slowly:
4.996B steps at stop, gauntlet mean win about 1.3-1.6% (Nexto about 5%), ball-touch ratio 0.0144, goals per 10k rows about 6. Its final checkpoint is packaged as
PulsarGCO v1.0.2 (step 4,995,887,104) and backed up in `build/checkpoints_gco_fresh_FINAL_4995887104_BACKUP`. Two hypotheses are tested together, by user direction:
(1) a larger near-goal share from step 0 gets past the wall faster; (2) the V-dagger composition critic (deferred earlier, memory note) helps on a clean GCO base.

## 2. Configuration (everything else identical to run 1)

- Reset mix (fixed, non-adaptive): NearGoalState 0.30, NearGoalState(1600, 3200) 0.20, then 0.10 each: BallNearCar(600,900), AirDrill, AirPlay, Kickoff, Random.
  (Run 1 launched at 0.20/0.20/0.10/0.10/0.20/0.20 for BallNearCar/NearGoal/AirDrill/AirPlay/Kickoff/Random with no far slice, and was later 0.19/0.20/0.09/0.09/0.19/0.14 plus
  0.10 far.)
- V-dagger twins ON from step 0 on the shared trunk: `vdagEnabled`, tau 0.75, seek beta 0.15 (the paper's deployed value; the config default is 0.30), Phi = +H with
  H = relu(min(V1,V2) - V_real). gapSensor (V_exp), reachability, geo stay off. Goal critic stays on (gamma 0.9994, beta 0.25).
- 1v1 only, pure self-play, goal +-1 plus the -0.5 no-touch timeout, entropy 0.035, policy LR 1.5e-4 fixed, batch 400k rows, miniBatch 25k, 2 epochs, 1024 games.

## 3. Confound (stated up front)

The doctrine is one lever at a time. This run changes TWO (mix and twins) against run 1, so a difference between the runs cannot be attributed to either alone. It can
only answer "does this combination get past the wall and climb at least as fast". If it does, the ablation arms are the follow-up (mix-only, twins-only), each about 1B steps
(4-5 hours). If it does worse, the same applies. Single seed.

## 4. Pre-registered comparison (run 1 at the same step is the baseline)

Run 1 reference: ball-touch ratio 0.0006 first 10 iterations, 0.0021 at 500M, 0.0092 at 1B; goals per 10k 2.5, 2.7, 5.1; episode length 290, 303, 944 (the no-touch timeout
penalty went to 0 between 500M and 1B); gauntlet first-touch 0, 1.2%, 7-10%; first gauntlet win after 1B (Nexto).
- **Better:** the wall breaks (no-touch penalty reaches 0 and episode length above 600) by 500M steps, or touch ratio above 0.004 at 500M.
- **Same:** within the run-1 band at each 100M check.
- **Worse:** touch ratio below half of run 1's at 500M AND 1B.

## 5. Health checks every 100M steps, and guards

Each check prints: steps, touch ratio, aerial touch, in-air ratio, speed to ball, goals per 10k, episode length, no-touch penalty, entropy, EV, policy update magnitude,
gauntlet win / first touch per bot, plus the twin panels `Headroom/Vdag Update Magnitude`, `Headroom/Vdag Mean`, `Headroom/H Mean`, `Headroom/H P90`, `Headroom/Inj Std Ratio`,
`Headroom/Vdag Loss`, and run 1's value at the same step.
- **Stop and report if:** `Headroom/Vdag Update Magnitude` reads 0 (the twins are inert: this exact failure ran for the life of an earlier run), `Headroom/Vdag Mean` or H keeps
  inflating (above about 5 and rising, the paper's ratchet signature), policy entropy leaves 0.77 +- 30% before the wall breaks or +-30% of its own band after, the gauntlet
  falls three evaluations in a row after it is nonzero, OOM or a crash loop.
- Restart only ever resumes from this run's own full checkpoint (recovery doctrine).

## 6. Revert

`--gco-fresh` is unchanged and resumes run 1 from `checkpoints_gco_fresh` (final checkpoint 4,995,887,104). Disabling the twins alone: set `cfg.ppo.vdagEnabled = false` in
`ConfigureGCOFresh2` and rebuild (a fresh start is needed: the twins co-adapt the trunk from step 0).

## 6b. Launch record (2026-11-19)

Run 1 stopped cleanly at 4,995,887,104 steps (iteration 12,407) and packaged as PulsarGCO v1.0.2. The first fresh2 launch (16:14, ~10M steps) was ABORTED after an early check found
`Headroom/Vdag Loss` and `Headroom/Vdag Update Magnitude` missing: in `PPOLearner.cpp` the `if (models["vdag1"])` report block was nested inside `if (reach)` (misleading indentation),
so with reachability off every twin-health panel was invisible, including the only guard against inert twins. Fixed (brace moved), rebuilt, the aborted folder quarantined as
`build/checkpoints_gco_fresh2_ABORTED_panelbug_20261119`, and the run relaunched from scratch at 16:17 (`--gco-fresh2`). First twin panels on the clean launch: update magnitude 0.064
(nonzero), twin loss 0.006, inj std ratio 0.15 (matches beta). Health checks every 100M steps via `scratchpad/fresh2_read.py` (run 1 at the same step alongside).

## 7. Record (fill in)

| Item | Value |
|---|---|
| Start | |
| 100M / 200M / ... health reads | |
| Wall broken at | |
| Twin panels healthy | |
| Verdict | |
