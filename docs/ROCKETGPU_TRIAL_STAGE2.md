# RocketGPU trial, stage 2 report (2026-11-25)

Plan: [ROCKETGPU_TRIAL_PLAN.md](ROCKETGPU_TRIAL_PLAN.md). Stage 1: [ROCKETGPU_TRIAL_STAGE1.md](ROCKETGPU_TRIAL_STAGE1.md). Code: `~/pulsar_rg`.

## Gate B1: passed

Reference: Pulsar3's own `EnvSet` playing 1v1 with a Pulsar3 policy (checkpoint 4200187904), 1,280,000 arena-decisions (2,560,000 player rows), with some
random held actions and forced demolitions.

| Requirement | Result |
|---|---|
| State-based terms within 0.001 on at least 99.9% of rows | every term: 100% of rows, and 100% of the rows where it fires; largest difference 0.0000018 |
| Event-based terms fire on the same events at least 99% of the time | Demo, OffensiveDemo, OpposedSave, VerifiedShot, GoalSpeed, Goal: 100% |
| Events the port derives itself | touches 20,859, goals 1,392, saves 251, shots 1,450, verified shots 1,436: all identical |

One mismatch was found and fixed: Pulsar3's 4 s goal-credit and 2 s pass windows are one tick shorter (479 and 239 ticks) because its tick rate is computed in single precision.

**Not exercised:** KickoffRace never fires (see below) and TeammateSpacing is always zero in 1v1, so both agree only at zero.

## Start states, endings, events on the new engine

| Check | Result |
|---|---|
| Five start types, 40,000 draws each against Pulsar3's setters, 58 values per type | every distribution within the 0.1% two-sample level (largest at 0.95 of it); mix 20% each |
| Written states read back from the engine, then one decision | exact read-back; median car position within 0.5 uu of the straight-line path |
| Forced demolitions in RocketGPU | 256 of 256 counted as events |
| 20 s no-touch cutoff | truncates on decision 600 and 1,200, nowhere else; single-precision count of Pulsar3's rule also gives 600 |
| Training run through RocketChad3 with the full task | 786,432 actions, none outside the mask; finite losses |

## Pulsar3's own weights playing the trial environment

| Per 1,000 arena-decisions | Trial | Pulsar3 |
|---|---|---|
| Touches | 18.3 | 16.3 |
| Goals | 1.35 | 1.19 |
| Shots | 1.33 | 1.13 |
| Saves | 0.215 | 0.196 |
| Airborne share | 0.484 | 0.479 |
| Mean boost (0 to 1) | 0.233 | 0.215 |

The bot plays much the same on both engines. Most reward terms have similar mean size; TeamPressure (0.18 vs 0.22) and TooFar (0.11 vs 0.14) are lower in the trial.
Part of that may be the forced demolition setups in the recording, which move cars around; this was not separated. Demolitions are not comparable for the same reason.
This is a comparison of rates, not a gate.

## Found in the live Pulsar3 run: KickoffRace pays nothing

KickoffRace (weight 60) rewards a contested first touch within **45 decisions**. That was 3 s at the old 15 Hz; at Pulsar3's 30 Hz it is 1.5 s.
In the recording, the earliest kickoff first touch was on decision 80 (median 105; 248 kickoffs). In the live run's metrics it is non-zero in 1 of 1,161 recorded
iterations (about 0.0001).

**Update 10:18:** removed from the live run (user-directed, at 4,345,137,152 timesteps) and from the trial. The reward check was re-recorded and
re-run with the 23-term stack: gate B1 still passes with every term and derived event matching. The tables above describe the 24-term run.

## For stage 3

- **Speed is the main risk.** With Pulsar3 sharing the GPU the training run managed about 7,000 to 16,000 player steps/s at 2,048 arenas, and the environment alone about
  14,000 (1,024 arenas) and 41,000 (4,096). The gate needs about twice Pulsar3's rate. These timings are not trustworthy with the GPU shared, but ball prediction
  looks like the largest single cost, and the trainer computes an extra observation every decision because the no-touch rule makes timeouts irregular.
- **Reward scale.** RocketChad3 does not normalize returns; the value loss was about 70 to 110 with these rewards. Pulsar3's return normalization has to be matched.
- Still to match from stage 1: Pulsar3's advantage filter, its Muon optimizer, entropy scaling.
