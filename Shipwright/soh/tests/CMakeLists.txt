add_executable(soh_behavior_tests
    camera_at_default_policy_tests.cpp
    cosmetics_editor_layout_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/zelda3d/behaviors/camera/at_default_policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../soh/Enhancements/cosmetics/CosmeticsEditorLayout.cpp
)

target_include_directories(soh_behavior_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/zelda3d/behaviors
    ${CMAKE_CURRENT_SOURCE_DIR}/../soh/Enhancements/cosmetics
)
target_compile_features(soh_behavior_tests PRIVATE cxx_std_20)
target_link_libraries(soh_behavior_tests PRIVATE gtest_main)

include(GoogleTest)
gtest_discover_tests(soh_behavior_tests)
