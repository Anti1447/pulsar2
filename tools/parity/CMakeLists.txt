# Standalone CPU recorder for the GPU parity work (docs/GPU_ENV_PLAN.md, G1 Part A). No libtorch, no trainer build touched.
#   v3: cmake -S tools/parity -B build-parity-v3 -DGGL_ROCKETSIM_V3=ON    v2: ... -B build-parity-v2 -DGGL_ROCKETSIM_V3=OFF
cmake_minimum_required(VERSION 3.16)
project(ParityRecorder CXX)
set(CMAKE_CXX_STANDARD 20)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../GigaLearnCPP/RLGymCPP RLGymCPP)
add_executable(parity_record record_cpu.cpp)
target_link_libraries(parity_record PRIVATE RLGymCPP)
set_target_properties(parity_record PROPERTIES CXX_STANDARD 20)
add_executable(parity_bench bench_cpu.cpp)
target_link_libraries(parity_bench PRIVATE RLGymCPP)
set_target_properties(parity_bench PROPERTIES CXX_STANDARD 20)
