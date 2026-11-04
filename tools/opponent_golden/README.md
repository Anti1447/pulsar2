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
