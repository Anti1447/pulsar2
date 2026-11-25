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

## Follow-up: off-centre hits (P1-8)
Cause: the GPU applied the contact's normal and friction impulses together and never revised them; Bullet iterates both with accumulated impulses.

| mean position error vs v3 (uu), after P1-6/7 -> after P1-8 | car 1 s | car 2 s | ball 1 s | ball 2 s |
|---|---|---|---|---|
| aerial hit, off-centre | 10.6 -> 1.1 | 51.6 -> 6.3 | 29.5 -> 5.0 | 53.7 -> 3.9 |
| ground hits (9) | 6.1 -> 7.6 | 21.4 -> 27.6 (worse) | 19.2 -> 11.8 | 32.6 -> 15.4 |
| flip into ball | 16.5 -> 16.6 | 29.3 -> 29.4 | 4.2 -> 2.5 | 2.0 -> 5.8 |
| carried ball | 2.1 -> 1.9 | 3.3 -> 3.0 | 1.5 -> 3.2 | 4.1 -> 9.0 (worse) |

v2 vs v3 on the off-centre aerial: 0.8 / 1.7 / 1.2 / 2.3, so a gap remains. Part A bands unchanged (36/43). The one-tick-earlier first touch is unchanged. The Part B rerun in progress at the time was built before P1-8.

## Second follow-up: contact order and drive latency (P1-9, P1-10)
| mean position error vs v3 (uu), car 1 s / 2 s; ball 1 s / 2 s | after P1-8 | after P1-10 |
|---|---|---|
| ground hits (9) | 7.6 / 27.6; 11.8 / 15.4 | 0.4 / 0.7; 7.7 / 13.7 |
| carried ball | 1.9 / 3.0; 3.2 / 9.0 | 0.3 / 1.0; 3.5 / 13.2 (ball at 2 s worse) |
| flips into / near ball (2) | 16.6 / 29.4; 2.5 / 5.8 | 12.9 / 21.5; 4.9 / 13.6 (ball worse) |
| double jump into ball | 7.7 / 14.9; 13.6 / 23.6 | 3.8 / 7.2; 4.4 / 9.9 |
| aerial hits (3) | about 1 / 4; 3.5 / 3 | 0.1 / 0.5; 0.3 / 0.8 |
| drive, steer, handbrake | 2.4 to 15 at 1 s | 0.3 or less at 1 s |
Part A bands: 36/43 vs v3, unchanged in count. Remaining flip error is mostly the car's own floor landing. The GPU now follows v3 rather than v2 on drive timing.
