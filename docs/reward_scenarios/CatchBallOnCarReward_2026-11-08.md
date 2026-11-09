> Audit date 2026-11-08. Method: hand trace of the real header (`CatchBallOnCarReward.h`, 112 lines) at tickSkip 8
> (deltaTime = 8/120 = 0.0667 s), then **10 compiled tests** (`tests/test_reward_audit_catchballoncar.cpp`, all pass)
> that run the real header on fabricated states; every traced value was confirmed (C7 is 5.398, not "about 5.3").
> **Not run in the engine**: the tests take the touch flag as given, so whether a roof-resting ball reports contact
> every step is still unmeasured. Not edited: the reward, `BuildRewards()`.
> Intent confirmed by Nathan 2026-11-08: the reward should pay **possession-building catches only**, not every touch
> that happens to slow the ball, to cut farming and misclassified touches.

# CatchBallOnCarReward

**Verdict: MISALIGNED and farmable. Do not enable as written.** It does not detect a catch. It pays on any touch step
where the ball slowed by any amount, and a carry makes that true nearly every step.

## Classification and registration
- Class: one-shot event per touch step, individual positive, **no cooldown, no rising-edge test, no opponent test**.
- Registered (commented out, `src/ExampleMain.cpp:375-377`): `ZeroSumReward(new CatchBallOnCarReward(), TEAM_SPIRIT)`, weight 0.5.
  `TEAM_SPIRIT` is 0.3 in Phase A, 0.6 from PHASE B. History: re-enabled 2026-11-05, pulled the same day as "too easy to farm".
- Change log: v3 history has 1 entry (2026-06-30). The 2026-11-05 comments in `ExampleMain.cpp` are the only Pulsar note.

## Scenario table
Common setup unless stated: blue car origin (0,0,17), forward (0,1,0), up (0,0,1), flat, on ground.
`ballTouchedStep` true, `state.prev` present. "pay" is raw, before weight 0.5 and the ZeroSum wrapper.

| # | tier | setup | trace | pay | verdict |
|---|---|---|---|---|---|
| C1 | easy, pays | Ball in at 1000 uu/s, after touch ball (60 ahead, rel z 103) moving (0,420,30), car vel (0,400,0) | slowdown (1000-421)/1000 = 0.579; frontAlign 60/119.2 = 0.503 (>= 0.4); isOnTop (h 103, horiz 60, fwd 60); relSpeed 36 < 250; frontHalfScale 1.0 -> carry 4.0; base 5 x 0.579 x 0.801 x 1 = 2.32; top 1.0 | **7.32** (cap 15) | ALIGNED: a clean hood catch |
| C2 | easy, no pay | Same as C1 but incoming ball speed 540 (0.9 x 600) | `prevBallSpeed < 600` returns 0 | 0 | ALIGNED. At 660 x 0.5 slowdown it pays normally |
| C3 | middle, geometry cliff | Ball resting on roof centre, rel (0, 30, 124), slowdown 0.5 | frontAlign = 30/127.6 = **0.235** < 0.4 | **0** | **MISALIGNED**: the best carry position pays nothing |
| C3b | counterfactual to C3 | Ball moved to forward 59.5 (rel z 124); everything else the same | frontAlign 59.5/137.5 = 0.433; isOnTop; align factor 0.773; base 5 x 0.5 x 0.773 = 1.93; carry 4 x (0.5 + 59.5/120) = 3.98; top 1.0 | **6.91** | A 10.8 uu shift moves 0 -> 6.9. Cliff at forward = 0.4364 x height (54 uu at h = 124, 48.7 vs 59.5 = 0.9x / 1.1x). The roof spans -56..74 forward, so roughly the back two thirds of the roof pays 0 |
| C4 | middle, clear / block | Shot at 2500, car (0,1200,0) hits it, ball after (0,1400,0), rel (0,100,50) | slowdown 0.44; align 0.894 -> align factor 0.958; speed penalty 1 - (1400-1200)/1000 = 0.8; isOnTop fails (h = 50 not > 50), isNearTop passes; relSpeed 200 < 350 -> carry 2 x 1.333 = 2.67; base 5 x 0.44 x 0.958 x 0.8 = 1.69; top 0.4 | **4.75** (cap 8) | **MISALIGNED** with your intent: a clear that leaves at 1400 uu/s is not possession, and it pays 65% of a real hood catch. Relative speed 200 counts as "carried" because the ball left at car speed |
| C5 | hard, carry spam | Carry at 1500 uu/s, ball rel (0,70,124) (hood), ball speed 1498 after a 2 uu/s dip, touch flag true | slowdown 2/1500 = 0.0013 (> 0, so it passes); align 0.49; isOnTop; relSpeed ~10; speed penalty 1 - (1498-1200)/1000 = 0.70; carry 4 x 1.083 = 4.33; base 5 x 0.0013 x 0.9966 x 0.70 = 0.005; top 1.0 | **5.34** | **MISALIGNED**: the pay is almost entirely the carry bonus and the on-top bonus, both granted for any slowdown > 0 |
| C6 | counterfactual to C5 | Same, but the car is boosting and ball speed goes 1500 -> 1510 | slowdown clamps to 0 -> `slowdown <= 0` returns 0 | **0** | A 10 uu/s speed difference flips 0 vs 5.34: discontinuity at slowdown = 0+ |
| C7 | hard, 600 boundary | Carry decelerating from 620 -> 610 | prev 620 >= 600; slowdown 0.016; carry + top apply | ~5.3 | The 600 floor does not stop a slow carry; it just sets how long a coast pays |
| C8 | edge | First step of an episode | `!state.prev` returns 0 | 0 | ALIGNED |

C5/C6 require the touch flag to be true on consecutive carry steps. `ballTouchedStep` is true if the last hit tick is
within the 8-tick step window (`Player.cpp:19`). The 2026-11-03 ConsecutiveAirTouch audit measured engine contact persisting
for 8-20 consecutive steps during a push. **Whether a ball resting on a roof reports contact every step is not measured
for this reward. UNVERIFIED; this is the first thing the engine pack must check.**

## Farming and gaming probes
1. **Income loop (the carry-coast loop).** Brake lightly while carrying a ball above 600 uu/s: every step with any
   slowdown pays about 5.3 (C5). A coast from 1400 to 700 uu/s at about 525 uu/s^2 lasts about 1.3 s = 20 steps, about
   **106 raw per coast**. If every carry step pays (5.34 x 15 Hz): **80 raw/s = 40 weighted/s at 0.5**, about one Goal
   (150) per 4 s. Even if only half the carry steps show a dip, that is 20/s weighted. The theoretical cap is 15 per
   step (225 raw/s). Compare DribbleMastery's steady maximum of 0.467 per step (7 raw/s): this reward pays **about 11x more
   per step than the dribble reward it is stacked with**, for the same behavior. The speed-up half of the loop pays 0, so a policy
   needs to cycle accelerate/coast; that is one boost press away.
2. **Does the wrapper remove it?** No. `ZeroSumReward` subtracts the opponent's average catch pay; in 1v1 the opponent
   only gets it if they also carry. Works against any opponent that lets the bot carry (past selves, Nexto, passive
   bots). Mirror play nets about 0 only if both sides farm equally. The reward never inspects opponents, so there is no opponent type it fails against.
3. **Avoidance by degeneracy / early termination:** not applicable (never negative).
4. **Anti-farm defenses present:** none. No cooldown, no rising-edge test (`state.prev->players` touch flag is never
   read), no ball-left-the-car re-arm, no one-shot per possession.
5. **Invariant check:** with the wrapper it is zero-sum by construction, but it is an individual positive income term
   whose rate is far above the stack's other shaping. Bare it would break the whole-stack invariant.
6. **Other gaming:** (a) a stationary car blocking a hard shot with its nose pays about 2-3 for doing nothing offensive;
   (b) the pay depends on `rel.z` in the world frame, so a car on a ramp or wall shifts every gate; (c) no field-position
   or direction condition, so a trap in the bot's own box pays the same as one at midfield (arguably fine, but it is
   not "possession building" unless the ball stays).

## What a possession-building version would need (suggestions only, nothing edited)
In order of value, each one lever per `CLAUDE.md`:
1. **Rising-edge**: require `!state.prev->players[...].ballTouchedStep` for this player. Kills the carry-spam loop (C5).
2. **Minimum slowdown**: require slowdown >= about 0.25 and an absolute drop of >= about 300 uu/s, instead of `> 0`.
   Removes the 0 vs 5.34 discontinuity (C6) and pays the catch, not the jitter.
3. **Require an incoming ball**: the ball must have been >= about 250 uu from the car on the previous step (not already on it).
4. **Cooldown with re-arm**: >= 1.0-1.5 s per player and re-armed only after the ball has left to >= 300 uu.
5. **Possession follow-through**: reject exits (C4) by requiring ball speed <= car speed + about 250 after the touch, or
   pay a held-possession stream (a few steps later) instead of the instant.
6. **Car-frame geometry**: use the same car-frame fwd/up/lat gates as `DribbleMasteryReward` (`lf`, `lu`, `lr`)
   so the roof centre pays (fixes C3). Drop the world-frame `frontAlign` test or relax it.
Then re-run this ladder; C3, C4, C5, C6 are the regression scenarios.

## Overall verdict
Misaligned with the confirmed intent on three counts: the geometry gate excludes the best catch position (C3), blocks
and clears pay as catches (C4), and a carry pays about 5.3 a step on any deceleration (C5), making it far more
farmable than the dribble reward. Do not enable. Pointers: `CatchBallOnCarReward.h:44` (slowdown test),
`:94-99` (carry bonus), `:104-110` (top bonus and cap), `:52-56` (front alignment gate).

## Pre-registration (for a fixed version, not for the current code)
- **Health metric:** catch events per 1000 steps, and mean pay per catch. Healthy: catches rare and discrete,
  mean pay 3-8, and catches per carry second near 0 (one per possession, not per step). Suspicious: pay per step while
  `Dribble/carry` is active above 1.0, or catch events within 0.5 s of each other for the same player.
- **Panel:** per-reward mean contribution for this term (name to be confirmed in the reward-logging panel before enabling),
  next to `Player/Ball Touch Ratio` and goals per episode.
- **Viewer check:** the bot traps a fast ball onto the hood and holds it; it does not brake-feather a carry.
- **Revert:** this term's share of the positive reward above 10%, or carry-time fraction above 25% for two consecutive
  evals, or goals/episode not improving over 200M steps while catch pay rises. Revert = comment it out (nothing checkpointed depends on it).
- **Engine pack to run first:** roof-rest contact flag per step (settles the UNVERIFIED item); one hood catch; a
  coast-while-carrying run reproducing C5.

## Update 2026-11-08 (same day): Rev 3 applied (audit fixes 1-3)
Implemented in `CatchBallOnCarReward.h`: rising-edge touch, real slowdown (>= 25% and >= 300 uu/s), incoming ball (>= 250 uu
from the car on the previous step), and a 1.5 s cooldown that re-arms only after the ball has been >= 300 uu away (a gated-out touch
does not start it). The 24 tests in `tests/test_reward_audit_catchballoncar.cpp` pass; the carry-spam scenarios (C5, C5b, C6, C7) now
pay 0, C1 still pays 7.32, and the cooldown blocks exactly 23 steps after a paid catch.
**Still open** (audit fixes 4-5, deferred): C3 (roof-centre catch pays 0, hood edge pays 6.9) and C4 (a 2500 -> 1400 clear pays 4.75).
Those tests are kept as explicit "still true" markers. **Still unverified in the engine:** the roof-rest touch flag. The reward is still
not in the stack; the pre-registration above still applies, plus the new `Catch/*` panels (see the DribbleMastery report's update note).

## Update 2026-11-08 (later): Rev 4 applied (audit fixes 4 + 5)
- **Fix 4, car-frame geometry.** The ball must end in DribbleMastery's carry band in the car's frame (up 100..230, fwd -70..150,
  |lat| <= 75). The world-frame `frontAlign >= 0.4` gate is gone: **C3 now pays 7.5 at lf 30 (was 0)**, and the 0 -> 6.9 cliff at
  forward 54 uu is gone (pay across 53 vs 55 differs by < 0.05; at the band edges it falls smoothly to 5.0 at lf -60 and 4.5 at lf 150).
- **Fix 5, no clear or block payout.** The near-top branch and the ball-speed penalty are removed; the ball must leave the touch with
  relative speed <= 350 and not faster than the car by more than 250. **C4 (the 2500 -> 1400 nose clear) now pays 0 (was 4.75)** because
  the ball is not on the car, and a hard hit that ends over the hood at car + 700 uu/s also pays 0.
- **Pay shape changed** because the frontAlign factor is replaced by the position quality: C1 7.32 -> 7.895; cap 8 (was 15 / 8).
  Full formula in the header.
- Tests: 27 in `tests/test_reward_audit_catchballoncar.cpp` pass, including an equivalence test that the reward pays exactly when
  `CarryCatchTelemetry::PossessionCatch` is true, so the `Catch/Possession Catch Rate` panel counts what the reward would pay.
- **Open:** the engine roof-rest touch-flag measurement, and a decision on whether a catch at the cap (8 raw, 4 weighted at 0.5) is the
  right size against the live stack. The reward is still not enabled.

## Re-audit 2026-11-08 (night): "it is not procing enough" - CONFIRMED, the Rev 3/4 gates are over-tight

**Live telemetry** (run `aoxm2g5n`, 91 logged points, trainer on Rev 4 with the new `Catch/*` panels, per player-step):
`Catch/Slowing Touch Rate` mean 1.45e-4 (about one per 7.7 min of player time); `Catch/Possession Catch Rate` mean 1e-6 (max 1e-5).
Only ~0.7% of the touches that already survive the speed gates end as a possession catch, and the reward pays exactly when
`PossessionCatch` is true, so the reward is effectively dead (0.25 weight x ZeroSum x ~1e-6).

**Engine experiment** (real RocketSim v3 car + ball, 864 scripted drops onto a moving roof: car 600/1000/1400 uu/s, ball horizontal
speed 0..1.2x car, vz 0/-500/-1000, 2 arrival times, 2 fwd and 2 lateral offsets, 3 sampler phases; 8-tick sampling; first rising-edge touch).
Source: scratchpad `catch2.cpp` / `catch3.cpp` (not committed). Every scenario produced a rising-edge touch.

| Gate (current Rev 4, world frame) | Share of first touches that die there |
|---|---|
| prev ball speed < 600 | 9.7% |
| world slowdown < 25% / 300 uu/s | **80.4%** |
| prev ball-car distance < 250 | 9.8% |
| reaches the geometry / exit gates | **0%** (0 of 864 pay) |

Findings:
1. **Gate 2 is measured in the WORLD frame.** A ball dropped on a roof that is already moving with it is *sped up* by the car, so
   "slowdown" is negative for exactly the catches a dribbler makes; only a ball faster than the car is ever slowed. The catch is a
   RELATIVE-velocity event (ball vs car).
2. **Gate 3 (prev distance >= 250) is geometrically near-impossible at 15 Hz.** Touch happens at centre distance ~130; the previous
   step is 0.0667 s earlier, so >= 250 needs a closing speed of ~1800 uu/s. A gentle catch (closing 600) was one step ago at ~170 uu
   and is rejected as "already on the car". Rev 3 introduced this; it was calibrated by thinking at 30/120 Hz.
3. **Exit gate relSpeed <= 350 rejects every bounce** (a ball dropped on a roof rebounds at ~0.6x the normal speed; 600 uu/s down -> 360 up).
   DribbleMastery's own carry gate admits 500.
4. Geometry (car-frame band) never failed in the scripted drops: Rev 4's band is not the problem.

**What the same fixture says about relative-frame gates** (prev REL speed, REL drop, exit REL speed; pays out of 864):
R0 rel>=400, drop>=50%, exit<=350: 12.4% | R1 drop>=35%, exit<=350: 14.7% | **R2 drop>=35%, exit<=500 (carry gate): 25.5%** |
R3 rel>=300, drop>=30%, exit<=500: 26.5%. These are scripted catch attempts, not a rate in self-play; they show the gates stop
being the binding constraint, nothing more.

**Recommended (not applied; a fix is a separate ask):**
- A. Measure incoming speed and slowdown in the car-relative frame: `prs = |ballVel_prev - carVel_prev|`, require `prs >= 400`, and `(prs - crs)/prs >= 0.35`.
- B. Drop the `MIN_INCOMING_DIST` gate (rising edge + relative incoming speed already say "new contact"); keep the 1.5 s cooldown and the 300 uu re-arm as the farm defence.
- C. Exit relSpeed <= 500 (== the carry gate) so `PossessionCatch` stays "ball enters the carry state". Keep the `car speed + 250` cap.
- D. Update `Telemetry/CarryCatchTelemetry.h` the same way and keep the grid test (reward > 0 <=> `PossessionCatch`); add the engine drop grid above as a test so the gates cannot silently go dead again.
- Pre-register: `Catch/Possession Catch Rate` should rise from ~1e-6 to the 1e-4..1e-3 range; revert if `Carry/Time Fraction` does not move or the paid-catch count per episode exceeds ~1 per 2 s (the cooldown bound) in the viewer.
