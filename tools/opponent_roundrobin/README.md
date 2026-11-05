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
for the fixed-seed gauntlet: the match loop lives in `GigaLearnCPP/src/private/GigaLearnCPP/MatchLoop.h` and the trainer's `GauntletEval` calls it with our
policy as the controller (see docs/EXTERNAL_OPPONENTS.md, "Gauntlet of our policy").

Read the confidence bands with care: they assume independent games. A deterministic bot (Nexto) from only five 1v1 kickoff layouts
replays a handful of distinct games, so its kickoff-suite bands are too narrow; the scrimmage suite is the diverse one.

## Watch mode (checklist 1.4)

Plays two bots against each other in ONE arena, in real time, and streams every step to the RocketSimVis web viewer:

```bash
# terminal A: the viewer's web server
cd ~/pulsar2/tools/viz/RocketSimVisWeb && ../.venv/bin/python -u server.py
# terminal B (repo root): A vs B, 6 games, kickoff suite, speed 1 (bots: Nexto, GalactusV2, ChronosV2, Random)
cd ~/pulsar2 && ./build/OpponentRoundRobin watch Nexto GalactusV2 6 kickoff 1
```

Open **http://localhost:9275/visualizer/visualizer.html** in a browser on Windows (the bare `http://localhost:9275` only lists the folder).
Arguments after the two bots: games (6), `kickoff` or `scrimmage` (the training state mix), speed multiplier (1 = real time), seed.
The colours swap every game, so both bots are seen on both sides. The log prints each game's result and who touched first.
In the viewer: left mouse cycles the spectated car, `P` jumps to the car closest to the ball.
