# GCO pure self-play arm (pre-registration draft, 2026-11-18)

Status: **IMPLEMENTED and smoke-tested (section 10); the real run is NOT started.** Branch `gco-goalcritic`. Doctrine: measurement before machinery; one lever; pre-registered criteria;
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

## 10. Implementation record (2026-11-18)

**Flag:** `./GigaLearnBot --gco-fresh` (new; `--gco` is unchanged). Code: `ConfigureGCOFresh` and a `g_GCOFresh` branch in `MakeEnv` (`src/ExampleMain.cpp`), `NearGoalState.h`
(committed with tests `tests/test_near_goal_state.cpp`: valid and overlap-free in 1v1/2v2/3v3, blue-attacks share 0.506, 12% of idle-car resets score within 3 s). No Learner change: the fixed
policy LR comes from `exploiter.policyLRStartScale = 1.0` (the ramp's `lrScale` is then 1 at every step). Folder: `checkpoints_gco_fresh` (new). Entropy stays 0.035 (main's).
Critic-context heads: DROPPED (user, after the Phase A null); the run must start WITHOUT `GGL_CRITIC_CTX`.

**What the flag sets:** `gcoRun`, 1v1 only (fleet fractions 0 for 2v2/3v3, 32-game 1v1 skill tracker, gauntlet 1v1 only), `trainAgainstOldVersions` off, external `serveFrac` 0, exploiter pool off,
V_real + goal critic on and V-dagger / geo / gap sensor / reachability off, the reset mix of section 9, metrics run name `gco-fresh`.

**CPU smoke (`GGL_SMOKE=1`, throwaway directory, no checkpoint), 25 iterations, no crash:** "No (loadable) checkpoints found, starting new model"; "128 1v1 / 0 2v2 / 0 3v3"; skill tracker 32 1v1;
gauntlet 8 games per bot 1v1 vs the four bots; `GCO/Policy LR` 1.5e-4 from the first iteration; the only opponent source in the EV panels is self-play, and every bot reads 0 goals for/against in training (loaded
for the gauntlet, never served); checkpoints written to `checkpoints_gco_fresh`.

**Backups taken (before the start), under `build/`:** `checkpoints_gco_6.1_BACKUP_20261118` (the live GCO run, point-in-time; re-synced after the stop), `checkpoints_gco_6.1_FORK_lambdatest_BACKUP_20261118`,
`checkpoints_gco_6.1_ARM_lambda099_BACKUP_20261118`; all verified against their sources.

**Start procedure (needs the user's go-ahead to stop the live run):** `../tools/run_trainer.sh --stop` (saves a clean checkpoint) from `build/`, then `rsync -a checkpoints_gco_6.1/ checkpoints_gco_6.1_BACKUP_20261118/`,
`systemctl --user unset-environment GGL_CRITIC_CTX`, then `../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco-fresh`. Do NOT use `trainerctl restart` (it starts the bare main run). First check in the boot log:
`Checkpoint Save/Load Dir: "checkpoints_gco_fresh"`, "starting new model", "128 1v1 / 0 / 0", no "Critic context" line. Revert: stop it; the live GCO run resumes from `checkpoints_gco_6.1` with
`../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco`. Milestone reviews at 500M, 1B, 2B and 3B steps.

## 11. Launch record (2026-11-18)

Live GCO run stopped cleanly at checkpoint 67,553,331,755 (exit 0, 5,093 s after its restart); its backup `build/checkpoints_gco_6.1_BACKUP_20261118` re-synced and verified (a superset of the source: 16 numbered
checkpoints; the five main model files of the final checkpoint byte-identical). The bot at the last released checkpoint was packaged first: **Pulsar2 v1.0.33**, checkpoint 67,550,920,389, installed in the
Windows RLBot folder (client binary byte-identical to the one in v1.0.32; not yet played in a match).

Fresh run started 17:42 CST as unit `gigalearn-trainer-20261118-174231` with `./GigaLearnBot --gco-fresh`. Boot log: save dir `checkpoints_gco_fresh`, "starting new model", 1024 1v1 / 0 / 0 arenas, skill tracker
32 1v1, gauntlet 96 games per bot 1v1 every 50 iterations, no "will be reset" lines, no "Critic context" line; policy LR 1.5e-4; first iteration collection about 149k steps/s, overall about 52k steps/s
(so 500M steps is about 2.7 h and 3B about 16 h at that rate). Milestone reviews at 500M, 1B, 2B, 3B steps (section 9).
To go back to the old run: stop this one, then from `build/`: `../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco`.


## 12. 1B milestone read and reset-mix change (2026-11-18)

1B read (1006M steps, iter 2500): ball touch ratio 0.0098 (15x the start), time in air 0.43 (was 0.89), speed toward ball 492, goals/10k 5.07 (2x start), episode length ~944 (no longer timing out; penalty 0), entropy 0.569, critic EV 0.30, gauntlet first-touch 7-10% (median 6.3%), gauntlet win 0% vs all four bots (n=49). The acquisition wall broke between 500M and 1B. Aerial touch ratio fell to 6e-5 (ground play learned first).

User-directed reset-mix change applied at 1,006,362,624 steps (clean stop, resume from checkpoints_gco_fresh): NearGoal 0.20 -> 0.10, BallNearCar 0.20 -> 0.24, AirDrill 0.10 -> 0.13, AirPlay 0.10 -> 0.13; Kickoff 0.20 and Random 0.20 unchanged. One lever. Compare post-1B panels against the 1B read above. Watch: aerial touch ratio (expect up), goals/10k (NearGoal is cut, so a drop in free goals is expected and not a regression by itself), gauntlet first-touch/win. Revert: restore 0.20/0.20/0.10/0.10/0.20/0.20.

## 13. Reset-mix change reverted (2026-11-18)

User-directed: the section 12 mix (NearGoal 0.10 / BallNearCar 0.24 / AirDrill 0.13 / AirPlay 0.13) was in effect for about 312M steps (1,006M to ~1,318M); the run was stopped cleanly at ~1,318M and resumed from checkpoints_gco_fresh with the launch mix restored: BallNearCar 0.20 / AirDrill 0.10 / AirPlay 0.10 / Kickoff 0.20 / Random 0.20 / NearGoal 0.20. PulsarGCO v1.0.0 (checkpoint 1,025M) was cut during the changed-mix window.

## 14. Plateau rule and what comes after (pre-registered 2026-11-19, user-directed; written at ~1.7B steps)

**Direction (user).** The bootstrapping wall is broken (section 12). Keep this run on 1v1 pure self-play until skill gain plateaus for a sustained period, then decide: activate 2v2 and/or 3v3, start over, or keep going. The 3B stop-and-review of section 9 is a review point, NOT an endpoint; the run continues past it unless a guard trips.

**Observation to carry forward (single run, no ablation).** The NearGoal setter was in the mix from the start and the wall broke between 500M and 1B. That is consistent with near-goal resets being what supplied the early goal density, but it was never ablated (the 1B-to-1.3B mix change moved four slices at once and was reverted), so treat it as a hypothesis. For a future fresh run, the cheap test is a higher NearGoal share from step 0 (for example 30-40%) and a comparison of steps-to-first-gauntlet-win.

**Skill signals, in order of trust.** (1) Gauntlet mean win % and per-bot win % (real match play against fixed bots; currently near 0, so it is the LAST to move and the most meaningful once it does). (2) `Ref/Oldest Share` (frozen opponent, cannot inflate; 0.76 at 640M, 0.95 at 1.58B; it SATURATES near 1, so it stops being informative soon). (3) Goals per 10k rows, ball-touch ratio, speed toward ball (behavioral, not skill). `Rating/1v1` is inflated and is NOT a plateau signal, whatever it does. Manual out-of-run checks (user matches vs Psyonix bots, `anchor_battery.py` / `match_play_eval.py`) count and should be recorded here with the checkpoint step.

**Plateau definition.** Evaluated at each 500M-step boundary from 2B on, on the median of the last 500M steps versus the median of the 500M before it. PLATEAU = all three: (a) gauntlet mean win % has not risen by more than 1.0 percentage point (and no single bot's win % has risen by more than 3 points); (b) goals per 10k rows within +-5%; (c) ball-touch ratio within +-5%. Because the gauntlet near zero is noisy (n of about 48 per bot per 500M window), a plateau must hold for TWO consecutive windows (1B steps) before it is called, and any manual match result that shows a clear gain (a new Psyonix tier beaten, a gauntlet bot beaten more than twice in a row) resets the count.

**Guards (manual, each evaluation).** Stop and report if gauntlet scores fall for three consecutive evaluations, if policy entropy leaves its own band (currently about 0.57) by more than 30%, or if episode length or goals per 10k rows collapse by more than 30% over a window. A restart only ever resumes from this run's own full checkpoint (recovery doctrine).

**Decision menu once a plateau is called (to be chosen then, with the numbers; not chosen now).**
1. Activate 2v2/3v3 on this lineage: only if 1v1 skill is flat and the team-play machinery (the padded obs, 230-dim plus the 48-dim ball prediction, already supports it) can be turned on without a reward or net change; record per-mode gauntlet and EV before and after, and compare against the paused 1v1 curve.
2. Add the deferred V-dagger / composition critic (memory note, section 9) as a one-lever change on this base.
3. Re-introduce old versions or external bots into training (the pure self-play arm's counterfactual), since the plateau could be self-play myopia.
4. Start over with the near-goal-heavy reset mix of the observation above, if the run looks stuck in a local style.
5. Continue unchanged if the plateau looks like a long flat stretch with a late climb (check the entropy and the Plasticity panels first).
Each is one lever with pre-registered criteria, per the doctrine.

## 15. Far near-goal slice added (2026-11-19, user-directed, at 3,344,556,032 steps)

A second `NearGoalState`, `NearGoalState(1600, 3200)` (ball 1600..3200 uu in front of the attacked goal line; the near slice is 600..2200, so the new one is the same setter 1000 uu deeper), added at 10%. Donors: Random 0.20 -> 0.14, and BallNearCar, AirDrill, AirPlay, Kickoff 0.01 each (0.19 / 0.09 / 0.09 / 0.19); the proven near slice stays at 0.20. New mix: BallNearCar 0.19 / AirDrill 0.09 / AirPlay 0.09 / Kickoff 0.19 / Random 0.14 / NearGoal 0.20 / NearGoal-far 0.10. Clean stop, resumed from checkpoints_gco_fresh/3344556032. Test `NearGoal_far_variant_is_valid_and_deeper` passes in 1v1/2v2/3v3 (no out-of-bounds, no overlaps, the attacker is within 1200 uu horizontally in 100% of resets; the 3D distance reads 0.95-0.96 because the ball is airborne in 45% of resets).

Pre-registration. Hypothesis: a mid-range scoring-chance slice helps the late-game plateau (more attacking and defending practice between kickoff and the goal mouth). Compare the 500M windows before (2.5-3.0B, and 3.0-3.3B) and after against the plateau panels of section 14 (gauntlet win %, goals per 10k rows, touch ratio). Expected: goals per 10k rows should move (more resets near goals) without that being skill, so read goals/10k together with the gauntlet. One lever; revert by removing the far line and restoring Random 0.20, BallNearCar 0.20, AirDrill 0.10, AirPlay 0.10, Kickoff 0.20. The plateau rule of section 14 restarts its two-window count from this point.
