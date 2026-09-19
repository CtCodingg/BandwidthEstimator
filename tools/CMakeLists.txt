# Test data generator; reuses the simulators of the tests.
add_executable(bwe_generate_test_data
  generate_test_data.cpp
  ${PROJECT_SOURCE_DIR}/tests/support/link_simulator.cpp
  ${PROJECT_SOURCE_DIR}/tests/support/shared_channel_simulator.cpp
)

target_include_directories(bwe_generate_test_data PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
)

target_link_libraries(bwe_generate_test_data PRIVATE
  BandwidthEstimator::BandwidthEstimator
)

# Next to the library so the DLL is found on Windows.
set_target_properties(BandwidthEstimator bwe_generate_test_data PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)
