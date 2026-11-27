# RocketGPU trial, stage 1 report (2026-11-25)

Plan: [ROCKETGPU_TRIAL_PLAN.md](ROCKETGPU_TRIAL_PLAN.md), approved with the research networks left out. Code: `~/pulsar_rg` (outside this repository; versions in its `VERSIONS.md`).

## Gate A: passed

| Requirement | Result |
|---|---|
| 230 state values within 0.00001 of Pulsar3's builder | largest difference 0.00000095 over 76,800 recorded 1v1 rows (slot shuffling off) |
| Action masks identical | 0 of 76,800 rows differ; the 90-row action table is identical |
| Ball-prediction difference (reported, not gated) | median 0.00 uu; within 5 uu: 100% at 0.25 s and 0.5 s, 99.7% at 1 s, 99.2% at 2 s (4,011 recorded ball states) |
| Pulsar3's weights give the same action probabilities within 0.0001 | largest difference 0.0000007; values 0.000001 |
| Exported weights read back by Pulsar3's code | perturbed weights: largest probability difference 0.0000019 |
| Export plays at Pulsar3's level in its evaluator | against the original: 570 to 531 goals (51.8% for the original); a random network concedes 99.3% of goals |

Also checked: each of the 34 boost pads, driven onto in the live engine, shows at the correct observation index for both teams; 900 random
steps on 4,096 arenas gave finite observations and always at least one legal action; a 45 s training run through RocketChad3 sampled
851,968 actions, none outside its mask.

## How the pieces work
- Observation and mask: `pulsar/obs.py` (engine-independent), `pulsar/engine.py` (RocketGPU arrays; ball prediction from a second car-free simulation).
- Masking: the 90-entry mask is appended to the observation and the network gives unavailable actions logit -1e10. RocketChad3's own code is **unmodified**
  (the plan expected a change inside it).
- Export: `pulsar/model.py` writes the four model files into a copy of a Pulsar3 checkpoint folder.

## Found along the way
- A ball placed by a state set is left frozen by `ball_path` unless its activation flag and idle timer are reset first. Handled in the predictor.
  Stage 2's state setters must be checked for the same thing in the main simulation.
- RocketGPU car arrays are strided 32-car tiles: writing through `flatten()` silently writes into a copy. State setters must index (tile, lane).

## Not covered
- 1v1 only. Teammate and opponent slot shuffling is implemented but only the unshuffled layout was compared.
- The comparison gives both builders the same state. It does not show that the two engines set flags (jump, flip, contact) the same way during play.
- Speed of the new observation code is unmeasured (gate B2).

## Needed in stage 2 or 3 beyond the plan's wording
Pulsar3's learner also drops the lower half of rows by advantage size from the policy loss, and uses its own Muon implementation. RocketChad3 has
neither in that form. Entropy converts as 0.035 / ln(90) because Pulsar3 does not normalize by the number of legal actions.
