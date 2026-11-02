> Audit 2026-11-01, **revised the same day after an engine probe** (`tests/test_wavedash_engine.cpp`). Method: hand trace of
> the real header + compiled tests (`tests/test_reward_audit_movement.cpp`, 14 scripted-state scenarios) + a real RocketSim v3 car
> driven through jumps and flips. Intent inferred from the header and v3 log, not confirmed with Nathan. Not edited: the reward,
> `BuildRewards()`. Nothing here ran in training.

# RolvWavedashReward

**Intent (inferred):** pay a wavedash - landing while flipping to turn the flip's impulse into ground speed - scaled by the speed added.
**Class:** individual event, unwrapped (v3: `new RolvWavedashReward(), 20.0f`, PHASE_3_GOLD). **Registered:** commented out at 20.

## UPDATE 2026-11-02 - read `wavedash_ts8_experiments_2026-11-02.md`
The "15 Hz captures ~40-60% of true wavedashes" figure below came from pressing jump/flip for a single tick, which the bot cannot do (its
actions are held for 8 ticks). With 8-tick actions on a level car Rolv (and the stock rule) fired in 0 of 10,000 oracle episodes; after a
nose-up approach they fire in most episodes (Basic up to 74%, Rolv up to 82%). The formal definition of the mechanic is `docs/WAVEDASH_FORMAL.md`. So: visibility depends on how the car arrives, not on sampler phase. The
other conclusions (no cooldown, no kickoff gate, near-constant payout, unwrapped) stand.

## CORRECTION to the first version of this audit
The first draft said an ordinary jump-flip-land hop is paid in full and a hop loop farms it. **That was wrong for the engine.** My fixture
scripted `isFlipping` true on the landing step for a hop, which the engine does not produce. Measured with a real car (level front flip after
one jump, state sampled every 8 ticks like `EnvSet`):

| flip pressed (ticks before touchdown) | flip still active at landing? | Basic fires (of 8 sampler phases) | Rolv fires | mean stored dv |
|---|---|---|---|---|
| >= 4 (every ordinary hop, z >= 35) | no: the dodge lifts the car, hang time becomes ~130-170 ticks, the flip ends first | 0 | 0 | - |
| 3 (z ~ 32) | yes | 4 | 5 | 497 |
| 2 (z ~ 30) | yes | 2 | 3 | 498 |
| 1-0 | yes | 0 | 1 | 471 |

(rows are flips pressed 112, 113, 114 against a no-flip landing at tick 114; `tests/test_wavedash_engine.cpp` asserts the pattern.)
So the detector is **selective**: it only sees flips pressed within ~3 ticks of ground contact, which is the wavedash timing window. The reward
is not farmable by plain flip hops.

## Scenarios (scripted states, `RolvWavedash_*`)
| tier | setup | traced value | verdict |
|---|---|---|---|
| easy | jump, flip with a 500 uu/s kick, land while flipping | **500/2300 = 0.2174** on the landing step | ALIGNED |
| easy | land without flipping | 0 | ALIGNED |
| middle | flip with no velocity change | 0 | ALIGNED |
| middle | flip ends in the air before landing | stored value cleared, 0 | ALIGNED |
| middle | `scaleByAcceleration = false` | flat 1.0 | ALIGNED |
| middle | frame 0 (no prev) | 0 | ALIGNED |
| hard | repeat landings in a flip | every one pays, no cooldown | AMBIGUOUS: it is the intended mechanic, but nothing limits the rate |

## What is still wrong
1. **15 Hz drops 40-100% of true wavedashes** (table above). The reward a bot sees for a correctly timed wavedash is a coin flip that depends on
   the sampler phase, and it systematically favors earlier flips (>=3 ticks) over the latest ones.
2. **The scaling has almost no range.** The stored value is the velocity change over the step in which the flip starts; the engine measured
   471-498 uu/s for every level front flip, so the payout is a near-constant 0.2 (about 4.3 at weight 20, 2.7 per event after the ~62% capture).
   Direction is not checked: a backward or sideways dodge pays the same.
3. **No cooldown, no ball or height context, unwrapped.** A bot can loop jump -> last-moment flip on the spot (hop ~0.9 s) and collect ~2-3 per
   second at weight 20 with no ball involved. That is the mechanic itself being farmed, not a flaw in detection; it is 0.15 goals per 10 s.
4. **No kickoff gate** (v3's `WavedashBasicReward` has one).
5. Not tested: tilted-car wavedashes (the real technique uses nose-down tilt), diagonal/sideways flips, wall/ceiling landings, and the Rolv
   "landing in a flip" being triggered by something other than a deliberate wavedash.

## Verdict
Closer to correct than the first draft claimed. As written: narrow detector, unwrapped one-sided income with no rate limit, near-constant
magnitude, and 15 Hz capture of ~40-60%. Fine as a tiny nudge toward the timing; not as a weight-20 term. See the comparison in the change log.

## Pre-registration (if enabled)
Health: wavedash events per minute (wire `Rolv`'s counters first - none exist), share with the ball within 1500 uu after the event; suspicious:
events at a steady rate on empty ground. Viewer: wavedashes into approach lines, not hop-and-flip on the spot. Revert if events/min rises while
Ball Touch Ratio falls.
