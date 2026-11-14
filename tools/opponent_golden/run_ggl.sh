#!/usr/bin/env bash
# Checklist 1.2, Galactus + Chronos: the ORIGINAL bots' own sources vs our port, same recorded sim states.
# Run from the repo root, after `cmake -DGGL_BUILD_TESTS=ON build && make -C build OpponentGoldenGGL`.
set -euo pipefail
OUT=${1:-/tmp/golden}
mkdir -p "$OUT"
[ -d "$HOME/opponent_ref_src/galactus" ] && [ -d "$HOME/opponent_ref_src/dice" ] || python3 tools/opponent_golden/fetch_ref_src.py   # text only, pinned commits
tools/opponent_golden/build_ref.sh
cd build
./OpponentGoldenGGL gen "$OUT/ggl_states.json" | tail -1
./ref_galactus "$OUT/ggl_states.json" "$OUT/ref_g.json"
./ref_chronos "$OUT/ggl_states.json" "$OUT/ref_c.json"
./ref_dice "$OUT/ggl_states.json" "$OUT/ref_d.json"
./ref_model_galactus "$OUT/ref_g.json" ../rlbot-run/galactus_v2 "$OUT/model_g.json"
./ref_model_chronos "$OUT/ref_c.json" ../rlbot-run/chronos_v2 "$OUT/model_c.json"
./ref_model_dice "$OUT/ref_d.json" ../rlbot-run/dice "$OUT/model_d.json"
./OpponentGoldenGGL judge "$OUT/ggl_states.json" "$OUT/ref_g.json" "$OUT/ref_c.json" "$OUT/ref_d.json" "$OUT/model_g.json" "$OUT/model_c.json" "$OUT/model_d.json" 2>&1 | grep -v '^GGLBotOpponent'
