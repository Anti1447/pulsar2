# Dodge, flip, jump and aerial parity patches (P1-6, P1-7), 2026-11-24

Goal: close the GPU vs RocketSim v3 differences on dodges, flips and aerial play found in Part A. What changed: PATCHES.md.
Test: Part A replay (66 scenarios, 6 of them new aerial / flip ball touches) against v3 and v2; 14 of 14 GPU tests pass.

## Car position error vs v3 (uu), at 1 s / 2 s unless noted
| scenario | before | after |
|---|---|---|
| jump (tap, hold, double) | 1 to 4 | 0.1 or less at 1 s |
| side dodge | 3.6 / 21 | 0.4 / 11 |
| back dodge | 32 / 83 | 13 / 22 |
| back-diagonal dodge | 82 / 165 | 62 / 101 |
| forward dodge | 22 / 76 | 17 / 19 |
| forward-diagonal dodge | 61 / 40 | 6 / 23 |
| air roll + yaw + boost | 0.8 / 0.8 | 0.2 / 0.2 |

## Ball touches (new scenarios), car position vs v3 at 1 s / 2 s
| scenario | before | after | v2 vs v3 |
|---|---|---|---|
| aerial hit, centred | 1.3 / 2.3 | 0.6 / 1.2 | 0.8 / 1.6 |
| aerial hit, off-centre | 11 / 52 | 11 / 52 (unchanged) | 0.8 / 1.7 |
| front flip into ball | 5.5 / 6.0 | 10 / 25 (worse) | 11 / 20 |
| diagonal flip near ball (misses) | 11 / 22 | 23 / 34 (worse) | 7 / 20 |
| double jump into ball | 12 / 25 | 8 / 15 | 13 / 29 |
In every hitting scenario the GPU's first touch is one tick earlier than v3's.

## Part A bands
vs v3: 36 of 43 scored scenarios pass (before: 34 of 43, same scenario set). Still failing: 4 dodge apex heights (landing bounce; 6 dodge scenarios failed before), ball_ceiling, ball_goal_post, pad_small_pickup. The new ball-touch scenarios have no pre-registered bands.

## Not fixed / got worse
- Off-centre car-ball contact: the car's deflection after the hit differs by about 50 uu at 2 s. Not touched here.
- Car vs ceiling (airborne, 219 uu at 1 s, was 70; v2 and v3 themselves differ by 168). Back-wall ramp unchanged and bad (670 uu at 2 s).
- Car-car bumps are 5 to 20% further from v3 than before (GPU was already 15 to 35 uu off at 1 s).
- The new P1-6 behaviour gives up the exact v2 match the GPU had on jumps and side dodges.
