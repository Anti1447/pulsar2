# Umbrella project for the GPU environment port (docs/GPU_ENV_PLAN.md). Built by tools/build_gpu.sh into build-gpu/.
# Independent of the trainer's CMake; the trainer links nothing from here until phase P5 (option GGL_GPU_ENV).
cmake_minimum_required(VERSION 3.24)
project(PulsarGPU LANGUAGES CXX CUDA)
enable_testing()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../GigaLearnCPP/libsrc/RocketSimCUDA rsc)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tests/gpu tests_gpu)
