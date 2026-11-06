# RocketSim v2 vs v3 (2026-11-06)

Why the v2-trained bots (Galactus, Chronos, Nexto) play differently in our v3 sim.

## 1. Round robin (rr_v2_*.md vs rr_v3_*.md, 200 games per pairing, same seed)
- Games last 2-3x longer in v3 (mirror Galactus: 34 s -> 64 s) and 10-40% end as 90 s draws (v2: about 0-6%).
- Ball touches per game: v2 about 26, v3 about 200-365. Mean car speed is lower in v3 (about 800-1000 vs 1230-1500 uu/s).
  The ball stays on the cars: carries and dribbles, no hard hits.

## 2. Engine probe (tools/engine_probe, `EngineProbe` in both builds, same scenarios)
| scenario | v2 | v3 |
|---|---|---|
| car at 945 uu/s hits a resting ball: peak ball speed | 1493 | 874 |
| car at 1176 uu/s | 1848 | 1051 |
| car at 1238 uu/s | 1942 | 1104 |
| engine-reported extraHitVel (the extra car-ball impulse), at 945 / 1176 / 1238 | 664 / 808 / 845 | 661 / 807 / 845 |
| ball rolling at a parked car: rebound speed (500 / 1000 / 2000 uu/s) | 230 / 165 / 500 | 0 / 0 / 0 |
| ball stays within 160 uu of a parked car, 1000 uu/s, ticks | 0 | 137 |
| ground drag (1500 uu/s ball after 1 / 2 / 3 s) | 1231 / 970 / 717 | 1230 / 969 / 715 (identical) |

The extra car-ball impulse is COMPUTED identically in v3 (same values as v2), but the ball does not end up with it: hits come out about 45%
weaker and a ball driven into a car does not bounce back. In v2 the impulse is added to the ball velocity after the tick's solve
(`Ball::_velocityImpulseCache`); in v3 it goes into `RigidBody::accum_lin_vel`, which is handed to the constraint solver as
`external_force_impulse` (`solver_body.rs:47`), and the solver appears to cancel it while the car and ball are in contact.
Not yet fixed or confirmed at that level; this is the lead.

Ground drag and gravity-free rolling match, so the difference is specific to car-ball contact.
