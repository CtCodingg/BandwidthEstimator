# Test data generator; reuses the simulators of the tests.
set(BWE_TOOL_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/generate_test_data.cpp
  ${PROJECT_SOURCE_DIR}/tests/support/link_simulator.cpp
  ${PROJECT_SOURCE_DIR}/tests/support/link_simulator.hpp
  ${PROJECT_SOURCE_DIR}/tests/support/shared_channel_simulator.cpp
  ${PROJECT_SOURCE_DIR}/tests/support/shared_channel_simulator.hpp
)

# Plot script; shown in Visual Studio, not compiled.
set(BWE_TOOL_SCRIPTS
  ${CMAKE_CURRENT_SOURCE_DIR}/plot_recording.py
)
set_source_files_properties(${BWE_TOOL_SCRIPTS} PROPERTIES
  HEADER_FILE_ONLY ON
)

add_executable(bwe_generate_test_data ${BWE_TOOL_SOURCES} ${BWE_TOOL_SCRIPTS})

# Visual Studio filters mirror the directories.
source_group(TREE ${PROJECT_SOURCE_DIR}
  FILES ${BWE_TOOL_SOURCES} ${BWE_TOOL_SCRIPTS}
)
set_target_properties(bwe_generate_test_data PROPERTIES
  FOLDER "BandwidthEstimator"
)

target_include_directories(bwe_generate_test_data PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
)

# The shared library if built: this also checks the DLL exports of the
# public API on every build.
if(TARGET BandwidthEstimatorShared)
  set(BWE_TOOL_LIBRARY BandwidthEstimator::Shared)
else()
  set(BWE_TOOL_LIBRARY BandwidthEstimator::Static)
endif()

target_link_libraries(bwe_generate_test_data PRIVATE ${BWE_TOOL_LIBRARY})
