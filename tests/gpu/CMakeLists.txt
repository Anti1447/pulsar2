# GPU port test suite (docs/GPU_ENV_PLAN.md, "Unit and parity test suite"). One executable per layer; ctest labels:
#   unit = no oracle, parity = GPU vs CPU oracle, gpu = needs a CUDA device, slow = gate / nightly runs.
set(GPU_TESTS_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/gpu_test_main.cpp)
function(pulsar_gpu_test name)
    add_executable(${name} ${name}.cu ${GPU_TESTS_COMMON})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. )
    target_link_libraries(${name} PRIVATE rocketsim_cuda_core)
    set_target_properties(${name} PROPERTIES CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "${ARGN}")
endfunction()

pulsar_gpu_test(test_l6_team_layout unit;gpu)
pulsar_gpu_test(test_l6_reset_isolation unit;gpu)
pulsar_gpu_test(test_l1_bump_demo_events unit;gpu)
pulsar_gpu_test(test_l1_ball_ground unit;gpu;parity)

# Hand-run diagnostics (not ctest): the GPU twin of tools/engine_probe.
add_executable(gpu_engine_probe gpu_engine_probe.cu)
target_include_directories(gpu_engine_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gpu_engine_probe PRIVATE rocketsim_cuda_core)
set_target_properties(gpu_engine_probe PROPERTIES CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")

add_executable(gpu_parity_replay gpu_parity_replay.cu)
target_include_directories(gpu_parity_replay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gpu_parity_replay PRIVATE rocketsim_cuda_core)
set_target_properties(gpu_parity_replay PROPERTIES CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
