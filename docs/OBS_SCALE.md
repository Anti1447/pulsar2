# Observation scale (measured 2026-11-05)

Question: should the 278-float observation be normalized (fixed per-feature scaling inside the model)? Measured with `tools/obs_stats` (`./build/ObsStats`, target `ObsStats`): the trainer's builder (AdvancedObsPadded, shuffle on, 8 ball-prediction slices), the training reset mix, tickSkip 8, actionDelay 7.
Three runs: random valid actions 1v1 (204,800 rows), random actions 3v3 (307,200 rows), and `GGL_OBS_BOTS=1` Galactus vs Chronos 1v1 (153,600 rows, closest to real play).

## Result: the observation is already on a unit scale
- **No feature has a standard deviation above 1.3** in any run (largest: index 66, std 1.24 with bots).
- **Only 6-8 features ever leave +-10**, reaching about +-13 at most: 63, 64, 66 and their mirrors in 1v1 (150, 151, 153, 179, 211 with bots), 65/66 and mirrors in 3v3. These are rare excursions, not the bulk of the distribution.
- **Constant features** (std 0): 64 in 1v1 (padded slots for absent players), 11 in 3v3 (padding and presence flags). Dividing by their std would divide by zero.
- **Near-constant flags** (std 0.03-0.1, mean 0.99): indices 17-50, which are almost always 1 even with two bots playing (a flag drops only for a short window), plus a few others (2, 53, 140, 149, 169, 178, 198, 207, the ball-prediction tail 232-277). Standardizing them would multiply a rare event by about 10-30x.

## Conclusion
Mean/std standardization (from these stats or a running estimate) is not warranted: the inputs are already O(1), and standardizing would amplify rare flag events and need a std floor for the constant padding. The existing `standardizeObs` flag is also unusable here (it aborts with `skillTracker` or `pipelinedCollection`, and it leaves the gauntlet, render and live bot on raw obs).
If an outlier fix is wanted, it is a handful of indices (63-66 and mirrors): a fixed scale or soft clip on those, inside the model, which would work on every surface. NOT built.

Limits: random play and two scripted bots stand in for the training distribution; a trained policy would visit other states (more aerial and fast play). Re-run the tool on a mid-run checkpoint's play before relying on it.
