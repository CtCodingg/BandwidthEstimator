cmake_minimum_required(VERSION 3.14)

project(BandwidthEstimator
  VERSION 0.1.0
  DESCRIPTION "Estimation of the available bandwidth from transport statistics"
  LANGUAGES CXX
)

# Build tests and tools by default only when this is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(BWE_IS_TOP_LEVEL ON)
else()
  set(BWE_IS_TOP_LEVEL OFF)
endif()

option(BWE_BUILD_SHARED "Build the shared library" ON)
option(BWE_BUILD_STATIC "Build the static library" ON)
# OFF: tests are neither configured nor built, GoogleTest is not searched
# for or downloaded.
option(BWE_BUILD_TESTS "Build the BandwidthEstimator unit tests" ${BWE_IS_TOP_LEVEL})
option(BWE_BUILD_TOOLS "Build the BandwidthEstimator tools" ${BWE_IS_TOP_LEVEL})

if(NOT BWE_BUILD_SHARED AND NOT BWE_BUILD_STATIC)
  message(FATAL_ERROR "Enable BWE_BUILD_SHARED, BWE_BUILD_STATIC or both.")
endif()

include(GNUInstallDirs)

# Executables and DLLs in one directory, so programs find the DLL on Windows.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# Solution folders in Visual Studio.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(BWE_PUBLIC_HEADERS
  ${PROJECT_SOURCE_DIR}/include/bwe/bandwidth_estimate.hpp
  ${PROJECT_SOURCE_DIR}/include/bwe/bandwidth_estimator.hpp
  ${PROJECT_SOURCE_DIR}/include/bwe/estimator_factory.hpp
  ${PROJECT_SOURCE_DIR}/include/bwe/export.hpp
  ${PROJECT_SOURCE_DIR}/include/bwe/measurement.hpp
  ${PROJECT_SOURCE_DIR}/include/bwe/recording.hpp
  ${PROJECT_SOURCE_DIR}/include/bwe/shared_link_allocator.hpp
)

# Implementation of the public API.
set(BWE_PUBLIC_SOURCES
  ${PROJECT_SOURCE_DIR}/src/estimator_factory.cpp
  ${PROJECT_SOURCE_DIR}/src/recording.cpp
  ${PROJECT_SOURCE_DIR}/src/shared_link_allocator.cpp
)

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

set(BWE_LIBRARY_SOURCES
  ${BWE_PUBLIC_HEADERS}
  ${BWE_PUBLIC_SOURCES}
  ${BWE_INTERNAL_SOURCES}
)

# Visual Studio filters mirror the directories.
source_group(TREE ${PROJECT_SOURCE_DIR} FILES ${BWE_LIBRARY_SOURCES})

# ---------------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------------
# Settings shared by the shared and the static library.
function(bwe_configure_library target)
  target_compile_features(${target} PUBLIC cxx_std_17)
  set_target_properties(${target} PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    FOLDER "BandwidthEstimator"
  )
  target_include_directories(${target}
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
      ${PROJECT_SOURCE_DIR}/src
  )
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
    )
  endif()
endfunction()

set(BWE_INSTALL_TARGETS)

if(BWE_BUILD_SHARED)
  add_library(BandwidthEstimatorShared SHARED ${BWE_LIBRARY_SOURCES})
  add_library(BandwidthEstimator::Shared ALIAS BandwidthEstimatorShared)
  bwe_configure_library(BandwidthEstimatorShared)
  target_compile_definitions(BandwidthEstimatorShared
    PRIVATE BWE_BUILDING_LIBRARY
  )
  # BandwidthEstimator.dll / libBandwidthEstimator.so
  set_target_properties(BandwidthEstimatorShared PROPERTIES
    OUTPUT_NAME BandwidthEstimator
    EXPORT_NAME Shared
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
  )
  list(APPEND BWE_INSTALL_TARGETS BandwidthEstimatorShared)
endif()

if(BWE_BUILD_STATIC)
  add_library(BandwidthEstimatorStatic STATIC ${BWE_LIBRARY_SOURCES})
  add_library(BandwidthEstimator::Static ALIAS BandwidthEstimatorStatic)
  bwe_configure_library(BandwidthEstimatorStatic)
  target_compile_definitions(BandwidthEstimatorStatic PUBLIC BWE_STATIC)
  # On Windows the import library of the DLL is BandwidthEstimator.lib, so
  # the static library needs another name: BandwidthEstimator_static.lib /
  # libBandwidthEstimator_static.a
  set_target_properties(BandwidthEstimatorStatic PROPERTIES
    OUTPUT_NAME BandwidthEstimator_static
    EXPORT_NAME Static
    POSITION_INDEPENDENT_CODE ON
  )
  list(APPEND BWE_INSTALL_TARGETS BandwidthEstimatorStatic)
endif()

# Default for projects using add_subdirectory(): the shared library if built.
if(BWE_BUILD_SHARED)
  add_library(BandwidthEstimator::BandwidthEstimator
    ALIAS BandwidthEstimatorShared)
else()
  add_library(BandwidthEstimator::BandwidthEstimator
    ALIAS BandwidthEstimatorStatic)
endif()

# Files without build step, shown in Visual Studio.
set(BWE_PROJECT_FILES
  ${PROJECT_SOURCE_DIR}/.clang-format
  ${PROJECT_SOURCE_DIR}/README.md
)
add_custom_target(BandwidthEstimatorProjectFiles SOURCES ${BWE_PROJECT_FILES})
set_target_properties(BandwidthEstimatorProjectFiles PROPERTIES
  FOLDER "BandwidthEstimator"
)
source_group(TREE ${PROJECT_SOURCE_DIR} FILES ${BWE_PROJECT_FILES})

# ---------------------------------------------------------------------------
# Installation (usable via find_package(BandwidthEstimator), providing
# BandwidthEstimator::Shared and BandwidthEstimator::Static)
# ---------------------------------------------------------------------------
install(TARGETS ${BWE_INSTALL_TARGETS}
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
