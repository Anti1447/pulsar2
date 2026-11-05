# Scaffold anneal plan (decided 2026-11-05)

**Decision:** "anneal later" means a MANUAL step, written down now, done at a checkpoint restart. Nothing anneals automatically. `mechanic_census` is NOT the trigger (it cannot load these checkpoints and does not count aerial touches). `ScaffoldController` is not used (it regulates flip/wavedash counters that none of these rewards feed).

**Which weights** (live in `BuildRewards()`, about 265 against Goal 150; FlipReset is no longer in the stack): AerialTouch 120, AirIntercept 75 (potential-based), ConsecutiveAirTouch 40, WallJumpToBall 30.

**Why they stay high at first:** the 500M-step target is "hits the ball, basic recoveries", which is the formative window these weights were raised for. Do not lower them early.

**The rule**
1. **Signal:** `Player/Aerial Touch Ratio` (logged in `ExampleMain.cpp`, W&B). Read together with the Goal reward share and the Nexto Goals For/Against panel, so a high ratio that costs goals does not count as success.
2. **Threshold and window:** NOT SET. They cannot be calibrated before the run, because this lineage has no measured curve. At the 500M-step review, read the ratio's curve, choose X (the level) and N (the window in iterations), and write them in the box below BEFORE changing anything.
3. **Action:** when the ratio has held above X for N iterations, lower AerialTouch 120 -> 80 (a change of 33%, inside the +-50% limit), restart from the checkpoint, and log the change in `docs/REWARD_CHANGE_LOG.md`. Wait for the next review, then 80 -> 50 if the ratio still holds. AirIntercept, ConsecutiveAirTouch and WallJumpToBall each move in their own separate changes, one lever at a time.
4. **Pre-registered failure signal:** if the aerial ratio or the goal rate falls clearly after a step, restore the previous weight.

**Set at the 500M review:** X = ____ , N = ____ , date = ____

**Not covered:** this is a procedure, not a measurement. No run has shown where the ratio settles, and none has shown that lowering the weights keeps the skill.
