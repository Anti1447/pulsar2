#!/usr/bin/env bash
# Builds the REFERENCE programs for the Galactus/Chronos golden test from the ORIGINAL bots\x27 own sources
# (fetched, not committed: ~/opponent_ref_src, see fetch_ref_src.py). Run from the repo root. Output: build/ref_galactus, build/ref_chronos.
set -euo pipefail
SRC=${REF_SRC:-$HOME/opponent_ref_src}
HERE=tools/opponent_golden
RS=GigaLearnCPP/RLGymCPP
RSM="$RS/RocketSim/src/Math/Math.cpp $RS/RocketSim/src/Math/MathTypes/MathTypes.cpp $RS/RocketSim/src/Sim/PhysState/PhysState.cpp"
JSON=GigaLearnCPP/libsrc/json
build() { # name define sources...
  local name=$1 def=$2; shift 2
  g++ -std=c++20 -O1 -w -D$def -I"$HERE/ref_stubs" -I"$SRC/$name/inc" -I"$RS" -I"$RS/RocketSim/libsrc/bullet3-3.24" -I"$JSON" \
    "$HERE/ref_obs.cpp" $RSM "$@" -o "build/ref_$name"
  echo "built build/ref_$name"
}
G=$SRC/galactus/inc/RLGymCPP; C=$SRC/chronos/inc/RLGymCPP
build galactus REF_GALACTUS $G/ObsBuilders/GalactusOBS.cpp $G/ActionParsers/DefaultAction.cpp $G/GameStates/StateUtil.cpp
build chronos REF_CHRONOS $C/ObsBuilders/AdvancedObsPadded.cpp $C/ObsBuilders/AdvancedObs.cpp $C/ActionParsers/DefaultAction.cpp $C/GameStates/StateUtil.cpp

# The original bots' own model code (GigaLearnCPP/Models.h, InferenceModels.cpp) against libtorch
TL=GigaLearnCPP/libtorch
buildmodel() { # name define
  local name=$1 def=$2
  g++ -std=c++20 -O1 -w -D$def -I"$HERE/ref_stubs" -I"$SRC/$name/inc" -I"$RS" -I"$RS/RocketSim/libsrc/bullet3-3.24" -I"$JSON" \
    -I"$TL/include" -I"$TL/include/torch/csrc/api/include" "$HERE/ref_model.cpp" "$SRC/$name/inc/GigaLearnCPP/InferenceModels.cpp" \
    $RS/RocketSim/src/Math/Math.cpp $RS/RocketSim/src/Math/MathTypes/MathTypes.cpp \
    -L"$TL/lib" -ltorch -ltorch_cpu -lc10 -Wl,-rpath,"$PWD/$TL/lib" -o "build/ref_model_$name"
  echo "built build/ref_model_$name"
}
buildmodel galactus REF_GALACTUS
buildmodel chronos REF_CHRONOS
