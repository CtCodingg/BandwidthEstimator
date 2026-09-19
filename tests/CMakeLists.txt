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
  foreach(target gtest gtest_main gmock gmock_main)
    if(TARGET ${target})
      set_target_properties(${target} PROPERTIES FOLDER "external")
    endif()
  endforeach()
endif()

set(BWE_TEST_SOURCES
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
)
list(TRANSFORM BWE_TEST_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)

# Recordings for the replay test; shown in Visual Studio, not compiled.
file(GLOB BWE_TEST_DATA CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/data/*.csv
)
set_source_files_properties(${BWE_TEST_DATA} PROPERTIES HEADER_FILE_ONLY ON)

add_executable(bwe_tests
  ${BWE_TEST_SOURCES}
  ${BWE_TEST_DATA}
  ${BWE_INTERNAL_SOURCES}
)

# Visual Studio filters mirror the directories.
source_group(TREE ${PROJECT_SOURCE_DIR}
  FILES ${BWE_TEST_SOURCES} ${BWE_TEST_DATA} ${BWE_INTERNAL_SOURCES}
)
set_target_properties(bwe_tests PROPERTIES FOLDER "BandwidthEstimator")

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

# The static library if built, otherwise the shared one.
if(TARGET BandwidthEstimatorStatic)
  set(BWE_TESTED_LIBRARY BandwidthEstimator::Static)
else()
  set(BWE_TESTED_LIBRARY BandwidthEstimator::Shared)
endif()

target_link_libraries(bwe_tests PRIVATE
  ${BWE_TESTED_LIBRARY}
  GTest::gtest_main
  Threads::Threads
)

include(GoogleTest)
gtest_discover_tests(bwe_tests)
