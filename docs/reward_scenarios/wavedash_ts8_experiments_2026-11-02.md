# Wavedash experiments at tickSkip 8 (2026-11-02)

Context: `research/tools/oracle_wavedash.py` (oracle v1, ts4) was buggy (0.5% vs the bot's 11.2%), the repo declared the ts4 interface
ceiling hypothesis dead, and said the question for the ts8 interface is "a NEW experiment". This is that experiment, in C++ against the real
engine (`tests/test_wavedash_ts8_oracle.cpp`; the formal definition it serves is `docs/WAVEDASH_FORMAL.md`). Scripted executor, not an
optimal oracle. Nothing here ran in training.

## RETRACTIONS - the first version of this report was wrong
Two fixture bugs of mine produced numbers that I then interpreted:
1. **Mirrored orientation matrix.** For random headings I built `right = (fy, -fx, 0)`; this engine's convention is `right = (-fy, fx, 0)`
   (default spawn: forward (0,1,0), right (-1,0,0)). The car was a reflection, which produced the "-386 uu/s landing loss", "label 11.4% in
   the control arm (close to the repo's 11.2%)" and "label 72% after a nose-up tilt". **All of those are artifacts; retracted.**
2. **Stale dodge state.** Reusing one car across episodes without resetting `airTimeSinceJump` left it above the 1.25 s dodge limit in a random
   share of episodes, so some flips silently did not happen. Fixed: every episode now resets `airTimeSinceJump/airTime/flipTime/jumpTime`.
Also retracted from earlier today: "15 Hz captures 40-60% of true wavedashes" (came from a 1-tick press the bot cannot make).

## Setup (corrected)
A car falls forward with the dodge available (z 60-120, speed 600-1100, random heading, ball parked far). Decisions are 8 ticks with actions
held. The executor presses a forward flip (jump + pitch -1) for one decision when the predicted touchdown is <= k+0.5 decisions away; optional
nose tilt (pitch +1 up, -1 down) for the first `tilt` decisions; a **control** arm never flips. Label (repo census): horizontal speed 3 steps after
landing > speed the step before + 100 uu/s. Rules replayed on the end-of-step snapshots: **Basic** (stock `WavedashReward` / v3
`WavedashBasicReward`), **Rolv**, and candidate **R4** (pay clamp((gain-100)/400,0,1) once, 3 steps after a landing, if a jump input occurred
within 4 steps before to 3 after it; **R4n** = no jump condition).

## Pre-registered hypotheses and verdicts (corrected numbers)
Round 1 (level car):
- H1 best-offset label success >= 50%: 92.7% at k=0, but the control arm is 92.5% -> met on the raw number, **uninformative**.
- H2 Basic and Rolv recall < 60%, Rolv >= Basic: both 0% -> met trivially (they never fire).
- H3 R4 fires on <= 5% of control AND recall > 90%: control 0.0% yes; recall 78% at k=1, 0% elsewhere -> **rejected** on recall.
- H4 R4n fires materially more on control than R4: 92.5% vs 0.0% -> **supported**.

| arm (level car, N=2000) | label % | Basic fire % | Rolv fire % | R4 fire % | R4n fire % |
|---|---|---|---|---|---|
| control | 92.5 | 0 | 0 | 0 | 92.5 |
| k=0 | 92.7 | 0 | 0 | 0 | 92.7 |
| k=1 | 49.3 | 0 | 0 | 38.6 | 49.3 |
| k=2 | 11.7 | 0 | 0 | 0 | 11.7 |
| k=3 | 26.6 | 0 | 0 | 0 | 25.4 |

Round 2 (nose tilt):
- H5 some (tilt, k) cell lifts mean gain >= +100 above the same-tilt control while R4 fires: up1 k=1 gain +490 vs +149 control, R4 80% -> **supported**.
- H6 Basic and Rolv fire < 2% in every cell: up2 k=1 13.8% / 46.0%, up3 k=1 74.4% / 82.1% -> **rejected**.

| cell (N=1500) | label % | mean gain | Basic % | Rolv % | R4 % |
|---|---|---|---|---|---|
| level ctrl / k=1 (up0) | 93.3 / 49.4 | +145 / +296 | 0 / 0 | 0 / 0 | 0 / 38.9 |
| up1 ctrl / k=1 | 95.1 / 80.1 | +149 / +490 | 0 / 0 | 0 / 0.4 | 0 / 80.1 |
| up2 ctrl / k=1 | 93.0 / 86.2 | +146 / +467 | 0 / 13.8 | 0 / 46.0 | 0 / 86.2 |
| up3 ctrl / k=1 | 90.3 / 25.6 | +144 / +164 | 0 / 74.4 | 0 / 82.1 | 0 / 25.6 |
| dn3 ctrl / k=1 | 92.3 / 59.5 | +145 / +374 | 0 / 0.1 | 0 / 0.1 | 0 / 50.5 |

## What this establishes (corrected, pinned by the tests)
1. **Detector visibility at ts8 depends on the approach.** On a level approach Basic and Rolv never fire (0 of 10,000). After a nose-up approach they
   fire often (Basic up to 74%, Rolv up to 82%), because the flip then spills past the end-of-step snapshot.
2. **The census-style speed label is useless as a success definition here.** Throttle alone satisfies it in 90-95% of no-flip controls (mean
   +145 uu/s within 3 steps). Whether the repo's census has the same property was not checked (it also requires a jump input near the landing).
3. **A jump-input condition is necessary, and the gain threshold must be raised.** R4n pays 92.5% of controls; R4 pays none, but R4 as built
   pays any jump near a landing whose gain exceeds 100, i.e. mostly the throttle baseline. Flip-attributable gain is ~300-350 (best cell 490 vs 150).
4. **The mechanism matches the formal model.** The tick-level trace (`docs/WAVEDASH_FORMAL.md`, `tests/test_wavedash_formal.cpp`): a flip pressed
   1-2 ticks before first wheel contact is cancelled in 2-10 ticks and keeps +379..+422 uu/s; pressed 3+ ticks early it is a flip hop; nose down it
   is never cancelled.

## Limits
Scripted executor; forward flips only; one speed band and spawn family; N=1500-2000 per cell; the tick-level labeler for the formal definition is
not written yet, so recall/precision against any label here are not accuracy figures. Pay and fire rates are.
