# Pre-run checklist: the next pulsar2 run (6.0 lineage)

Started 2026-11-02. Tick items as they are finished. Sizes are rough (S/M/L). Facts below were read from the repo on 2026-11-02; anything marked *unverified* was not run.

**Run identity:** `checkpoints_6.0`, W&B `6.0-ballpred`, fresh from step 0 (`docs/6.0.md`). Trainer = `GigaLearnBot` built from `src/ExampleMain.cpp`, built and run under WSL.
**Locked:** observations are `AdvancedObsPadded` + 8 ball-prediction slices (278 floats). **Target by 500M steps:** the bot hits the ball, plus basic recoveries; mechanics should visibly improve fast and look clearly different from random early.

## 0. Decisions and housekeeping
- [x] Observation design locked (above).
- [x] Success target defined (above). *Still to write down:* what the reward-health and behavior checks look like for "hits the ball" and "basic recoveries" (W&B metric, range, and what to look for in RocketSimVis).
- [x] All uncommitted work committed 2026-11-02 (6.0 nets, ported-reward includes, `CLAUDE.md`, improvement-ideas doc, Chronos/Galactus `SOURCE.txt`, submodule pointers). Nothing is pushed.
- [x] **Decided 2026-11-02: `actionDelay` 7 for training** (done in `ExampleMain.cpp`, `CLAUDE.md`, `docs/6.0.md`, `docs/PULSAR5.md`). Original question: The live client now uses 7 (RLBot play with 0 was erratic) but `cfg.actionDelay` in `ExampleMain.cpp:844` is still 0, so live and training differ.
  - For 7: matches live, and matches Chronos and Galactus (both trained with 7, so they would no longer act ~58 ms sooner than trained; `docs/6.0.md`).
  - For 0: the comment at `ExampleMain.cpp:835` records why 0 was chosen: max action-effect correlation for precision mechanics ("revisit only if RLBot sim-to-real matters").
  - If it changes: fresh run needed anyway; update the comment there, `CLAUDE.md` lines 26/120/139/515, `docs/6.0.md` line 53 and `docs/PULSAR5.md`; the engine requires `actionDelay <= tickSkip`; the Reward Lab scenarios default to 0 (set `action_delay` per scenario or leave as is, since rewards do not read it).
- [ ] **Push** the new commits and, if wanted, the `cpp-interface` branch to a fork (see item 6). Remotes: `origin` = lucamignatti/pulsar2, `fork` = Anti1447/pulsar2.
- [x] (tag pre-6.0-20261102, hashes in docs/6.0_RESTORE_POINT.md) Restore point: tag the commit the run starts from (precedent: `pre-strip-20260725`), and record the `*.lt` hashes (weights are git-ignored).
- [ ] Final go/no-go: all tests green (2026-11-02: GigaLearnTests 207/207 incl. Lab, built with -DGGL_BUILD_TESTS=ON, run from the repo root; receiver tests tools/reward_lab/test_stream.py 20/20 and test_serve.py 8/8 passed 2026-11-02), clean checkpoints_6.0 (re-clean after the smoke), short smoke run DONE 2026-11-02 (CPU GGL_SMOKE, 21 iterations, 843k steps, no errors).

## 1. Chronos and Galactus verified in training (M-L)
Existing: `GGLBotOpponent` ports each bot's observations and action masks; a random-weight smoke run loses heavily to both. The render viewer can choose the agent on each side.
- [x] No delay emulation needed: `actionDelay` is 7, matching their training.
- [ ] **Golden test:** same recorded states into the original repos' inference code and into `GGLBotOpponent`; compare observation vectors and chosen actions.
- [ ] **Watch them in RocketSimVis:** kickoffs, boost use, speed at touch, Chronos' x-mirroring.
- [ ] **Sim vs live:** N head-to-head games (Chronos vs Galactus, optionally Nexto) in the sim and the same N live through RLBot; compare win rates, goals, touches. Choose N and the tolerance up front.
- [ ] Resolve the live ball-prediction mismatch (RLBot supplies its own prediction; the sim uses RocketSim's).
**Gate:** observation and action agreement above a chosen threshold, and a sim-vs-live result within a pre-set tolerance.

## 2. Audit the rewards (L)
Scope: the 15 active rewards in `BuildRewards()` (Goal 150, AerialTouch 120, BallToGoal 75, AirIntercept 75, CarEnergy 75, WallJumpToBall 60, KickoffRace 60, ConsecutiveAirTouch 40, Demo 37.5, OpposedSave 25, TouchAccel 10, GuardedPickupBoost 6, BallProximity 4, TeamPressure 0.15, TimeCost 0.01) and the 23 commented-out ports from RLGymBot_v3. In the Lab today only 3 are registered and instrumented (OffsidePenalty, CarEnergyPotentialReward, PassingSetupReward).
- [ ] Triage by weight x how often it fires: Goal, AerialTouch, the 75s, then the 60s.
- [ ] Per reward: register it (`Lab/RewardRegistry.h`, one line); write a golden-path scenario, one failing scenario per gate, and a farming check; derive the expected values by hand first; run; fix.
- [ ] Resolve the two open items in `docs/ENABLED_INVENTORY.md`: CarEnergy 75 is unmeasured; six scaffold weights (~260 vs Goal 150) name `mechanic_census` as their anneal trigger and it cannot work (the unwired `ScaffoldController` is the designed replacement).
- [ ] You sign off finished packs (`reward_lab_pack sign`); only a CURRENT pack counts as verified.

## 3. Improve and fix rewards (M, after 2)
- [ ] Per reward: keep, fix, disable, or enable-from-the-ports, with evidence from the audit.
- [ ] One lever per change; no weight change above +-50%; a line in `docs/REWARD_CHANGE_LOG.md`; a pre-registered "working" signal before enabling anything.
- [ ] Record the stack's total count and weight mass (gradient dilution was RLGymBot_v3's main problem).

## 4. Ball prediction confirmed (M)
Existing: `tests/test_ballpred_cache.cpp`, `tests/test_advanced_obs_padded.cpp`, and the claims in `docs/6.0.md`.
- [ ] Check the 8 slices in a real render arena, not only unit tests: after touches, resets, kickoffs and state-setter resets (the Lab uses the same builder; ball-at-known-state scenarios can do most of this).
- [ ] Measure cost at scale: one ball-only arena per environment across 310 games; steps per second before and after, and memory.
- [ ] Verify Galactus' 5 samples (0.25/0.5/1/1.5/2 s) are built from our predictor correctly.
- [ ] Confirm the `--gco` lineage still builds its 230-float observation.
- [ ] Live path: the RLBot client does not request ball prediction (it flooded the socket) and packaging for 6.0 is not done. Defer packaging until after the run starts; it is not needed to train.

## 5. The new planning document (M, write last)
- [ ] Sources: RLGymBot_v3 `CLAUDE.md` (weight heuristics, W&B stop rules, mechanic spec template, reward health vs behavior confirmation), its `RewardCatalog.md` and change log; pulsar2 `REWARD_SHAPING.md`, `ENABLED_INVENTORY.md`, `PULSAR5.md`, `docs/6.0.md`, the zero-sum / unfarmable doctrine.
- [ ] Outline: (1) run identity and restart plan; (2) reward doctrine; (3) the reward inventory with each reward's verified status; (4) the measurement program; (5) exit criteria; (6) open risks.
- [ ] Keep it thin: one document that links to the others instead of copying them.

## 6. cpp-interface patches (live play against the current game build)
- [x] pulsar2: the two patches are preserved in `patches/cpp-interface/` (0001 schema bump to `c2648b4` + io_uring buffer cap 32 -> 4, 0002 sample bots off the default build) and committed in the submodule on the local branch `pulsar-local-patches`.
- [x] `GGLBot2/cpp-interface` (Windows live-deploy repo): both patches applied to the working tree 2026-11-02 (it had the stock schema and 32 buffers). **Not committed there, not built:** the schema bump could need changes in GGLBot2's RLBot client code, and the buffer cap is a Linux io_uring fix with no known effect on a Windows build. `GGLBot` (the older one) was not touched.
- [ ] Build GGLBot2 against the new schema and confirm it connects to the current game build.
- [ ] Push `pulsar-local-patches` to a fork of RLBot/cpp-interface so a fresh clone can fetch the commit pulsar2 points to (until then the patch files are the durable copy).

## Optional
- [ ] Wire the reward stream into the trainer's render branch (two lines in `ExampleMain.cpp`: `MaybeInstallStreamTapsFromEnv(result.rewards, TICK_SKIP);`) so reward traces can be watched live during render runs. Held by decision until a run exists.

## Suggested order
Decisions (0) -> items 1, 2 and 4 in parallel -> 3 -> 5 -> go/no-go. Item 6 can run alongside.
