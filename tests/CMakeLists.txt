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
add_executable(OpponentGolden ../tools/opponent_golden/golden_dump.cpp)
set_target_properties(OpponentGolden PROPERTIES CXX_STANDARD 20)
target_include_directories(OpponentGolden PRIVATE "${PROJECT_SOURCE_DIR}/GigaLearnCPP/src/private")
target_include_directories(OpponentGolden PRIVATE ${TORCH_INCLUDE_DIRS})
target_link_libraries(OpponentGolden GigaLearnCPP ${TORCH_LIBRARIES})
