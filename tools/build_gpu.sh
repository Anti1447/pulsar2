#!/bin/bash
# Build and test the vendored RocketSim-CUDA in build-gpu/ (separate from the trainer's build/).
# usage: tools/build_gpu.sh [configure|build|test|all]   (default: all). Niced; safe next to the live trainer.
set -e
cd "$(dirname "$0")/.."
export PATH=/usr/local/cuda-12.8/bin:$PATH CUDACXX=/usr/local/cuda-12.8/bin/nvcc
SRC=gpu
step=${1:-all}
if [ "$step" = configure ] || [ "$step" = all ]; then
  cmake -S $SRC -B build-gpu -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=${GPU_ARCH:-86}
fi
if [ "$step" = build ] || [ "$step" = all ]; then
  nice -n 10 ninja -C build-gpu -j${JOBS:-6}
fi
if [ "$step" = test ] || [ "$step" = all ]; then
  (cd build-gpu && OMP_NUM_THREADS=4 nice -n 10 ctest --output-on-failure -j1 ${CTEST_ARGS})
fi
