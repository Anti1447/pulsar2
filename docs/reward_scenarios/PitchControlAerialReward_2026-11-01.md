> Audit date 2026-11-01. Method: hand trace of the real header + compiled tests in `tests/test_reward_audit_movement.cpp`
> (14 pass). Fixture: scripted single-car states, deltaTime = 8/120. **Intent below was inferred from the class header
> and the v3 change log, not confirmed with Nathan - correct it and re-read the verdicts.** Not edited: the reward,
> `BuildRewards()`. Nothing here was run in the engine or in training.

# PitchControlAerialReward

**Intent (inferred):** pay the fast-aerial setup: jump, pitch up 25-65 degrees, then the second jump within 0.55 s.
**Class:** individual event (v3 wraps it `ZeroSumReward(.., 0.3f)`, 12 in Gold), no ball context. **Registered:** commented out.

## Scenarios (`PitchControlAerial_*`)

| tier | setup | traced value | verdict |
|---|---|---|---|
| easy | liftoff z=30, pitch 30 deg (+boost), double jump at z=150, payout step | base 2.0 + height 0.005*(150-30)=0.6 + pitch 0.875 = 3.475; **x1.5 = 5.2125** with boost | ALIGNED |
| easy | pitch 10 or 80 deg | 0 | ALIGNED |
| middle | flip instead of double jump (`hasDoubleJumped` false) | 0 | ALIGNED |
| middle | second jump at 0.6 s (window 0.55 s) | 0 | ALIGNED |
| middle | frame 0 | 0 | ALIGNED |
| hard | climb after the double jump | **no effect** on the payout (z raised from 400 to 1500 on the payout step: identical) | MISALIGNED with the Rev 2.0 comment |
| hard | taller first jump | payout grows | the bonus rewards pre-double-jump height |

## Latent defect
The deferred payout at the top of `GetReward` returns before the "track max height while SECOND_JUMP" block ever runs, so `maxHeightAfterSecondJump` is just z at the double-jump step. The Rev 2.0 note ("deferred to next tick so height can accumulate") does not happen; the height bonus (cap 3.0) measures the climb before the double jump. Magnitude is small (about 0.6 in the golden case); it is a comment/behavior mismatch, not an exploit.

## Farming
- No cooldown and no ball/height context. After a payout the state returns to WAITING, so a repeat needs a fresh liftoff from the ground (a landing). Ten fabricated cycles pay exactly 10 x 5.2125.
- Raw about 5.2 per cycle; at weight 12 that is about 62 per cycle, **about 63/s if a cycle takes 1.0 s** (an ASSUMED minimum real cycle), against Goal 150.
- Wrapping in `ZeroSumReward` cancels it against a mirror farmer only; against past selves or Nexto the looping side keeps the differential.
- Max payout (2 + 3 + 1) x 1.5 = 9.0.
- At 15 Hz the second jump must land in steps 1-8 after liftoff (0.067-0.533 s).
- UNVERIFIED: that the engine facade reports a pure double jump as `hasDoubleJumped` and a dodge as `hasFlipped` only (the class relies on a comment saying so).

## Verdict
Does what it says on the happy path and is **farmable by a hop loop with no ball involved**. It would need a ball/context gate (e.g. second jump only when the ball is within reach, or a payout tied to a touch) before it is safe. Pointer: `PitchControlAerialReward.h`, top of `GetReward` (deferred payout) and Phase 1.

## Pre-registration (if enabled after a fix)
Health: sequences per minute, share with a ball touch within 2 s; suspicious: sequences with no touch. Viewer: fast aerials toward the ball, not hops in open field.
