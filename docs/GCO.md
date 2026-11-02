# GCO — goal-concede-only main run (2026-11-01)

An exploiter's training recipe on a main run. It starts from an existing main checkpoint and trains on goals only,
like an exploiter (docs/EXPLOITERS.md), but instead of beating a frozen target it **becomes the new main policy**:
self-play plus everything main plays against.

| | Main (`checkpoints_5.3`) | Exploiter | GCO (`checkpoints_gco`) |
|---|---|---|---|
| Starting weights | its own lineage | a main snapshot (or an older exploiter) | a main checkpoint, copied once |
| Optimizers / return stats | resumed | fresh | fresh |
| Reward | shaped stack | `GoalReward` only (+1 scored, −1 conceded) | `GoalReward`, plus −0.5 to every car on a no-touch timeout |
| Resets | mixed drills | kickoffs only | main's mixed drills (kickoffs only until 12.81B) |
| 20s no-touch timeout | truncation | truncation | a real ending (no bootstrap) |
| Fleet | phase marker | always PHASE B (1/3 each 1v1/2v2/3v3) | 1/3 each (6/12, 4/12, 2/12 from 14.76B to 14.8B) |
| Skill tracker | 16 games | – | 32 games in the fleet's split (10 / 11 / 11) |
| Best-rated archives | top 2 per mode | – | top 2 per mode (`best_r`, `best2v2_r`, `best3v3_r`) |
| Entropy | `cfg.ppo.entropyScale` (0.035) | 0.4× main's (0.014) | 0.4× main's (0.014) |
| Policy LR | full | 10% until critic EV ≥ 0.5, then ramps to 100% over 100M | 10%, then **gated steps** of 4.5% (see below) |
| Auxiliary learners (reach, goal critic, V-dagger, geo, gap sensor) | on | off | off |
| Opponents | self-play, old versions, Nexto, imported exploiters | its frozen target(s) | **same as main** |
| Skill tracker / Elo, policy-version archive, best-rated saves | on | off | on |
| Ends | never | 0.70 eval or step cap | never |

**Resets and the timeout penalty (from 12.81B steps, 2026-11-01).** GCO started on kickoff-only resets. It now uses
main's mix: ball-near-car 30%, aerial drill 20%, air play 15%, kickoff 10% and random 25%. The 20-second no-touch
timeout ends the episode as a real outcome instead of a truncation, and every car on both teams gets −0.5
(`GCO_TIMEOUT_PENALTY` in `src/ExampleMain.cpp`, `NoTouchTimeoutPenalty` in `CommonRewards.h`). As a truncation, the
critic's bootstrapped value hid what a dead ball cost, so neither team had a reason to end the stall.

## Running it

From the build directory:

```bash
../tools/run_trainer.sh -- ./GigaLearnBot --gco                       # alone: all 1024 games
../tools/run_league.sh start --gco                                    # with both exploiter slots targeting it
./GigaLearnBot --gco --gco-from checkpoints_5.3/7835941626            # start from a specific main checkpoint
../tools/run_trainer.sh -- ./GigaLearnBot --gco --no-exploiter-pool   # alone, importing no exploiters
```

`--no-exploiter-pool` (also works on a plain main run) turns the exploiter import off, so the opponents are Nexto 15%,
old versions 25.5% and self-play for the rest, as main's were before exploiters.

**First launch (seeding).** If `checkpoints_gco/` has no checkpoint yet, `SeedGCORun` (`src/ExploiterLauncher.cpp`)
seeds one from the newest complete main checkpoint, or from `--gco-from`:

- It copies the network weights (`*.lt`) but no `*_OPTIM.lt`, so the optimizers start fresh.
- It writes a `RUNNING_STATS.json` that keeps only main's step count, iteration count and skill ratings. There's no
  return stat (the return scale is goal-only now), no W&B run ID (it gets its own run, `gco`) and no exploiter pool
  state (it re-admits every finished exploiter).
- It copies main's `policy_versions/` (old versions + rating references), so old-version play covers main's history
  from the first iteration.
- It writes `checkpoints_gco/GCO_SOURCE.txt` with the source checkpoint.

Later launches just resume `checkpoints_gco/`. The step count continues from main's, so the version archive and
rating references stay in order. To restart from scratch, stop the run and move `checkpoints_gco/` away.

**Exploiters under GCO.** `--gco` on an exploiter slot (`run_exploiter.sh --slot N --gco`, or `run_league.sh start
--gco`) takes new snapshots and the league's main versions from `checkpoints_gco`. The `exploiters/` folder is
shared with main's lineage: the GCO run imports main's finished exploiters too. A slot's *active* run still resumes
against its old target, so finish or discard it first if you want the next run to target GCO
(`./GigaLearnBot --finish-exploiter --slot N`).

## Policy LR schedule

The policy LR starts at 10% of `cfg.ppo.policyLR`. Every 25M steps a check reads the **median** critic explained
variance over the iterations since the previous check (per-iteration values swing about 0.2–0.7, with rare large
negative outliers). If the median is above 0.41, the LR goes up one step, 1/20 of the way to 100% (+4.5% of full
LR). Otherwise it stays put. Full LR therefore takes at least 500M steps. The step level and the current window
are saved in `RUNNING_STATS.json`, so restarts keep their place.

History: the first GCO run (2026-11-01, W&B `gco`) used the exploiter's continuous ramp, which starts once EV first
reaches 0.4 and reaches 100% within 100M steps. Rating/1v1 rose from 1257 to 1287 by 7.925B, just before full LR,
then slid to 1231 by 8.09B at 100%. The run was rolled back to `best_r1287` (7.925B) and restarted on the gated
schedule from 10% (W&B `gco-gated-lr`). The rolled-back checkpoints are in `checkpoints_gco/rolled_back/`. The gate
started at a 0.5 median; after 178 checks (to 12.43B steps) with window medians of 0.35–0.38 and no step-up, it was
lowered to 0.41. Rating/1v1 had still risen from 1256 to about 1528 at 10% LR by then.

## What to watch

The GCO dashboard (`tools/dashboard_gco/run_dashboard.sh`, or `RUN_DASHBOARD.bat` on Windows; http://localhost:8767)
is a copy of the main dashboard that reads only `--gco` logs and `checkpoints_gco`, starts `--gco --no-exploiter-pool`,
and replaces the Phase B card with the LR schedule: step level, next check, and every logged check.

- `GCO/LR Step` (0–20), `GCO/Policy LR` and `GCO/Window EV Median`; each 25M-step check is also a
  `Policy-LR check:` line in the log.
- `Rating/*` (Elo vs the same reference battery main uses), `Ref/Oldest Share`, the Nexto slope, and
  `Exploiters/Held-out Main Win Rate`. These numbers are directly comparable with main's `5.3-geo` run.

Code: the `gcoRun` flag in `LearnerConfig.h` (the LR ramp in `Learner.cpp`), `ConfigureGCO` and the `--gco` /
`--gco-from` options in `src/ExampleMain.cpp`, `SeedGCORun` in `src/ExploiterLauncher.cpp`.
