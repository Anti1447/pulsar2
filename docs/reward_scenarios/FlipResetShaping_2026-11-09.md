# FlipResetSetupReward + FlipResetApproachReward - design record, 2026-11-09

User-directed. Code: `Rewards/PlayerBall/FlipResetSetupReward.h`, `FlipResetApproachReward.h`, `FlipResetGeometry.h`. Tests: `tests/test_flip_reset_shaping.cpp` (14).
Registered in `BuildRewards()` as ZeroSum(TEAM_SPIRIT) at weights 10 / 20, right after FlipResetReward 120. NOT engine-verified.

## Decisions (from the Q&A)
- Setup state: ANY ground control (not just a carry): own touch within 8 steps (~0.53 s), ball within 600 uu and z <= 300, car grounded.
- Setup shape: per-step, boost factor clamp(boost/50, 0.2, 1); pays only while the ball CLOSES on the attacking-half wall/backboard/corner (>= 300 uu/s, full at 1200, nearness 1800 -> 600 uu); per-episode leaky bucket (1.0 raw, 0.1 raw/s).
- Hand-off: a 2 s setup latch; no wall-ride stage.
- Approach gate: ball airborne (z > 400) inside the latch window; pays only past apex (vz <= +100); expires after 3 s, ball < 300, opponent touch, landing, goal, or this player's touch.
- Approach terms: closing on the ballistic intercept (ball at vz -250), underside orientation, relative-speed match (horizontal match, car FASTER in z by 0..500, peak 200: identical velocity pays 0 because the ball "sticks"), one-shot contact bonus.
- Contact bonus: wheels-first (-up . toBall >= 0.3); pre-touch ball vz triangle -600..+100 peak -250; car 30-60 deg below the ball centre (15 deg taper); car on the side AWAY from the opponent goal (30 deg full, 90 deg zero); handbrake x1.25 (multiplier only, no standalone pay).
- After the reset: nothing extra (FlipResetReward 120 pays the event). No AirPlayState change (measure first).

## Farming probe
- Setup: bucket bounds sustained income to 0.1 raw/s = 1.0 weighted/s; <= 4.0 raw (40 weighted) per 30 s episode. Needs a ball closing on a wall, so hovering pays 0. ZeroSum nets ~0 in mirror play, but a weaker opponent leaves the income with the bot.
- Approach: closing is two-sided (net zero for hovering/oscillating); proximity terms 0.04 max/step, ~0.9 raw over a whole 3 s window; contact is one-shot per fresh setup (every disarm kills the latch). Max one clean contact is 1.25 raw = 25 weighted, about a fifth of FlipReset.
- Known soft spots: the ballistic intercept ignores bounces; `isOnGround` is true on walls so a wall-driving car counts as grounded; the 2 s latch and the contact bands are the user's numbers, never measured on real resets.

## Pre-registration
- Panels: per-term reward share for the two new terms, FlipReset event rate (resets/hour), Goal rate, `Carry/*`, player boost at wall setups.
- Healthy: Setup share of per-term |r| < ~10%; FlipReset events rise from ~0 within ~500M steps; Goal rate and Rating not worse.
- Suspicious: Setup share climbing with no launches (wall-carry farming); ball z at contact clustered outside the bands.
- Viewer: carries toward the side wall/backboard at 50+ boost, launch, then a wheels-first touch from below-behind the ball.
- Revert: delete the two `BuildRewards()` lines (no other state). One lever at a time with FlipReset 120 (already at its raised weight).

## Rev 2 (2026-11-09, later): speed floors, hard 2300 cap, speed-match bonus, wall launch touch (tests 19/19)
- **Ball speed into the wall:** side wall needs >= 1000 uu/s; backboard floor falls from 1000 (far) to 600 at <= 600 uu from the backboard (lower closer to the opponent goal). **Hard cap 2300:** above it the setup pays 0, the match bonus is 0 and the launch pays 0 (a faster ball cannot be caught up to).
- **Speed match:** setup pay x (1 + 0.5 x matchFrac); matchFrac = 1 at vector |car vel - ball vel| <= 200, 0 at >= 600 (multiplier, so no pay without the base setup; still inside the leaky bucket).
- **Launch touch (one-shot, in the Setup reward, raw 1.0 x boost factor x direction):** ball rolled up the wall (2 s latch from a qualifying ground step), this car is ON the wall (grounded, z >= 150, within 200 uu of a side wall or the backboard outside the goal mouth) and touches the ball (z >= 250, within 300 uu of a wall/backboard/corner). Direction from `state.ballPred` (ballistic fallback if absent): 1.0 if the path reaches the opponent goal mouth, 0.5 if it crosses (or, from the opponent half, stays in) the opponent half, 0 if it goes back toward our half or never crosses midfield. The latch is spent by the launch.
- **Approach interplay:** the launch arms the Approach (latch + ball z > 400); a car still on the wall waits unpaid and only a landing AFTER flying disarms it (previously any grounded frame killed it).
- Open: the exact wall-proximity numbers (200 / 300 uu, z minimums) and the "ballistic fallback ignores bounces" are mine, unmeasured; the 2 s latch must cover the roll up the wall.
