# std::thread in the tests; the library itself needs no thread library.
find_package(Threads REQUIRED)

find_package(GTest CONFIG QUIET)

if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  # Build GoogleTest statically and with the shared CRT on Windows.
  set(BUILD_SHARED_LIBS OFF)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(bwe_tests
  aimd_estimator_test.cpp
  delay_estimator_test.cpp
  estimator_factory_test.cpp
  ewma_filter_test.cpp
  hybrid_estimator_test.cpp
  interval_tracker_test.cpp
  link_capacity_estimator_test.cpp
  mathis_estimator_test.cpp
  median_filter_test.cpp
  parameter_reader_test.cpp
  recording_test.cpp
  replay_test.cpp
  scenario_test.cpp
  send_buffer_estimator_test.cpp
  shared_channel_radio_test.cpp
  shared_channel_scenario_test.cpp
  shared_link_allocator_test.cpp
  tfrc_estimator_test.cpp
  thread_safety_test.cpp
  tsbpd_reserve_estimator_test.cpp
  support/link_simulator.cpp
  support/link_simulator.hpp
  support/shared_channel_simulator.cpp
  support/shared_channel_simulator.hpp
  ${BWE_INTERNAL_SOURCES}
)

target_compile_definitions(bwe_tests PRIVATE
  BWE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

# std::filesystem needs an extra library before GCC 9.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(bwe_tests PRIVATE stdc++fs)
endif()

target_include_directories(bwe_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bwe_tests PRIVATE
  BandwidthEstimator::BandwidthEstimator
  GTest::gtest_main
  Threads::Threads
)

# Place library and tests in one directory so the tests find the DLL on
# Windows.
set_target_properties(BandwidthEstimator bwe_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)

include(GoogleTest)
gtest_discover_tests(bwe_tests)
