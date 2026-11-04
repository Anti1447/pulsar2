# Sim round robin of the external bots (checklist 1.5)

```bash
make -C build OpponentRoundRobin
./build/OpponentRoundRobin kickoff 200 20261105 docs/ROUND_ROBIN_KICKOFF.md        # 1v1, real kickoffs, ~1 min
./build/OpponentRoundRobin scrimmage 200 20261105 docs/ROUND_ROBIN_SCRIMMAGE.md    # 1v1, the training state mix
./build/OpponentRoundRobin scrimmage 100 20261105 docs/ROUND_ROBIN_3V3_SCRIMMAGE.md 3   # 3v3, one bot per side
```
Every ordered pair of {Nexto, GalactusV2, ChronosV2, Random} (and the mirrors) plays N games, one per arena, stepped like the trainer
(tickSkip 8, actionDelay 7, `Reset()` before the step). A game ends on a goal, 10 s without a touch or 90 s (the last two are draws).
`Random` is a uniform-random-valid-action floor (a stand-in for a random-weight policy). The same loop is where our own policy plugs in
for the fixed-seed gauntlet (a controller with `Act()`); that hook into the trainer is not built yet.

Read the confidence bands with care: they assume independent games. A deterministic bot (Nexto) from only five 1v1 kickoff layouts
replays a handful of distinct games, so its kickoff-suite bands are too narrow; the scrimmage suite is the diverse one.
