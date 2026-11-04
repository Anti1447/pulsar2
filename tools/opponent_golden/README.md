# Opponent golden tests (checklist item 1.2)

Same sim-recorded states through the ORIGINAL bot code and our PORT, then diff.

## Nexto
```bash
# once: the reference environment (outside the repo)
python3 -m venv ~/nexto_ref_venv && . ~/nexto_ref_venv/bin/activate
pip install "numpy<2" torch==2.4.1 --index-url https://download.pytorch.org/whl/cpu --extra-index-url https://pypi.org/simple
pip install "git+https://github.com/JPK314/rlgym-compat" && pip install --pre "rlbot>=2.0.0b19,<3"

# every time (from the repo root, after `cmake -DGGL_BUILD_TESTS=ON` and `make OpponentGolden`)
tools/opponent_golden/run_nexto.sh
```
`golden_dump.cpp` (target `OpponentGolden`) records 192 states (1v1/2v2/3v3: every kickoff layout, random drives, aerials,
wall/ceiling, flips, demolished cars, spent pads, ball in corners and nets, both teams) and writes what the port builds
(`NextoOpponent::BuildObs`: q, kv), its logits on CPU and CUDA, and what `Act()` returns. `compare_nexto.py` rebuilds the
observation with the original `nexto_obs.py`, runs the original `nexto-model.pt`, and reports: first differing obs index
(entity row + feature name), max logit difference, argmax mismatches (near-ties listed apart), CUDA argmax, `Act()` vs
argmax, and whether Nexto's live entity order (self, mates, opponents) changes the answer. Thresholds: docs/Pre_Run_Checklist.md 1.0.
`GGL_GOLDEN_TF32=0` runs CUDA without TF32 (default on, like the trainer).

The reference states are duck-typed stand-ins for rlgym_compat's `V1GameState`; how `rlgym_compat` derives its fields from an RLBot
packet was checked by reading (docs/EXTERNAL_OPPONENTS.md). Golden JSON (about 9 MB) is regenerated, never committed.

Mutation check (2026-11-05): a negated orange velocity, reversed orange pad flags, and a dropped previous action in the
port's output were each caught, with the exact entity row and feature named.

## Galactus and Chronos
```bash
tools/opponent_golden/run_ggl.sh        # from the repo root; needs `make -C build OpponentGoldenGGL`
```
No Python here: the references are the bots' OWN C++ sources (GalactusV2 @ 217324c, Chronos @ 4f8fdd2), fetched as text into
`~/opponent_ref_src` (outside the repo, `fetch_ref_src.py`) and compiled by `build_ref.sh` into `build/ref_galactus`,
`build/ref_chronos` (observation builder, action mask, action parser) and `build/ref_model_*` (their `Models.h` /
`InferenceModels.cpp` loading the same `.lt` weights). `golden_ggl.cpp` (`OpponentGoldenGGL gen|judge`) records the states, builds
what the PORT builds, and compares: observation, mask, all 90 parsed action rows, the network's logits and pick, CUDA picks.

Notes: the originals shuffle their padded player slots with a random engine, so slots are compared as sets and the net runs on
slot-sorted copies; the shuffle's effect on the pick is printed as informational. Galactus's reference gets the same five
ball-prediction samples the port builds (its training tracker is stubbed: `ref_stubs/`), so the ball-prediction SOURCE is not
tested here (see EXTERNAL_OPPONENTS.md). Mutation check 2026-11-05: six seeded port faults (Chronos timers without the original's
typo, Galactus big-pad gate, a changed prediction tick, Chronos never mirroring, mirrored action not mapped back, wrong activation
function) were each caught.
