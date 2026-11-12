# FlipResetTouchReward (coarse) + FlipReset/* telemetry (2026-11-11)

Status: implemented, 13/13 tests pass (`tests/test_flip_reset_touch.cpp`), `ExampleMain.cpp` syntax-checked. **Registered in `BuildRewards()` at weight 30 but not deployed**: the trainer was not restarted.
The weight 30 is my starting guess (a quarter of FlipReset 120), not a measured value.

## Intent (user-directed)
Simple and coarse: no ball height, no car height, no geometry. Pay when the player touched the ball on the previous step and now has a flip reset.

## Rule
`FlipResetTouchReward` pays 1.0 when `player.prev->ballTouchedStep && player.HasFlipReset() && !player.prev->HasFlipReset()`, else 0. Wrapped `ZeroSumReward(TEAM_SPIRIT)`, weight 30.
- Touch on the PREVIOUS step because with >= 3 wheels on the ball the engine reports `isOnGround = true` on the touch step, so `HasFlipReset()` only turns true the step after (FlipResetReward's 2026-11-07 audit).
- The false -> true edge because `HasFlipReset()` stays true up to 1.25 s; without the edge every step of the window would pay.
- Engine definition: `HasFlipReset() = !isOnGround && HasFlipOrJump() && !hasJumped`.

## Telemetry (state-only; shares the reward's predicates)
`FlipReset/Any Rate`, `FlipReset/After Touch Rate` (edges per player-step), `FlipReset/Per Episode Any`, `FlipReset/Per Episode After Touch` (mean per player-episode, flushed when the next
episode starts), `FlipReset/Episode Has After Touch Share`, `FlipReset/Total Any`, `FlipReset/Total After Touch` (per iteration), and `...Cumulative` (since the process started).
"Any" = HasFlipReset false -> true for any reason. "After Touch" = the same edge with a touch the step before.

## Scenarios (fabricated states; tests)
| Case | Value | Verdict |
|---|---|---|
| touch, then reset appears | 1.0 | ALIGNED |
| no touch the step before | 0 | ALIGNED |
| touch only on the current step | 0 | ALIGNED |
| reset already available (touching every step) | 0 | ALIGNED |
| held window, 10 steps of touches | pays once | ALIGNED |
| on ground / flip spent / past 1.25 s | 0; 1.2 s pays | ALIGNED |
| first step of an episode (no prev) | 0 | ALIGNED |
| any ball height 100..1800, car height 100..1800 | same 1.0 | ALIGNED (by request) |
| jump, reset, flip, reset chain | 1.0 per reset | ALIGNED |
| car on a wall touching the ball, leaves the wall without jumping | 1.0 | MISALIGNED with "a reset", accepted as coarse; documented in a test |
Mutation check: dropping the touch condition fails 5 tests, dropping the edge fails 2.

## Farming and gaming
- **Main false positive:** `HasFlipReset()` is also true for any airborne car that did not jump. A touch while on a wall/ramp/ceiling followed by leaving it without jumping pays 1.0 with no wheel contact in the air. A wall-ride dribble that keeps touching the ball and leaving the wall repeatedly could farm this. Rate ceiling: each pay needs the car to return to a supported state or lose the reset (the edge), so it is bounded by how fast the bot can re-touch the ball and leave a surface, roughly once per couple of seconds. Per-pay value is 30.
- **Real repeated resets** (jump, wheel-touch, flip, wheel-touch) pay once each, and the engine's own flip use re-arms the edge: that is the desired behaviour.
- **No cooldown by design.** If the telemetry shows `Any Rate` high and the dedicated `FlipReset` reward events (wheels-first, high ball) low, the pay is mostly wall exits.
- **Opponents:** ZeroSum removes the income in mirror play only.

## Pre-registration
- Healthy: `FlipReset/After Touch Rate` rises from near 0 together with FlipReset reward events; `Per Episode After Touch` small but nonzero; `Any Rate` >= `After Touch Rate` always.
- Suspicious: `After Touch Rate` climbs while the dedicated FlipReset events stay ~0 (wall-exit farming), or `Episode Has After Touch Share` high with no goal-rate change.
- Revert: delete the `FlipResetTouchReward` line in `BuildRewards()`; the telemetry can stay.

## Limits
Fabricated states only. Not verified on the engine: that `hasJumped` / `airTimeSinceJump` evolve the way the fixtures assume across the 8-tick step, or how many of the edges are real wheel contacts.
