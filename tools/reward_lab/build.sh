#!/bin/bash
# Build the Reward Lab runner without touching build/ (safe while the trainer runs).
#   tools/reward_lab/build.sh            -> build-lab/libRLGymCPP.a (current headers) and build-lab/reward_lab_run
#   tools/reward_lab/build.sh --tests    -> also build-lab/lab_tests (tests/test_reward_lab.cpp)
# Run from anywhere; it cd's to the repo root. Objects are cached per source file and rebuilt when the .cpp is newer; a changed
# HEADER does not invalidate them, so set FORCE=1 after editing headers the library includes (Reward.h, GameState.h, ...).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"
P="$ROOT/GigaLearnCPP"; O="$ROOT/build-lab"; mkdir -p "$O/obj"
FLAGS="-O3 -DNDEBUG -std=gnu++20 -fPIE -march=native -ffast-math -fno-finite-math-only -funroll-loops -DRG_ROCKETSIM_V3 -I$P/RLGymCPP/src -I$P/RLGymCPP/thread_pool -I$P/libsrc/json"
( cd "$P/RLGymCPP/src"
  find . -name '*.cpp' | while read f; do
    o="$O/obj/$(echo "$f" | sed 's#^\./##; s#/#__#g').o"
    if [ -n "$FORCE" ] || [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then echo "$f $o"; fi
  done | xargs -r -P "$(nproc)" -L1 bash -c "g++ $FLAGS -c \$0 -o \$1" )
rm -f "$O/libRLGymCPP.a"; ar rcs "$O/libRLGymCPP.a" "$O"/obj/*.o
LIBS="$O/libRLGymCPP.a $ROOT/build/libRocketSimV3.a $P/RLGymCPP/RocketSimV3/target/release/librocketsim_ffi.a -lpthread -ldl"
INC="-I$P/src/public -I$P/libsrc/json -I$P/RLGymCPP/src -I$P/RLGymCPP/thread_pool"
g++ -std=gnu++20 -O2 -DRG_ROCKETSIM_V3 $INC tools/reward_lab/reward_lab_run.cpp -o "$O/reward_lab_run" $LIBS
echo "built $O/reward_lab_run"
if [ "$1" = "--tests" ]; then
  g++ -std=gnu++20 -O1 -DRG_ROCKETSIM_V3 -Itests $INC tests/TestMain.cpp tests/test_reward_lab.cpp tests/test_reward_lab_export.cpp -o "$O/lab_tests" $LIBS
  echo "built $O/lab_tests (run from the repo root)"
fi
