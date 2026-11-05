# GigaLearnTests — offline unit tests. Never part of the trainer build: the top level
# only adds this directory under -DGGL_BUILD_TESTS=ON, so `cmake --build build` on the
# training box is unaffected.
file(GLOB TEST_SRC CONFIGURE_DEPENDS "*.cpp" "*.h")
add_executable(GigaLearnTests ${TEST_SRC})

set_target_properties(GigaLearnTests PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(GigaLearnTests PROPERTIES CXX_STANDARD 20)

target_link_libraries(GigaLearnTests GigaLearnCPP)

# Golden-state dumper for checklist item 1.2 (tools/opponent_golden): same OFF-by-default gate as the tests.
find_package(Torch REQUIRED)
# test_external_sampling.cpp needs torch tensors (the picker behind ExternalOpponent::temperature)
target_include_directories(GigaLearnTests PRIVATE ${TORCH_INCLUDE_DIRS})
target_link_libraries(GigaLearnTests ${TORCH_LIBRARIES})
add_executable(OpponentGolden ../tools/opponent_golden/golden_dump.cpp)
set_target_properties(OpponentGolden PROPERTIES CXX_STANDARD 20)
target_include_directories(OpponentGolden PRIVATE "${PROJECT_SOURCE_DIR}/GigaLearnCPP/src/private")
target_include_directories(OpponentGolden PRIVATE ${TORCH_INCLUDE_DIRS})
target_link_libraries(OpponentGolden GigaLearnCPP ${TORCH_LIBRARIES})

# Checklist 1.3: the opponents inside a training-style loop (tools/opponent_loop)
add_executable(OpponentLoop ../tools/opponent_loop/loop_check.cpp)
set_target_properties(OpponentLoop PROPERTIES CXX_STANDARD 20)
target_include_directories(OpponentLoop PRIVATE "${PROJECT_SOURCE_DIR}/GigaLearnCPP/src/private")
target_include_directories(OpponentLoop PRIVATE ${TORCH_INCLUDE_DIRS})
target_link_libraries(OpponentLoop GigaLearnCPP ${TORCH_LIBRARIES})

# Checklist 1.5: sim round robin of the external bots (tools/opponent_roundrobin)
add_executable(OpponentRoundRobin ../tools/opponent_roundrobin/round_robin.cpp)
set_target_properties(OpponentRoundRobin PROPERTIES CXX_STANDARD 20)
target_include_directories(OpponentRoundRobin PRIVATE "${PROJECT_SOURCE_DIR}/GigaLearnCPP/src/private")
target_include_directories(OpponentRoundRobin PRIVATE ${TORCH_INCLUDE_DIRS})
target_link_libraries(OpponentRoundRobin GigaLearnCPP ${TORCH_LIBRARIES})

# Observation scale measurement (tools/obs_stats)
add_executable(ObsStats ../tools/obs_stats/obs_stats.cpp)
set_target_properties(ObsStats PROPERTIES CXX_STANDARD 20)
target_include_directories(ObsStats PRIVATE "${PROJECT_SOURCE_DIR}/GigaLearnCPP/src/private")
target_include_directories(ObsStats PRIVATE ${TORCH_INCLUDE_DIRS})
target_link_libraries(ObsStats GigaLearnCPP ${TORCH_LIBRARIES})

# Galactus/Chronos golden test (our side; the reference programs are built by tools/opponent_golden/build_ref.sh)
add_executable(OpponentGoldenGGL ../tools/opponent_golden/golden_ggl.cpp)
set_target_properties(OpponentGoldenGGL PROPERTIES CXX_STANDARD 20)
target_include_directories(OpponentGoldenGGL PRIVATE "${PROJECT_SOURCE_DIR}/GigaLearnCPP/src/private" "${PROJECT_SOURCE_DIR}/tools/opponent_golden")
target_include_directories(OpponentGoldenGGL PRIVATE ${TORCH_INCLUDE_DIRS})
target_link_libraries(OpponentGoldenGGL GigaLearnCPP ${TORCH_LIBRARIES})
