# ConsecutiveAirTouchReward: steeper ball-speed damping (2026-11-10)

Status: logic changed and audited on fabricated states (30/30 tests in `tests/test_reward_audit_consecairtouch.cpp`, 8 new). **Not deployed**: the trainer was not
restarted, so the live run still uses the old 2300 -> 3000 / 0.5 damping until the next restart. The Reward Lab pack `consecutive_air_touch` is now STALE (not re-signed; sign-off is the user's).

## Intent (user-directed, with one open question)
A car tops out at 2300 uu/s, so it cannot chase a ball leaving faster than that horizontally to make a consecutive touch. Credit for a touch should therefore fall off
with the ball's horizontal (x, y) speed: full at <= 2000 uu/s, linear down to a floor of 0.25 at 2600 uu/s, 0.25 beyond.
The request also said "horizontal ball speed must not exceed 2300". I implemented the ramp (the later and more specific rule), which gives 0.625 at 2300 and does **not** cap at
2300. See "Open questions".

## Change
`HorizontalSpeedMul` (ConsecutiveAirTouchReward.h): `HORIZ_VEL_LIMIT` 2300 -> 2000, `HORIZ_VEL_MAX` 3000 -> 2600, `HORIZ_MIN_MUL` 0.5 -> 0.25.
`m(v) = 1 - 0.75 (v - 2000)/600` on [2000, 2600]. Unchanged: the waiver within 3000 uu of the opponent goal back, the other credit factors, Phi, the control gate, the turnover and demo resets.

## Classification
Exact PBRS (potential of a per-player effective-streak state times a distance gate), `ZeroSumReward(..., TEAM_SPIRIT)`, weight 120 (raised 80 -> 120 on 2026-11-08). The speed factor only scales
what a touch adds to the state, so the potential is still an exact function of state and the telescope is untouched.

## Scenarios (all in the test file; ball at z 1000 unless stated, far from the goal)
| Setup | Traced | Verdict |
|---|---|---|
| horizontal speed 0, 1800, 2000 | m = 1.0 | ALIGNED |
| 2200 (1.1x the lower knee) | 1 - 0.75 x 200/600 = 0.75 | ALIGNED |
| 2300 (car top speed) | 0.625 | AMBIGUOUS: damped, not blocked (see below) |
| 2340 | 0.575 | ALIGNED |
| 2600, 2860, 6000 | 0.25 (floor) | ALIGNED |
| velocity (1600, 1600, 0) or with vz = 4000 | horizontal 2262.7 -> 0.6716 | ALIGNED (z ignored) |
| velocity (0, 0, 5000) | 1.0 | ALIGNED by spec (horizontal only); arguably a gap |
| 2600 ball 2900 uu from the orange goal back | 1.0 (waiver) | AMBIGUOUS: the waiver predates this request |
| same ball 3100 uu from it | 0.25 | ALIGNED |
| chain of 5 touches, ball leaving at 1500 / 2400 / 2700 (car level, softness 1) | Phi(5)=0.8647 / Phi(3)=0.6321 / Phi(2)=0.3935 | ALIGNED: faster ball, slower chain |
| full-credit touch (ball z 1500, car z 1000) at 2600 and at 5000 | 0.25 each | ALIGNED |
| touch on a 2400 ball with the car at 2300 (dv 100) | 0.96 x 0.5 = 0.48 | AMBIGUOUS: positive credit above the car's top speed |

## Farming and gaming probes
- **Income loop:** a chain that ends in a landing nets exactly 0 at gamma 1 (tested with 12 touches on a 2400 uu/s ball). The multiplier changes how fast the potential
  saturates (rate), never its ceiling (Phi < 1) or the refund, so there is no new income.
- **Does it help a juggle farm?** No. It lowers per-touch credit for fast balls, so the cheapest chains are now slow, controlled ones, which is the intent. A slow-ball
  juggle is unchanged.
- **Avoidance by degeneracy / early termination:** none; the term is a refunded potential, never a penalty on its own.
- **Opposing shaping:** it now pulls the same way as the existing softness factor (hard whacks) and the distance gate, so those three together pay least for a ball that
  gets away. No opposing term found.
- **Opponents it works against:** unchanged; ZeroSum removes the income in mirror play only.

## Open questions for the user
1. **Hard cap or ramp?** RESOLVED 2026-11-10 by the user: keep the ramp (no hard cap). A ball at 2300-2600 earns 0.625-0.25 of a touch.
2. **Goal waiver:** kept (full credit within 3000 uu of the opponent goal back, i.e. a fast shot near goal). Drop it if "must not exceed" should hold everywhere.
3. **Horizontal vs total speed:** a fast vertical ball is not damped, by spec. Total speed would also catch it.

## Pre-registration
- Healthy: `Rewards/ConsecutiveAirTouchReward` (mean per-step contribution) falls modestly or stays flat, chains on slow balls unchanged, `Player/Aerial Touch Ratio` not collapsing.
- Suspicious: the reward's share of per-term |r| falling by more than about half within ~200M steps, or the aerial touch ratio dropping with it.
- Revert: set the three constants back (2300 / 3000 / 0.5).
- Missing instrument: there is no panel for the distribution of post-touch horizontal ball speed on air touches, so I can't tell from W&B how much of the chain credit this change touches. A one-line telemetry panel would fix that.

## Limits
Fabricated states, no engine. Not verified: how often real chains involve balls above 2000 uu/s horizontal, or that the bot's air-dribble rate responds.
