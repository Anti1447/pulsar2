cmake_minimum_required(VERSION 3.24)
project(RocketSim-CUDA VERSION 0.1.8 LANGUAGES CXX CUDA)

# --- Pulsar patches (see PATCHES.md) -------------------------------------------------------------------------------
option(RSC_BUILD_CPU_ORACLE "Build the Bullet-based CPU reference lib (needed by the differential harness)" ON)
option(RSC_BUILD_TESTS "Build unit tests and the differential harness" ON)
option(RSC_BUILD_BENCH "Build the throughput and mixed-mode benchmarks" ON)
option(RSC_BUILD_PYTHON "Build the nanobind Python module (unused by the trainer)" OFF)
find_package(CUDAToolkit REQUIRED)

# C++20 and CUDA standards
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)

# CUDA Architectures (GEMINI.md: Ampere sm_80, Ada sm_89, Hopper sm_90, Blackwell)
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES "80;86;89;90")
endif()

# Strict IEEE-754 Numerical Precision Invariants (GEMINI.md Section 2.3)
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++20 /utf-8 /permissive- /Zc:__cplusplus /EHsc")
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --fmad=false --prec-div=true --prec-sqrt=true -ftz=false -allow-unsupported-compiler -Xcompiler=/std:c++20,/utf-8,/permissive-,/Zc:__cplusplus,/EHsc")
else()
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --fmad=false --prec-div=true --prec-sqrt=true -ftz=false")
endif()

# -----------------------------------------------------------------------------
# Target 1: rocketsim_cpu (Ground Truth Oracle Library)
# -----------------------------------------------------------------------------
set(ROCKETSIM_CPU_SOURCES
    src/BulletLink.cpp
    src/CollisionMeshFile/CollisionMeshFile.cpp
    src/Math/Math.cpp
    src/Math/MathTypes/MathTypes.cpp
    src/RocketSim.cpp
    src/Sim/Arena/Arena.cpp
    src/Sim/Arena/ArenaConfig/ArenaConfig.cpp
    src/Sim/Arena/DropshotTiles/DropshotTiles.cpp
    src/Sim/Ball/Ball.cpp
    src/Sim/BallHitInfo/BallHitInfo.cpp
    src/Sim/BallPredTracker/BallPredTracker.cpp
    src/Sim/BoostPad/BoostPad.cpp
    src/Sim/BoostPad/BoostPadGrid/BoostPadGrid.cpp
    src/Sim/Car/Car.cpp
    src/Sim/Car/CarConfig/CarConfig.cpp
    src/Sim/GameEventTracker/GameEventTracker.cpp
    src/Sim/MutatorConfig/MutatorConfig.cpp
    src/Sim/PhysState/PhysState.cpp
    src/Sim/btVehicleRL/btVehicleRL.cpp
)
file(GLOB_RECURSE BULLET3_SOURCES "libsrc/bullet3-3.24/*.cpp")

if(RSC_BUILD_CPU_ORACLE)
add_library(rocketsim_cpu STATIC ${ROCKETSIM_CPU_SOURCES} ${BULLET3_SOURCES})
add_library(RocketSim::CPU ALIAS rocketsim_cpu)

target_include_directories(rocketsim_cpu PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libsrc/bullet3-3.24>
)
endif()

# -----------------------------------------------------------------------------
# Target 2: rocketsim_cuda_core (GPU Physics & SoA Memory Engine)
# -----------------------------------------------------------------------------
set(ROCKETSIM_CUDA_SOURCES
    src/cuda/sim_context.cu
    src/cuda/step_kernel.cu
    src/cuda/arena_mesh.cu
)

add_library(rocketsim_cuda_core STATIC ${ROCKETSIM_CUDA_SOURCES})
add_library(RocketSim::CUDA ALIAS rocketsim_cuda_core)

set_target_properties(rocketsim_cuda_core PROPERTIES POSITION_INDEPENDENT_CODE ON CUDA_SEPARABLE_COMPILATION OFF)
target_include_directories(rocketsim_cuda_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(rocketsim_cuda_core PUBLIC CUDA::cudart)

if(RSC_BUILD_TESTS AND RSC_BUILD_CPU_ORACLE)
# -----------------------------------------------------------------------------
# Target 3: differential_harness (Differential Testing & Golden Master)
# -----------------------------------------------------------------------------
add_executable(differential_harness
    tests/differential/harness_main.cpp
    tests/differential/cpu_ref_sim.cpp
    tests/differential/golden_master.cpp
    tests/differential/scenarios/scenario_registry.cpp
    tests/differential/scenarios/bounce_scenarios.cpp
    tests/differential/scenarios/car_scenarios.cpp
    tests/differential/scenarios/ablation_scenarios.cpp
    tests/differential/scenarios/arena_scenarios.cpp
    tests/differential/scenarios/multicar_scenarios.cpp
    tests/differential/scenarios/random_scenarios.cpp
)

target_include_directories(differential_harness PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/differential
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/differential/scenarios
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libsrc/bullet3-3.24
)

target_link_libraries(differential_harness PRIVATE
    rocketsim_cpu
    rocketsim_cuda_core
)

endif()

if(RSC_BUILD_PYTHON)
# -----------------------------------------------------------------------------
# Target 4: rocketsim_cuda (Nanobind Python Module with DLPack Zero-Copy)
# -----------------------------------------------------------------------------
find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)

execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT)
list(APPEND CMAKE_PREFIX_PATH "${nanobind_ROOT}")
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(rocketsim_cuda
    NB_DOMAIN rocketsim_cuda
    src/bindings/nanobind_module.cpp
)

target_include_directories(rocketsim_cuda PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rocketsim_cuda PRIVATE
    rocketsim_cuda_core
    CUDA::cudart
)

endif()

# -----------------------------------------------------------------------------
# Unit tests (upstream L0) and benchmarks
# -----------------------------------------------------------------------------
if(RSC_BUILD_TESTS)
    foreach(t test_math test_soa test_sdf test_integrator)
        add_executable(${t} tests/unit/${t}.cu)
        target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(${t} PRIVATE rocketsim_cuda_core)
    endforeach()
endif()

if(RSC_BUILD_BENCH)
    foreach(b throughput_benchmark mix_benchmark)
        add_executable(${b} benchmarks/${b}.cu)
        target_include_directories(${b} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(${b} PRIVATE rocketsim_cuda_core)
    endforeach()
    # cpu_readme_benchmark and unified_soccar_benchmark are not built: the latter includes MSVC <intrin.h>
endif()

# CTest integration (labels: unit = no oracle; parity = GPU vs CPU oracle)
if(RSC_BUILD_TESTS)
    enable_testing()
    add_test(NAME RSC_TestMath COMMAND test_math)
    add_test(NAME RSC_TestSoA COMMAND test_soa)
    add_test(NAME RSC_TestSdf COMMAND test_sdf)
    add_test(NAME RSC_TestIntegrator COMMAND test_integrator)
    set_tests_properties(RSC_TestMath RSC_TestSoA RSC_TestSdf RSC_TestIntegrator PROPERTIES LABELS "unit;gpu")
    if(RSC_BUILD_CPU_ORACLE)
        # Report mode always exits 0 and writes the parity table (data, not a gate).
        add_test(NAME RSC_DifferentialReport COMMAND differential_harness --ticks 120 --envs 4 --report --scenario all
                 --out-report ${CMAKE_BINARY_DIR}/rsc_parity_report.md)
        # Upstream's own fail-fast gate breaks on `idle` at tick 22 (angular velocity 1.4e-4 vs 1e-4) on sm_86.
        # Recorded as an EXPECTED failure: if upstream or our patches fix it, this test flips and tells us.
        add_test(NAME RSC_DifferentialIdleKnownFail COMMAND differential_harness --scenario idle --envs 4 --ticks 500)
        set_tests_properties(RSC_DifferentialIdleKnownFail PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(RSC_DifferentialReport RSC_DifferentialIdleKnownFail PROPERTIES LABELS "parity;gpu")
    endif()
endif()
