# Opponents inside a training-style loop (checklist 1.3)

```bash
make -C build OpponentLoop && ./build/OpponentLoop            # from the repo root; 120 arenas, ~1 min
./build/OpponentLoop 20261105 341                              # the PHASE B fleet (1023 arenas), ~3 min, gives the cost numbers
```
`loop_check.cpp` builds a real mixed 1v1/2v2/3v3 `EnvSet` (tickSkip 8, actionDelay 7) and runs the serve mechanics of
`Learner.cpp`'s collection loop (the partitioned roll, `BeginServe`, a random `oppTeam`, the `isOld` mask, `Act()` with the previous
step's terminals) for Nexto, GalactusV2 and ChronosV2 on CUDA. It is a re-statement of the loop, not the Learner.

It checks: realised share of the roll; the bot's rows are acted and no other row is touched; actions differ between kickoff /
ground / aerial states; one team per arena handed to the bot in every mode; repeat determinism (Nexto exact, the others vary through
their slot shuffle); Nexto's fresh-episode flag equals a cold start (with a control that history changes actions otherwise);
step cost against the env alone.

The real Learner was exercised separately with `GGL_SMOKE=1 WANDB_MODE=offline ./GigaLearnBot` (serveFrac 0.75 in smoke): see
`docs/Pre_Run_Checklist.md` 1.3. Clean `build/checkpoints_6.0` and the new `build/wandb/offline-run-*` afterwards.
