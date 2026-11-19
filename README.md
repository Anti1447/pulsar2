# Pulsar2 (fork: Anti1447/pulsar2)

A Rocket League self-play bot trained with PPO on a C++ trainer (libtorch + a vendored RocketSim physics sim). This repository is a fork of
[lucamignatti/pulsar2](https://github.com/lucamignatti/pulsar2), which is built on the GigaLearnCPP framework (its original README is kept below, unchanged).
The upstream work (Luca Mignatti) covers the trainer, the 5.0 run (RocketSim v3, tickSkip 8, 230-dim team-canonical obs, the Optimistic-Critic Ladder and composition critic research
program). Start with `CLAUDE.md` for the architecture and the operating doctrine, and `research/reports/README.md` for the upstream experimental record.

## What this fork has done (from 2026-10-27)

All of it is on branch `gco-goalcritic` (other lineages: `4.0-team`, `6.0-ballpred`, `exploiters`, `gco`, `v3-reward-port`). Each study has a pre-registration
block dated before its run, in `docs/`.

**Tooling and operations**
- Trainer lifecycle and safety: `tools/trainerctl`, `tools/run_trainer.sh` (crash-restart wrapper, save on stop, W&B login/offline, WSL-safe), atomic checkpoints.
- Training dashboards (`tools/dashboard`, `tools/dashboard_gco`), local full-metrics logging (`metrics_latest.json`, `metrics.jsonl`), and a remote-dashboard path.
- RLBot v5 packaging: `tools/package_bot.sh` (Pulsar2 vX.Y.Z) and `tools/package_gco.sh` (**PulsarGCO** vX.Y.Z, own release root, carry-at-9 versioning: 1.0.9 -> 1.1.0, 1.9.9 -> 2.0.0),
  installing to the Windows RLBot folder, plus a live-path parity check of the RLBot client against the trainer.
- RocketSim v3 bumped to rocketsim 0.3.5 (fixes a lost car-ball hit impulse).

**Reward audits ("Reward Lab")** (`tools/reward_lab`, `docs/REWARD_CHANGE_LOG.md`, `docs/reward_scenarios/`, `tests/`)
- Every reward in the stack was traced against a scenario ladder plus a farming probe, then frozen as a compiled test. Fixes found this way: the WallJumpToBall wall detector,
  KickoffRace windows and layouts, ConsecutiveAirTouch weighting, TouchAccel (pay the impulse above a per-player high-water mark), spacing-aware BallProximity,
  UnifiedAirDribble pay rules. Added VerifiedShot, FlipResetTouch and demo-funnel telemetry. CarEnergy weight 75 -> 25 after its audit.

**External opponents and the gauntlet**
- Nexto, GalactusV2 and ChronosV2 (and Dice) loaded as frozen bots, with golden tests of the ports against the original sources, a round-robin watch mode, and an in-trainer
  gauntlet (win % and first-touch % per bot) used as the fixed skill measure. Exploiter runs (external exploiter against a frozen bot) were added and studied (`docs/EXPLOITERS.md`).

**6.0 run prep.** actionDelay 7, 278-dim obs with ball prediction, restore points and a pre-run checklist (`docs/Pre_Run_Checklist.md`).

**GCO: the goal-only reward line (`docs/GCO*.md`)**
- A run whose reward is only the goal (+-1) plus a small no-touch timeout penalty, with the shaping stack removed, to learn at small compute with a method that can later scale.
- Offline studies, each pre-registered, with results recorded including the nulls: critic learning-rate test (neutral), GAE-lambda sweep (lambda 0.99 reverted to 0.95), team-critic
  diagnostics and team reset-mix test, real-simulator lookahead search (strong only when the opponent model is correct; oracle opponent 0.805 vs 0.260 raw on Dice),
  distillation of search labels into the policy (no gain; the shuffle control failed, so that line is closed), and critic-context Phase A (opponent source and clocks as critic inputs:
  a clean null).

**The fresh-start 1v1 pure self-play run (`docs/GCO_PURE_SELFPLAY_ARM.md`, started 2026-11-18)**
- A true cold start, `./GigaLearnBot --gco-fresh`: 1v1 only, goal-only reward, pure self-play (no old versions or external bots in training; they are used only for the skill tracker and gauntlet),
  fixed policy LR 1.5e-4, entropy 0.035, own checkpoint folder `checkpoints_gco_fresh`.
- Added a `NearGoalState` reset setter (ball and players near a goal) to give a goal-only reward dense enough to bootstrap from random play. The acquisition wall broke between 500M and 1B
  steps (ball touches 15x, goals per 10k rows 2x, time in the air 0.89 -> 0.43). That the near-goal setter is what broke it is a hypothesis, never ablated; a higher near-goal share from step 0
  is the proposed cheap test for a future run.
- State at about 1.7B steps: first gauntlet wins against Nexto, and a narrow human-played win over the Psyonix Rookie and Pro bots (PulsarGCO v1.0.0, cut at 1.03B steps).
  The run continues until skill plateaus (pre-registered plateau rule, section 14 of that doc); only then are 2v2/3v3, the deferred V-dagger critic, other opponent sources, or a restart decided.

**Running the fresh-start run**
```bash
cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco-fresh     # resumes from build/checkpoints_gco_fresh
tools/trainerctl status|logs|follow|stop                                       # never use `trainerctl restart` for GCO: it starts the non-GCO run
tools/package_gco.sh                                                           # newest checkpoint -> PulsarGCO vX.Y.Z (also --dry-run, --list)
```

---

# Upstream README (GigaLearnCPP)

## GigaLearnCPP Leaked Version

-------------
# This is the version that cheaters decided to leak :(

### This library was intended to be private, but apparently we can't have nice things. 

A trusted former GigaLearn user (with source code access) decided to join the cheaters and then share around this library, now it's public.
Thus, I am formally publishing this specific version for anyone to use.

-------------

GigaLearn is an even-faster C++ machine learning framework for Rocket League bots.
This is a progression far beyond my previous C++ learning framework, RLGymPPO-CPP (which I have stopped developing).

## Speed
Collection speeds are around 2x faster in GigaLearn than RLGymPPO-CPP, and around 10x faster than RLGym-PPO (on my machine).
Consumption speeds are a bit faster than RLGymPPO-CPP, although this varies heavily.
This speed is not at all final and I plan to make the library much faster once I finish other important features.

## Features
**Basic Features (Shared With Most Frameworks)**:
- Fast PPO implementation
- Configurable model layer sizes
- Checkpoint saving/loading
- Working example
- Return standardization and obs standardization support
- Built-in visualization support
- Easy custom metrics suppport

**Unique Learning Features**:
- Extremely fast monolithic single-process inference model
- Complete and proper action masking
- Built-in shared layers support (enabled by default)
- Built-in configurable ELO-based skill tracking system 
- Configurable model activation functions
- Configurable model optimizers
- Configurable model layer norm (recommended)
- Policy version saving system (required if using the skill tracker)
- Built-in reward logging

**Unique Environment/State Features**:
- Access to previous states (e.g. `player.prev->pos`)
- Simpler access to previous actions, final bools
- Inherented access to all `CarState` and `BallState` fields (e.g. `player.isFlipping`)
- No more duplicate state fields
- Simpler access to current state events (e.g. `if (player.eventState.shot) ...`)
- User-led setup of arenas and cars during environment creation
- RocketSim-based state setting (you are given the arena to state-set)
- Configurable action delay

***Coming Soon**:
- Training against older versions

## Installation
*There's no installation guide for now as I plan to rework several aspects of the library to make it easier to install.*

## Bringing In Rewards/Obs Builders/Etc. from RLGymPPO_CPP
State changes:
- `PlayerData` -> `Player`
- `player.phys` -> `player.`
- `player.carState` -> `player`
- `state.ballState` -> `state.ball`

Reward changes:
- `GetReward(const PlayerData& player, const GameState& state, const Action& prevAction)` -> `GetReward(const Player& player, const GameState& state, bool isFinal) override`
- `prevAction (argument)` -> `player.prevAction`
- `GetFinalReward()` -> `isFinal (argument)`

Metrics:
- `metrics.AccumAvg(), metrics.GetAvg()` -> `report.AddAvg()`

Learner config:
- `cfg.numThreads, cfg.numGamesPerThread` -> `cfg.numGames`
- `cfg.ppo.policyLayerSizes` -> `cfg.ppo.policy.layerSizes`
- `cfg.ppo.criticLayerSizes` -> `cfg.ppo.critic.layerSizes`
- `cfg.expBufferSize` -> `(experience buffer removed)`
