#!/usr/bin/env bash
# Checklist 1.2, Nexto: dump the port's view of recorded sim states, then compare with the original. Run from the repo root.
set -euo pipefail
OUT=${1:-/tmp/golden/nexto.json}
mkdir -p "$(dirname "$OUT")"
(cd build && ./OpponentGolden "$OUT")
"${NEXTO_PY:-$HOME/nexto_ref_venv/bin/python}" tools/opponent_golden/compare_nexto.py "$OUT" rlbot-run/nexto
