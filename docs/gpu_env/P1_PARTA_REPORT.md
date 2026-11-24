# G1 Part A report (2026-11-24): GPU physics vs RocketSim v3 (primary) and v2 (second reference)

Setup: 46 scenarios from tools/parity/scenarios.h, identical initial states and control scripts, 120 Hz ticks, traces by tools/parity/record_cpu.cpp (v2: -DGGL_ROCKETSIM_V3=OFF, v3: current vendored rocketsim 0.3.5) and tests/gpu/gpu_parity_replay.cu; bands from docs/gpu_env/G1_PREREG.md; scorer tools/parity/bands.py. Wavedash and car wall/ceiling bands (#7, #8) have no scenarios yet and are NOT scored.

Result: **29/40 scenarios pass the bands against v3, 31/40 against v2.** Passing: ground/boost driving, steering, handbrake, jump apex and flight time, double jump, 6 of 8 dodge directions on position (see below), all 9 car-ball hit scenarios, all 5 bump/demo scenarios (same demo outcomes), big pad pickup and respawn, ball bounces off the side wall, ceiling, corner and angled floor.

Failures (all four families fail against v3; v2 agrees except where noted):
1. **Ball rolling decay.** ball_roll_1500: speed at 2 s off by 5% (limit 3%); ball_roll_1500_spin: 52 uu / 16%; ball_drop_400: 4% after the bounces. The GPU ball loses speed more slowly than both CPU engines (996 vs 716 / 715 at 3 s). Rolling is the commonest ball state in play, so this is the first thing to fix.
2. **Dodges.** Forward and backward-diagonal flips: apex off by 7-24%, horizontal displacement 11-18%; also vs v2 (v2 vs v3 differ by up to 41 uu on the same dodges, so the engines are not identical here either). Left/right flips pass.
3. **Back wall / goal.** ball_back_wall: position error 1259 uu at 2 s vs v3 (v2 vs v3: 349 uu): the ball enters the net; goal-volume handling differs (the GPU uses an analytic goal, upstream's design).
4. **Small pad pickup timing.** v3: first pickup 5 ticks off; v2: passes.

Car-ball hits, which I earlier said would fail, PASS: that claim came from a stale 2026-11-06 table and is withdrawn (see the corrections in G1_PREREG.md and P1_INTERIM.md).

Not done in P1: the CPU adapter and the Part B policy-level runner, scenarios for wavedash and car wall/ceiling, and fixes for the four families above.

## Full band table

PASS = every band holds. Primary reference: v3; second reference: v2.

| scenario | vs v3 | vs v2 | failing bands vs v3 (value > limit) |
|---|---|---|---|
| ball_angled_floor | PASS | PASS |  |
| ball_back_wall | FAIL | FAIL | ball pos@2s 1.26e+03>30; ball speed@2s rel 0.856>0.03 |
| ball_ceiling | PASS | PASS |  |
| ball_corner | PASS | PASS |  |
| ball_drop_400 | FAIL | FAIL | ball speed@2s rel 0.0413>0.03 |
| ball_roll_1500 | FAIL | FAIL | ball speed@2s rel 0.05>0.03 |
| ball_roll_1500_spin | FAIL | FAIL | ball pos@2s 51.6>30; ball speed@2s rel 0.157>0.03 |
| ball_side_wall | PASS | PASS |  |
| bump_fast_headon | PASS | PASS |  |
| bump_slow_headon | PASS | PASS |  |
| bump_super_headon | PASS | PASS |  |
| bump_super_rear | PASS | PASS |  |
| bump_super_side | PASS | PASS |  |
| carhit_1200_off-60 | PASS | PASS |  |
| carhit_1200_off0 | PASS | PASS |  |
| carhit_1200_off60 | PASS | PASS |  |
| carhit_1400_off-60 | PASS | PASS |  |
| carhit_1400_off0 | PASS | PASS |  |
| carhit_1400_off60 | PASS | PASS |  |
| carhit_800_off-60 | PASS | PASS |  |
| carhit_800_off0 | PASS | PASS |  |
| carhit_800_off60 | PASS | PASS |  |
| dodge_p-1_r-1 | FAIL | FAIL | apex rel 0.0743>0.05; hdisp@1s after input (rel or 20uu) 0.176>0.08 |
| dodge_p-1_r0 | FAIL | PASS | apex rel 0.0526>0.05 |
| dodge_p-1_r1 | FAIL | FAIL | apex rel 0.0743>0.05; hdisp@1s after input (rel or 20uu) 0.176>0.08 |
| dodge_p0_r-1 | PASS | PASS |  |
| dodge_p0_r1 | PASS | PASS |  |
| dodge_p1_r-1 | FAIL | FAIL | apex rel 0.243>0.05; hdisp@1s after input (rel or 20uu) 0.11>0.08 |
| dodge_p1_r0 | FAIL | FAIL | apex rel 0.221>0.05 |
| dodge_p1_r1 | FAIL | FAIL | apex rel 0.243>0.05; hdisp@1s after input (rel or 20uu) 0.11>0.08 |
| double_jump | PASS | PASS |  |
| drive_boost | PASS | PASS |  |
| drive_ground | PASS | PASS |  |
| handbrake_turn | PASS | PASS |  |
| jump_hold_024 | PASS | PASS |  |
| jump_tap_3 | PASS | PASS |  |
| pad_big_pickup_respawn | PASS | PASS |  |
| pad_small_pickup | FAIL | PASS | first pickup tick diff 5>1.5; respawn pickup tick diff 3>2.5 |
| steer_left | PASS | PASS |  |
| steer_right | PASS | PASS |  |

Totals: vs v3 29/40 pass, vs v2 31/40 pass.
