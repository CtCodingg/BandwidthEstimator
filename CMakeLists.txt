cmake_minimum_required(VERSION 3.14)

project(BandwidthEstimator
  VERSION 0.1.0
  DESCRIPTION "Estimation of the available bandwidth from transport statistics"
  LANGUAGES CXX
)

# Build tests by default only when this is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(BWE_IS_TOP_LEVEL ON)
else()
  set(BWE_IS_TOP_LEVEL OFF)
endif()

option(BUILD_SHARED_LIBS "Build BandwidthEstimator as a shared library" OFF)
# OFF: tests are neither configured nor built, GoogleTest is not searched
# for or downloaded.
option(BWE_BUILD_TESTS "Build the BandwidthEstimator unit tests" ${BWE_IS_TOP_LEVEL})
option(BWE_BUILD_TOOLS "Build the BandwidthEstimator tools" ${BWE_IS_TOP_LEVEL})

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
# Internal sources; also compiled into the tests to test non-exported code.
set(BWE_INTERNAL_SOURCES
  ${PROJECT_SOURCE_DIR}/src/parameter_reader.hpp
  ${PROJECT_SOURCE_DIR}/src/parameter_reader.cpp
  ${PROJECT_SOURCE_DIR}/src/filters/ewma_filter.hpp
  ${PROJECT_SOURCE_DIR}/src/filters/median_filter.hpp
  ${PROJECT_SOURCE_DIR}/src/interval_sample.hpp
  ${PROJECT_SOURCE_DIR}/src/interval_tracker.hpp
  ${PROJECT_SOURCE_DIR}/src/interval_tracker.cpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/mathis_estimator.hpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/mathis_estimator.cpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/side_adapter.hpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/tfrc_estimator.hpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/tfrc_estimator.cpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/aimd_estimator.hpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/aimd_estimator.cpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/send_buffer_estimator.hpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/send_buffer_estimator.cpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/delay_estimator.hpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/delay_estimator.cpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/link_capacity_estimator.hpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/link_capacity_estimator.cpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/tsbpd_reserve_estimator.hpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/tsbpd_reserve_estimator.cpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/hybrid_estimator.hpp
  ${PROJECT_SOURCE_DIR}/src/algorithms/hybrid_estimator.cpp
)

add_library(BandwidthEstimator
  include/bwe/bandwidth_estimate.hpp
  include/bwe/bandwidth_estimator.hpp
  include/bwe/estimator_factory.hpp
  include/bwe/export.hpp
  include/bwe/measurement.hpp
  include/bwe/recording.hpp
  include/bwe/shared_link_allocator.hpp
  src/estimator_factory.cpp
  src/recording.cpp
  src/shared_link_allocator.cpp
  ${BWE_INTERNAL_SOURCES}
)
add_library(BandwidthEstimator::BandwidthEstimator ALIAS BandwidthEstimator)

target_compile_features(BandwidthEstimator PUBLIC cxx_std_17)

set_target_properties(BandwidthEstimator PROPERTIES
  CXX_EXTENSIONS OFF
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(BandwidthEstimator
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(BandwidthEstimator PRIVATE BWE_BUILDING_LIBRARY)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(BandwidthEstimator PUBLIC BWE_STATIC)
endif()

if(MSVC)
  target_compile_options(BandwidthEstimator PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(BandwidthEstimator PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
  )
endif()

# ---------------------------------------------------------------------------
# Installation (usable via find_package(BandwidthEstimator))
# ---------------------------------------------------------------------------
install(TARGETS BandwidthEstimator
  EXPORT BandwidthEstimatorTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/bwe DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT BandwidthEstimatorTargets
  FILE BandwidthEstimatorConfig.cmake
  NAMESPACE BandwidthEstimator::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BandwidthEstimator
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  ${PROJECT_BINARY_DIR}/BandwidthEstimatorConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES ${PROJECT_BINARY_DIR}/BandwidthEstimatorConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BandwidthEstimator
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BWE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
if(BWE_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
