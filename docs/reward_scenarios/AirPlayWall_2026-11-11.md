# AirPlayState: WALL_TO_AIR mode (2026-11-11)

Status: implemented, engine-tested (`tests/test_airplay_wall.cpp`, 2 tests, real RocketSim arenas, 3000 resets x 1v1/2v2/3v3), `ExampleMain.cpp` syntax-checked.
**Not deployed**: the trainer was not restarted. This is a state-setter change, not a reward; it changes what the bot visits, so it is one lever and needs its own measurement.

## Intent (user-directed)
Add a wall-to-air setup to `AirPlayState`: the ball ROLLING at 300..2300 uu/s toward a wall, corner or backboard anywhere on the map, more at midfield walls and the walls nearer the
halves, backboard and corner less frequent than side walls, ONE blue car behind the ball, every other car placed as in the other state setters.

## What it does
- **Mode share:** `wallFrac` (new last constructor parameter, default 0.30) of AirPlay's resets. The remaining 70% keep the old `resetFrac` 0.5 split between FLIP_RESET_READY and AIR_CARRY.
  AirPlay itself was 0.15 of all resets (`CombinedState`); the user then raised it to 0.20 and lowered BallNearCar 0.30 -> 0.25 (same day). Of ALL resets now: wall 6%, flip-reset-ready 7% (was 7.5%), air-carry 7% (was 7.5%).
  Without the weight change the 70/30 split alone would have cut flip-reset and carry seeding by 30%; at 0.20 the cut is about 7%.
- **Wall kind:** side wall 70% / backboard 15% / corner 15%.
  - Side wall: the ball meets x = +-4096 at a y drawn from a triangular distribution over +-3800 (peaked at midfield). Heading is the wall normal +-35 degrees.
  - Backboard: the ball meets y = +-5120 at |x| in 1300..3200 (always outside the goal mouth, which is |x| < 893 + ball radius), heading the wall normal +-35 degrees.
  - Corner: the ball meets the 45-degree corner wall near (+-3700, +-4350), approaching from inside that quadrant.
- **Ball:** on the ground (z = ball radius), horizontal speed uniform 300..2300 uu/s, rolling spin (no slip). It starts 0.7..1.8 s of rolling from the wall, never closer than 450 uu.
- **Setup car (one random BLUE car):** on the ground, 250..600 uu directly behind the ball along its heading, facing that way, speed = ball speed - 100..+250 clamped to 0..2300, boost 50..100.
- **Every other car** (all orange cars, any second/third blue car): the existing grounded-support placement (goal-side of the ball, 1500..3000 uu, facing it).
- Sampling retries until the ball and the car start inside the pitch (the corner planes cut in at |x|+|y| ~ 8064); a midfield side-wall roll is the fallback, so it always succeeds.

## Measured on the engine (20000 resets, 2v2)
wall 30.2% of AirPlay resets | side 70.0% / backboard 15.1% / corner 14.9% of those | side-wall contact |y| < 1200: 54.4%, 1200..2500: 34.1%, > 2500: 11.5%
| ball speed mean 1302 uu/s, below 1000: 34.7%, above 1800: 24.7%.
The test asserts these within tolerance, plus per reset: ball rolling on the ground inside the pitch, never headed at the goal mouth, exactly one blue car 250..600 uu collinear behind it,
speeds <= 2300, boost 50..100, support cars far from the ball, and `wallFrac` 0 / 1 switch the mode off / on.

## Interpretation choices to confirm
1. "Focus on midfield walls and the walls closer to the orange and blue half": I used one triangular distribution peaked at midfield and tapering toward the back corners (54% of side-wall
   contacts within 1200 of midfield, 12% beyond 2500). If you meant two separate bumps (midfield and each half) with fewer near the back corners, it is a one-line change.
2. Ball speed is uniform over 300..2300, not weighted toward either end and not lower near the opponent's goal (the FlipResetSetupReward's 1000 uu/s side-wall floor therefore pays on about 65% of these).
3. Backboard x range 1300..3200 and the corner target (+-3700, +-4350) are my picks.

## Risks and pre-registration
- This is exposure, not a reward, so it cannot be farmed, but it takes 30% of AirPlay from flip-reset-ready and carry. Judge by FlipReset events/hour and the new FlipReset/* panels (when deployed), `Carry/*`, and the wall-related rewards (FlipResetSetup, WallJumpToBall).
- Healthy: FlipResetSetup and WallJumpToBall per-term share rises, no drop in FlipReset events beyond the 30% seeding cut.
- Suspicious: FlipReset events fall by more than the cut, or episodes from this mode end quickly in NoTouch truncations (the ball rolling into a wall and stopping with nobody near it).
- Revert: `new AirPlayState(900, 0.5f, 1500, 3000, 0.0f)` in ExampleMain (restores the old split exactly).

## Limits
Reset geometry only. Not checked: that the bot learns anything from these starts, how long a rolling ball stays in play off the wall, or whether the engine's ball (restitution, wall friction) bounces off these walls in a playable way.
A bug found and fixed during this work: `RS_CLAMP` is a macro, and an RNG call inside its argument was evaluated several times (car speeds up to 2550); the test caught it.
