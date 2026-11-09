> Audit date 2026-11-08. Method: hand trace of the real header (`DribbleRewards.h:188-393`, Rev 4.0) at tickSkip 8
> (deltaTime = 8/120 = 0.0667 s, so 0.40 s = 6 steps, 0.75 s = 12 steps), then **15 compiled tests**
> (`tests/test_reward_audit_dribblemastery.cpp`, all pass) that run the real header on fabricated states.
> The tests confirmed every traced number except D3 (hand slip, corrected below). **Not run in the engine**: whether a
> bot can hold the pop-reset cycle (Finding 2) or a turning carry (D7) is still unmeasured. Not edited: the reward, `BuildRewards()`.
> Intent confirmed by Nathan 2026-11-08: reward holding a ground carry on the roof/hood (ground-only is intended).
> The concern recorded for the 2026-11-05 pull is that **the bot felt no pressure to turn a dribble into a shot at the
> other goal and kept farming dribble income instead.**

# DribbleMasteryReward

**Verdict: well bounded per carry, but it rewards holding, not scoring. The concern is confirmed by the code.**
A single uninterrupted carry is capped (about 39 raw over 10 s, then about 0), but pay is flat across the field,
the cap resets whenever the carry breaks, and nothing links a carry to a shot.

## Classification and registration
- Class: individual continuous positive shaping (per step while in the carry state), cooldown after opponent touches,
  threat-weighted additive penalty.
- Registered (commented out, `src/ExampleMain.cpp:374`): `ZeroSumReward(new DribbleMasteryReward(), TEAM_SPIRIT)`,
  weight 0.5 (`TEAM_SPIRIT` 0.3 Phase A, 0.6 from PHASE B). Pulled 2026-11-05 as "too easy to farm".
- Change log: v3 history, 6 entries. Rev 4.0 (2026-10-16) cut pay about 3x after a scenario test found a 12 s perfect carry paying
  about 1040 weighted against GoalReward 500 (the old figures); the Rev 4.0 numbers below are new.

## Scenario table
Common setup unless stated: blue car origin (0,0,17), flat, forward (0,1,0), up (0,0,1), on ground, no opponent
within 600 uu, ball moving toward the orange goal (direction factor 1.0). "pay" is raw, before weight 0.5 and the wrapper.
Time `t` is carry age; step = 0.0667 s; the 0.4 s gate lands on step 6 +-1 (float rounding).

| # | tier | setup | trace | pay | verdict |
|---|---|---|---|---|---|
| D1 | easy, pays | Car vel (0,1800,0), ball pos (0,40,141) so car-frame (lf 40, lu 124, lr 0), ball vel = car vel | gates pass (lu 100-230, lf -70..150, lr <= 75, speed >= 800, rel 0). Step 1: entry 0.03 | 0.03 once, then 0 until t = 0.4 | ALIGNED |
| D1b | same, t = 4 s | base = 1 x 1 x 1 x 1 = 1; sustainNorm 2.0, durationFactor 4; 1 x 0.08 x 4 = 0.32; flick-ready (lf 40, lu 124, rel 0) x1.25 = 0.40; boost 1.0 x 0.0667 = 0.0667 | **0.467** per step (the maximum) | ALIGNED |
| D2 | easy, no pay | Same, car speed 799 | `speed < 800` -> EndCarry | 0 | ALIGNED |
| D2b | counterfactual to D2 | Car speed 880 (1.1 x 800), t = 4 s | speedMult 0.4 + 0.6 x (80/900) = 0.453; bc = 0.08 x 4 x 0.453 x 1.25 = 0.181 + boost 0.067 | 0.248 | ALIGNED: slow carries pay about 53% |
| D3 | middle, lateral edge | Lateral 67.5 (0.9 x 75), t = 4 s; 82.5 fails the gate | centering 1 - 67.5/75 = 0.10; bc 0.1 x 0.08 x 4 = 0.032, x1.25 (flick) = 0.04, + boost 0.067 | 0.107 (82.5 -> 0). The first draft of this report said 0.117; the test caught the arithmetic slip | ALIGNED. But note the boost term is quality-blind (see Finding 4) |
| D4 | middle, state change | Carry at t = 4 s, opponent touches the ball | `Trigger(0.75)`, then `TickAndCheck` -> true -> EndCarry, return 0. Blocked for 12 steps (pre-decrement gate). Re-entry: entry 0.03, t restarts, pays 0 until t = 0.4 | 0.467 -> 0 -> 0.03 | ALIGNED. A teammate touch does nothing (only `team != player.team`) |
| D5 | hard, opponent pressure | Carry t = 4 s (pre-penalty 0.467). Opponent 300 uu from ball, ball closing on it at 1400 (full closing), facing -> threat 1.0, scale 1.0, closeness 0.5 -> opponentThreat 0.5 | penalty 0.5 x (0.5 + 2 x 0.467) = 0.717 | **-0.25** | ALIGNED with the intent ("established carry still net-negative under pressure") |
| D5b | counterfactual to D5 | Same distance, but the opponent chases from behind at 1500 vs ball 1800 (closing 0, facing 1): threat 0.4, scale 0.76, opponentThreat 0.38 | penalty 0.38 x 1.433 = 0.545 | -0.079 | ALIGNED. At 590 uu (closeness 0.017) the penalty is about 0: an open-field carry pays in full |
| D6 | hard, direction | Carry t = 4 s, ball at y = -4000 (own half, 9100 uu from the goal), ball vel straight at the orange goal | towardOpp 1.0 -> dirMultiplier 1.0 | **0.467** | See Finding 1: identical to a carry 300 uu from the goal |
| D6b | counterfactual to D6 | Same carry at y = +4800, ball 300 uu from the goal | towardOpp 1.0 -> 1.0 | 0.467 | Position never matters: **no commitment gradient** |
| D6c | counterfactual to D6 | Ball vel straight at own goal | towardOpp -1.0 -> 0.5 + 0.5 x 0 = 0.5 | 0.233 | Away still pays half |
| D7 | hard, circling | Car carries at constant speed on a circle: ball velocity direction sweeps 360 degrees | fraction of the circle with cos > 0.4: 37% at 1.0; cos < 0 (50%) at 0.5; ramp zone (13%) avg 0.75 | mean factor about **0.72** | UNVERIFIED that a bot can hold a turning carry; if so, going in circles keeps 72% of the straight-line pay |

## Cumulative pay of one uninterrupted carry (hand integral)
Per-step bc = 0.025 t^2 up to t = 4 s, then 0.4. Boost: 0.2/s from 0.6 s, 0.5/s from 1.0 s, 1.0/s from 2.0 s.

| T (s) | cumulative raw | per second |
|---|---|---|
| 2 | about 1.6 | 0.8 |
| 4 | about 10.6 | 2.65 |
| 6 | **24.6** | 4.1 |
| 10 | **38.9** (6-10 s taper averages x0.51: 14.3) | 3.9 |
| after 10 | 0.0093 per step = 0.14/s | about 0 |

At weight 0.5: a perfect 10 s carry = 19.5 weighted, 13% of one Goal (150). The taper (`taperStartSec 6`, `taperEndSec 10`,
floor 0.02) works against a carry that never ends.

## Findings
1. **No commitment gradient (confirms your concern).** The direction multiplier (`:375-386`) uses only the **direction**
   of the ball velocity (0.5 at/after sideways-away, 1.0 at dot >= 0.4). Distance to the goal and shot chance appear
   nowhere. A carry at midfield pays the same as one 300 uu from the net (D6/D6b), and nothing pays more at the moment a
   shot becomes available. Holding is rewarded the same as the opportunity to end the carry.
2. **The taper is per carry, not per possession.** Any single failing gate calls `EndCarry` (`:240`): `lu`, `lf`, `lr`,
   speed, relSpeed or z. Re-entry restarts `t` at 0 with a fresh 0.03 entry. A continuous carry marginal rate is 3.6/s at
   8 s and 1.0/s at 9.5 s, against about 0.8/s for the first two seconds of a fresh carry, so the best income cycle is
   to break the carry around 9.5 s. Estimated sustained rate with a hypothetical 0.78 s out-of-band pop between
   carries (a vertical pop of about 450 uu/s takes the ball above lu 230 for about 12 steps and returns it at relSpeed about
   255 < 500): **about 3.7 raw/s = 1.9 weighted/s**. Over a 30 s episode that is about 55 weighted, about 37% of one Goal,
   at no risk. UNVERIFIED that the bot can hold a cycle like this; the number is a ceiling for a skilled cycler, not a
   measurement.
3. **Pay for holding is not net of the alternative.** Compare 1.9 weighted/s of safe income with TimeCost 0.15/s and a
   goal that needs a contested shot. This matches the 2026-11-05 observation that the bot preferred the income.
4. **The boost term is quality-blind** (`:361-366`): `longDribbleBoostPerSec` (0.2 -> 0.5 -> 1.0 per second) is added
   to the shaped pay regardless of centering, relative speed or speed band. Any carry that squeaks past the hard gates
   (lateral 74, rel 499, speed 801) still earns 1.0 raw/s after 2 s (x direction factor, x taper).
5. **Minor:** the flick-ready window (+25%) is the best-paid configuration and the carry has no reason to leave it.
   Only an opponent touch ends a carry from outside; a teammate touch does not.

## Farming and gaming probes
1. **Income loop:** the cycle in Finding 2 (about 3.7 raw/s). Rate per touch is not the right unit here; it is
   per-second hold. Also note the all-in rate of a single carry in D1b: 0.467 per step = 7.0 raw/s at the cap.
2. **Does the wrapper remove it?** No. In 1v1 mirror play the carrier gets +income and the other side gets
   -income (the wrapper is `own - opponent average`): a carry transfers margin, it does not net to 0. Works against
   any opponent that does not contest the ball within 600 uu or touch it: passive past selves, a ball-chasing
   opponent that is slower. Against a contesting opponent the penalty (D5) usually makes it net-negative.
3. **Penalties:** the opponent-pressure penalty is applied only while the car is in the carry state, so it cannot be
   avoided by degeneracy and cannot push the bot to end the episode. Worst case per step is about 0.5 + 2 x pay = about
   1.4, only inside 600 uu of a chasing opponent.
4. **Anti-farm defenses present:** per-carry taper (works), opponent-touch cooldown (works), ground-only and z < 600.
   Missing: any per-possession or per-episode budget, any link to field position or shot value.
5. **Invariant check:** wrapped in `ZeroSumReward`, so the stack still sums to 0. Individual positive income that
   is not a potential, so it is shaping that does not telescope; this is the only protection against
   hacking it, and the weight is the lever.

## Overall verdict
Mechanically sound (gates, taper, cooldown and pressure penalty all behave as designed) but misaligned with the
confirmed concern: it pays for holding anywhere on the field and its taper resets on any break, so a bot can sit on
income instead of committing to a shot (Findings 1-3). At weight 0.5 the farm is worth about 1.9/s weighted at most;
the live problem is its flatness, not its size. Do not re-enable as is. Pointers: `DribbleRewards.h:375-386`
(direction multiplier), `:240` and `:267-285` (EndCarry resets), `:361-366` (quality-blind boost), `:369-372` (taper).

## Options (suggestions only, nothing edited)
One lever at a time:
- **Make the income depend on distance to the goal** (rising toward the goal, falling at midfield) or convert the
  direction term to a progress-toward-goal potential, so holding at midfield pays little.
- **Drop the 0.5 floor on the direction factor** (away/sideways -> 0) so circling and own-goal-ward carries pay 0 (D7).
- **Add a grace period before `EndCarry`** (about 1 s) so a short pop does not reset `t`, or decay `t` instead of zeroing it.
- **Gate the boost term by `base`** so marginal carries do not earn the 1.0/s floor.

## Pre-registration (if it is re-enabled)
- **Health metric:** carry-time fraction of steps (target 3-10%), mean carry length (target 1-5 s), carries per
  episode, shots or touches within 2 s of a carry end. Suspicious: carry fraction above 25%, mean carry length
  clustering near 6 s or 9.5 s (the taper edge), carry-end events with no shot within 2 s.
- **Panel:** per-reward mean contribution for this term, next to `Player/Ball Touch Ratio` and goals per episode.
- **Viewer check:** carries end in a shot or pass toward the opposing net, not in circles at midfield.
- **Revert:** carry fraction above 25% for two consecutive evals, or goals per episode flat/down over 200M steps while
  this term's return rises. Revert = comment it out in `BuildRewards()`; nothing checkpointed depends on it.
- **Engine pack to run first:** a steady carry and its t = 4/6/10 s pay; the pop-reset cycle (Finding 2); a turning
  carry (D7).

## Update 2026-11-08 (same day): Rev 5.0 applied (audit fixes 1-2) and telemetry added
- **Fix 1:** the direction factor has no floor. Away or sideways carries pay 0 (D6c: 0.2333 -> 0); a circling carry keeps about 0.43
  of straight-line pay (was 0.72).
- **Fix 2:** field-position factor, 0.3 at our own goal back to 1.0 within 2500 uu of the opponent goal back (linear in distance).
  The carry maximum 0.4667 becomes 0.21 at our goal, 0.35 at midfield and 0.4667 near the net; D6/D6b are no longer flat. A circling
  carry at midfield now earns about 0.32 of a straight carry at the net.
- Tests: `tests/test_reward_audit_dribblemastery.cpp` (16 tests, pass). The per-carry shape numbers (24.6 raw at 6 s, 38.9 at 10 s) hold at
  field factor 1.0.
- **Still open** (deferred): Finding 2 (the carry resets on any broken gate; hysteresis) and Finding 4 (quality-blind boost term).
  The pop-reset cycle is therefore still available, now at a reduced rate away from the net.
- **Telemetry** (measurement only, independent of both rewards; `Telemetry/CarryCatchTelemetry.h`, wired into `StepCallback`):
  `Carry/Time Fraction`, `Carry/Start Rate`, `Carry/Toward Net Ratio`, `Carry/Canonical Ball Y`, `Carry/Speed`, `Catch/Slowing Touch Rate`,
  `Catch/Possession Catch Rate`. Mean carry length (s) = Time Fraction / Start Rate x 8/120. These are the panels the pre-registration
  block above should read; they take effect at the next trainer restart. Record a baseline with both rewards OFF before enabling either.
