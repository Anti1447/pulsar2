# GCO pure self-play arm (pre-registration draft, 2026-11-18)

Status: **DECISIONS MADE (section 9); implementation in progress, nothing run.** Branch `gco-goalcritic`. Doctrine: measurement before machinery; one lever; pre-registered criteria;
an automatic guard; a revert path. User direction (2026-11-18): pure self-play for training, with old versions and external bots used ONLY for skill
metrics; test it as an arm of the "fresh base" comparison, after the critic-context Phase A (`GCO_CRITIC_CONTEXT_TEST.md`) reports.

## 1. Question

Does removing every non-self opponent from training (old versions, imported exploiters, the four external bots) improve learning of the goal-only
objective, measured against the same frozen bots used as a yardstick?

## 2. Why it might help, why it might not (stated before measuring)

For: (a) one opponent distribution, so the critic has no source heterogeneity to model (EV by source ranges about +0.3 to -0.7 in medians; Phase A measures how
much of the team-mode EV gap that explains); (b) the lookahead search gained +0.27 against our own policy where its opponent model is exact, and lost against bots
until the model was exact (`GCO_LOOKAHEAD_TEST.md` section 14, `GCO_SEARCH_DISTILL_ORACLE_TEST.md` sections 8-9); (c) no archive to maintain, AlphaZero-style.
Against: (a) nontransitivity and cycling are measured in this repo (the 4.0 lineage plateaued dead-even against itself); (b) the bots were added deliberately to force the
aerials that self-play never generates (`ExampleMain.cpp`, external opponent comment); (c) the raw policy scores only 0.26-0.33 against GalactusV2, ChronosV2 and Dice,
so those bots are stronger than the current policy and carry a signal self-play may never produce.

## 3. Design

Two arms, everything else identical (same code, seed policy, reward, net, learning rates, steps per iteration):

| Arm | `trainAgainstOldVersions` | external bots served in training | imported exploiters | opponent in training |
|---|---|---|---|---|
| A (mixed, the current GCO config) | on | `serveFrac` 0.20 (four bots, 5% each) | off (GCO) | self-play, old versions, bots |
| B (pure self-play) | **off** | **serveFrac 0** | off | self-play only |

The version ring, the reference set and the external bots stay LOADED in arm B for the metrics: `Rating/1v1`, `Ref/Oldest Share` and the gauntlet
(`GAUNTLET_LOG.jsonl`, deterministic games against each bot) keep running, and none of them touches training.

**Primary metric:** gauntlet score against each of GalactusV2, ChronosV2, Dice and Nexto, per mode available, at matched total timesteps, read as the mean over the last
three gauntlet evaluations (games per evaluation as configured; report the series, not one point). **Secondary:** `Ref/Oldest Share` (fixed opponent), goals per 10k rows
by mode, `GCO/EV` per mode (arm B has no source mix, so compare pooled and per-mode EV, not per source), policy entropy and `Plasticity/*` for collapse.

## 4. Open decisions (need the user)

1. **Starting point.** "Fresh base" is not defined yet. Options: (i) a true fresh initialisation under the goal-only reward (clean, but the acquisition-wall risk is
   real: the policy may never leave random play, and neither arm would then say anything about opponents); (ii) fork BOTH arms from the same GCO checkpoint (matched
   start, tests "does pure self-play continue better than mixed from here", which is not a fresh-base test); (iii) a fresh run on a known easier base (shaped rewards at
   start, annealed to goal-only), which adds a second variable. I would pick (ii) first because it is interpretable at this compute.
2. **Compute.** One GPU, one live run: the two arms cannot run concurrently here. Sequential arms confound time (the baseline drifts). Options: rented compute, or run B in the
   slot of the live trainer for a fixed budget and compare against A's same-timestep span from its own history (an inferior, non-concurrent comparison).
3. **Budget and seeds.** One seed per arm cannot separate an arm effect from run-to-run variance. Minimum honest design: two seeds per arm, or a predeclared effect size
   large enough to read from one (state it before running).

## 5. Pre-registered predictions

| Measure | H-pure helps | H-mixed needed |
|---|---|---|
| Gauntlet score vs GalactusV2/ChronosV2/Dice, matched steps | B >= A | B <= A - 0.05 on at least two bots |
| Critic EV (pooled and per mode) | B > A by >= 0.03 | within +-0.02 |
| `Ref/Oldest Share` | B >= A | B < A |
| Entropy, `Plasticity/Policy Dead Units` | within A's band | entropy below half of A, or dead units rising (collapse) |

A difference needs to clear the run-to-run spread; with one seed per arm only a gap well beyond the noise of the gauntlet series counts, and it is reported as a single-seed
result.

## 6. Guards and revert

- Guard: if arm B's entropy falls below half of arm A's at the same step, or its gauntlet scores fall for three consecutive evaluations, stop and report (nothing automatic exists in
  this run, so this is a manual check each evaluation).
- No hybrid checkpoints (recovery doctrine): each arm resumes only from its own full checkpoint.
- Revert: set `trainAgainstOldVersions` back on and `serveFrac` to 0.20; the config is code in `ExampleMain.cpp`, so a rebuild and restart applies it.

## 7. Known unknowns

- Which config lines pull the opponent mix (found: `trainAgainstOldVersions` near line 1579, `externalOpponent.serveFrac` near line 1869, `exploiterPool.*` near 782); the exact
  edit and its interaction with the GCO fleet flags (`gcoFleet`) are not made.
- Whether `Ref/Oldest Share` and the gauntlet stay valid when training never samples old versions or bots (they are read-only; confirm on the first evaluation).
- The Phase A result may change the weight on (a) in section 2.

## 8. Record (fill in)

| Item | Value |
|---|---|
| Starting point chosen | |
| Compute plan | |
| Arm A / arm B step budget and seeds | |
| Gauntlet series by bot | |
| Controls (entropy, dead units, EV) | |
| Verdict | |

## 9. Decisions (user, 2026-11-18) and the spec they imply

User direction: **a TRUE fresh start**, 1v1 only, goal-only reward, **pure self-play first** (arm B). This replaces the matched-checkpoint fork preferred in section 4.

| Item | Decision |
|---|---|
| Start | Fresh random initialisation, new checkpoint folder (name: `checkpoints_gco_fresh`), no seeding from any main/GCO checkpoint |
| Modes | 1v1 only: all 1024 arenas 1v1, skill tracker and gauntlet 1v1 |
| Reward | `GoalReward` +1 scored / -1 conceded, plus -0.5 to both cars on a 20 s no-touch timeout (a real ending, no bootstrap): GCO as it stands |
| Opponents in training | Pure self-play: `trainAgainstOldVersions` off, external `serveFrac` 0, no exploiter pool. Old versions, references and the four bots stay loaded for the skill tracker and the gauntlet ONLY |
| Resets | GCO's fixed non-adaptive mix with air drill 20% -> 10% and air play 20% -> 10%, and the freed 20% to a NEW "ball and players near goal" setter: ball near car 20%, kickoff 20%, random 20%, near-goal 20% (new), air drill 10%, air play 10% (sums to 100%). Corrected after reading `MakeEnv`: the live mix is five slices at 20% each (older prose in `docs/GCO.md` quoting 30/20/20/20/10 is stale) |
| Critics | V_real (gamma 0.9969) + the long-horizon goal critic (gamma 0.9994, beta 0.25) + the critic-context heads (measurement only; in pure self-play the source one-hot is constant, so only the clock features carry information). V-dagger, geo, gap sensor, reachability OFF |
| Policy LR | Fixed 1.5e-4 from the start (no EV gate, no ramp); critic LR 1.5e-4 |
| Entropy | 0.035 (main's value) |
| Network | The live GCO net: shared head 2x1152, policy 4x1024, critic trunk 2x1280, GELU + LayerNorm + residual, 278-dim obs (ball prediction) |
| Compute | Stop the live GCO run AFTER the Phase A window reads, save a clean checkpoint, then run the fresh run on that GPU. The live run can resume later from its checkpoint |
| Budget | One seed, about 3B steps, stop-and-review at 500M, 1B, 2B and 3B (gauntlet vs the four bots, entropy, EV, goals per 10k rows, no-touch timeout share) |

**Assumed defaults (not asked; change if wrong):** tickSkip 8 / actionDelay 7, gamma 0.9969, lambda 0.95, 2 epochs, the live run's iteration and batch size, `tsPerSave` 25M, 1024 arenas,
`GGL_CRITIC_CTX=1` in the environment, gauntlet and skill tracker as configured (1v1 only here).

**Deferred (user request):** test the V-dagger / composition critic on this fresh-start GCO base at a later point, NOT in this experiment (it changes the training signal;
this run is the clean baseline). Also recorded in the project memory.

**Provisional first-run criteria (guesses, no fresh-run baseline exists; revise at the first review).** Leaving random play: by 500M steps `Goals/10k Rows 1v1` sustained above 0
and the no-touch timeout share falling; by 1B steps a nonzero gauntlet score against Nexto. If at 1B steps goals per 10k rows are still near 0 and timeouts are not falling,
the acquisition wall holds for this reward/reset set: the response is a separate one-lever change (more near-goal/near-ball reset weight), not a quiet tuning.

**Known risks / overlap.** Another session has uncommitted edits to the reset setters (`AirDrillState.h`, `AirPlayState.h`, `BallNearCarState.h`, `StateSetter.h`; `docs/GCO_RESET_MIX.md`)
and to `ExampleMain.cpp`. The new setter goes in its own header and the fresh mode in new code behind a new flag, so that work is not overwritten, but the reset mix lives in the
same function (`MakeEnv`); coordinate before merging.
