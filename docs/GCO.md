# GCO — goal-concede-only main run (2026-11-01)

An exploiter's training recipe on a main run. It starts from an existing main checkpoint and trains on goals only,
like an exploiter (docs/EXPLOITERS.md), but instead of beating a frozen target it **becomes the new main policy**:
self-play plus everything main plays against.

| | Main (`checkpoints_5.3`) | Exploiter | GCO (`checkpoints_gco`) |
|---|---|---|---|
| Starting weights | its own lineage | a main snapshot (or an older exploiter) | a main checkpoint, copied once |
| Optimizers / return stats | resumed | fresh | fresh |
| Reward | shaped stack | `GoalReward` only (+1 scored, −1 conceded) | `GoalReward` only |
| Resets | mixed drills | kickoffs only | kickoffs only |
| Fleet | phase marker | always PHASE B (1/3 each 1v1/2v2/3v3) | always PHASE B |
| Entropy | `cfg.ppo.entropyScale` (0.035) | 0.4× main's (0.014) | 0.4× main's (0.014) |
| Policy LR | full | 10% until critic EV ≥ 0.5, then ramps to 100% over 100M | 10% until critic EV ≥ **0.4**, then the same ramp |
| Auxiliary learners (reach, goal critic, V-dagger, geo, gap sensor) | on | off | off |
| Opponents | self-play, old versions, Nexto, imported exploiters | its frozen target(s) | **same as main** |
| Skill tracker / Elo, policy-version archive, best-rated saves | on | off | on |
| Ends | never | 0.70 eval or step cap | never |

## Running it

From the build directory:

```bash
../tools/run_trainer.sh -- ./GigaLearnBot --gco                       # alone: all 1024 games
../tools/run_league.sh start --gco                                    # with both exploiter slots targeting it
./GigaLearnBot --gco --gco-from checkpoints_5.3/7835941626            # start from a specific main checkpoint
```

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

## What to watch

- `GCO/Critic Explained Variance` reaching 0.4, then `GCO/Policy LR` stepping from 10% to 100% over the next 100M steps.
- `Rating/*` (Elo vs the same reference battery main uses), `Ref/Oldest Share`, the Nexto slope, and
  `Exploiters/Held-out Main Win Rate`. These numbers are directly comparable with main's `5.3-geo` run.

Code: the `gcoRun` flag in `LearnerConfig.h` (the LR ramp in `Learner.cpp`), `ConfigureGCO` and the `--gco` /
`--gco-from` options in `src/ExampleMain.cpp`, `SeedGCORun` in `src/ExploiterLauncher.cpp`.
