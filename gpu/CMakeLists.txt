# Umbrella project for the GPU environment port (docs/GPU_ENV_PLAN.md). Built by tools/build_gpu.sh into build-gpu/.
# Independent of the trainer's CMake; the trainer links nothing from here until phase P5 (option GGL_GPU_ENV).
cmake_minimum_required(VERSION 3.24)
project(PulsarGPU LANGUAGES CXX CUDA)
enable_testing()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../GigaLearnCPP/libsrc/RocketSimCUDA rsc)

add_library(pulsar_gpu_adapter STATIC adapter/ArenaBatch.cu)
set_target_properties(pulsar_gpu_adapter PROPERTIES POSITION_INDEPENDENT_CODE ON CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
target_include_directories(pulsar_gpu_adapter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/adapter)
target_link_libraries(pulsar_gpu_adapter PUBLIC rocketsim_cuda_core)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tests/gpu tests_gpu)
