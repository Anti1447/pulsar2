# P1 interim finding (2026-11-24): the GPU engine matches v2 and CURRENT v3 on hits (this file first said otherwise; corrected below)

Measured with tests/gpu/gpu_engine_probe.cu (same scenarios as tools/engine_probe) against docs/engine_compare/FINDINGS.md:

| scenario | GPU | v2 | v3 |
|---|---|---|---|
| car hit, peak ball speed @ contact 941 / 1173 / 1237 uu/s | 1489 / 1850 / 1947 | 1493 / 1848 / 1942 | 874 / 1051 / 1104 |
| engine extraHitVel | 662 / 807 / 845 | 664 / 808 / 845 | 661 / 807 / 845 |
| ball into parked car, rebound at 500 / 1000 / 2000 | 95 / 198 / 515 | 230 / 165 / 500 | 0 / 0 / 0 |
| ticks within 160 uu of a parked car (1000 uu/s) | 0 | 0 | 137 |
| ball rolling at 1500: speed at 1 / 2 / 3 s | 1230 / 1017 / 996 | 1231 / 970 / 717 | 1230 / 969 / 715 |

Reading
- Car-ball hits: the GPU matches v2 to <0.5% and is ~70-80% stronger than v3. This is the dominant gameplay difference (v3 games run 2-3x longer, carries/dribbles, few hard hits - FINDINGS section 1). The v2-trained opponents (Nexto, Galactus, Chronos) were built for v2 hit physics.
- Ball rolling after the first second is slower to decay on the GPU than in BOTH CPU engines (996 vs 717 at 3 s). That is a fidelity bug candidate in the port itself (not a v2/v3 matter) and must be diagnosed before any training.
- Parked-car rebound is v2-like in character (nonzero rebound) but not equal to v2 (95 vs 230 at 500 uu/s).

Consequence for G1 as pre-registered (vs v3): Part A #10 and the policy-level metrics are expected to fail by construction. Decision needed: which engine is the reference. See the addendum in G1_PREREG.md.
Not done: the CPU adapter, Part A scenario harness, and the Part B policy runner. They were held back because the outcome depends on this decision.

## CORRECTION (same day)
The v3 column above and the conclusions drawn from it came from the 2026-11-06 FINDINGS table, which predates commit 1984b60 (rocketsim 0.3.5, "fixes the lost car-ball hit impulse"). Re-running the current `build/EngineProbe` (v3): peak ball speed 1497 / 1847 / 1942, rebound at a parked car 95 / 166 / 500, ticks within 160 uu 0, rolling 1230 / 969 / 715. Current v3 therefore matches v2 AND the GPU on hits, and equals the GPU on the 500 uu/s rebound. The only discrepancy confirmed first-hand is the ball rolling decay (GPU 996 vs 716/715 at 3 s). The statement that G1 against v3 "fails by construction" was unfounded and is withdrawn.
