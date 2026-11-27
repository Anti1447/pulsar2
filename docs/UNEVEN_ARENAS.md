# Uneven training arenas (2v1, 3v1, 3v2) for Pulsar3

2026-11-26, user-directed. **Enabled in Pulsar3 at 5,461,651,693 steps (17:07, 15% share)**, after the Phase B policy-LR ramp.

## What it does

- A marker file `build/checkpoints_pulsar3/UNEVEN_ARENAS` (content: share of arenas, 0 to 0.5; empty = 0.15) plus a restart
  puts that share of arenas, rounded down to a multiple of 6, into uneven shapes: 2-1, 1-2, 3-1, 1-3, 3-2, 2-3 (blue-orange).
- At 0.15: 150 uneven (25 of each), 292 1v1, 291 2v2, 291 3v3. Player rows stay at 4,094, about 15% of them uneven.
- External bots serve uneven arenas too (user-directed; `serveUnevenTeams` is set with the marker). Dice only takes 2-1 / 1-2
  (its limit is 2 a side); its other uneven arenas play self-play. Old versions and self-play include them. Skill tracker and
  gauntlet fleets are unchanged.
- Sides and colours: every shape exists with the bigger team on blue and on orange. In self-play every car is trained. In a bot or
  old-version iteration the opponent takes one colour for the whole fleet (random per iteration, as before), so the live policy
  plays both the bigger and the smaller side that iteration, and its colour varies between iterations.
- AirPlay's wall drill: the setup car is blue when teams are equal (unchanged) and either colour in uneven arenas.
- Bot statistics: `<Bot>/Goals For|Against` and the per-mode play series stay symmetric games only. Uneven games report under
  `Uneven/*`: trained cars by size and colour; self-play goals by bigger/smaller scorer; goals for/against bots and old versions
  as bigger or smaller, each split by the bigger team's colour (per iteration, not persisted).
- Nexto batches by player count now (it assumed 2 / 4 / 6).
- Without the marker the layout is the previous one.

Code: `RLGymCPP/TeamLayout.h`, `ExampleMain.cpp` (marker, `MakeEnv`), `Learner.cpp` (serve rule, row tags, `Uneven/*`),
`LearnerConfig.h`, `NextoOpponent.cpp`, `ExternalPlayMonitor.h`, `AirPlayState.h`, `KickoffOutcomeReward.h`.
Tests: `tests/test_uneven_arenas.cpp`.

## Tested (2026-11-26, in build-uneven/)

- Tests matching Uneven, PlayMonitor, Team, Kickoff, AirPlay, Nexto, Decimated passed. The full suite was not run. The uneven
  tests include every bot (Nexto, GalactusV2, ChronosV2, Dice, Pulsar2) acting from either side, on CPU.
- Second smoke with bots serving uneven arenas: self-play and bot iterations on both bot colours; `Uneven/*` trained cars were
  balanced (self-play 24/24 bigger, 12/12 smaller by colour).
- Smoke training on a copy of checkpoint 5,050,368,453 (`GGL_SMOKE=1`, 128 arenas with 18 uneven): loaded, 3 iterations
  collected and learned, one of them a bot iteration, saved, skill eval and gauntlet ran. Log shows no failure.
- Not covered: any effect on playing strength; the full-size fleet; the exploiter/GCO-only value panels' uneven labels (compiled
  only).

## Known limits

- Rewards: the zero-sum wrapper still balances the two teams' per-car averages, but the sum over all cars is not zero in an
  uneven arena. Kickoff coverage is paid only to teams of two or more.
- Uneven play is measured only by the `Uneven/*` training counters: sampled actions, a moving policy, no rating, no fixed-seed
  evaluation. The bots were built for even games, so their uneven play is of unknown quality.

## Enabling

1. Check `git diff` first: a rebuild of `build/` also compiles the other uncommitted changes (RocketGPU-trial additions, all
   behind environment variables or unchanged defaults - re-verify at the time).
2. `cmake --build build -j8`; `echo 0.15 > build/checkpoints_pulsar3/UNEVEN_ARENAS`;
   `tools/trainerctl stop`, then `cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --pulsar3`.
3. Boot log must show "Uneven arenas: 150".

Revert: delete the marker and restart.

## Revert criteria

None. A proposed rule (revert if the 1v1 four-bot mean falls more than 5 points in 500M steps) was declined by the user on
2026-11-26.

## Automatic switch (2026-11-26, user-authorized)

Unit `pulsar-uneven-switch` (`build-uneven/switch/enable_uneven.sh`, log `switch.log` beside it) waits for 5.454B steps,
records the 8-gauntlet baseline (`baseline.txt`), rebuilds `build/`, preflights, writes the marker, keeps a full checkpoint in
`build/checkpoints_pulsar3_pre_uneven/`, restarts Pulsar3, and rolls back if the new process does not train. It aborts if the
sources changed after testing. Cancel: `systemctl --user stop pulsar-uneven-switch`. A PC reboot cancels it.

### Outcome

17:13: switched; boot log shows 150 uneven arenas, boot probe 3/3, no crash line, about 61k steps/s in the first minutes.
WavedashReward 10 -> 12 went live in the same restart. Baseline at the switch (8 gauntlets, 5.147-5.437B): 1v1 four bots
22.53%, 2v2 all bots 3.15%, 3v3 0.75%. The 1v1 figure was already down from 28.6% before Phase B. Full pre-switch checkpoint:
`build/checkpoints_pulsar3_pre_uneven/5461651693`; old binaries: `build-uneven/switch/old/`.

## Uneven ratings (2026-11-27, user-directed)

With the marker present the rating matches add 12 arenas (each of the six shapes twice) to the symmetric 16: `Rating/1v2`,
`Rating/1v3`, `Rating/2v3`. Same Elo and same old-version opponents as the other modes; the live policy takes one colour per match
segment, so across the arenas it plays the bigger and the smaller side equally. A rating therefore says how the current policy does
against older selves in that matchup averaged over both sides, not how often the bigger side wins. New ratings start at 0. The
reference battery (`Ref/*`) and the gauntlet are unchanged.

Live from the restart at 6,620,738,832 steps (2026-11-27 01:01); first segment reported Rating/1v2, 1v3, 2v3.
