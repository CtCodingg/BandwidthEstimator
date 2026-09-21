cmake_minimum_required(VERSION 3.11)

project(BandwidthEstimator VERSION 1.0.0 LANGUAGES CXX)

option(BWE_BUILD_TESTS "Build the unit tests (requires GoogleTest)" ON)
option(BWE_BUILD_EXAMPLES "Build the example application" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(BWE_HEADERS
	include/bwe/AlgorithmFactory.hpp
	include/bwe/Estimator.hpp
	include/bwe/IAlgorithm.hpp
	include/bwe/Player.hpp
	include/bwe/Recorder.hpp
	include/bwe/Types.hpp
)

# Concrete IAlgorithm implementations: intentionally not under include/, so they are not installed
# and not part of the public API. Select one via Config::algorithm + AlgorithmFactory, or pass any
# other IAlgorithm to Estimator's raw-algorithm constructor. Listed here (and in add_library()
# below) only so they show up in IDEs; this does not install or expose them.
set(BWE_PRIVATE_HEADERS
	src/AimdAlgorithm.hpp
	src/RttTrendAlgorithm.hpp
	src/TfrcAlgorithm.hpp
)

set(BWE_SOURCES
	src/AimdAlgorithm.cpp
	src/AlgorithmFactory.cpp
	src/Estimator.cpp
	src/Player.cpp
	src/Recorder.cpp
	src/RttTrendAlgorithm.cpp
	src/TfrcAlgorithm.cpp
)

add_library(BandwidthEstimator STATIC ${BWE_HEADERS} ${BWE_PRIVATE_HEADERS} ${BWE_SOURCES})

target_include_directories(BandwidthEstimator
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
)

target_compile_features(BandwidthEstimator PUBLIC cxx_std_17)

set_target_properties(BandwidthEstimator PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
	target_compile_options(BandwidthEstimator PRIVATE /W4)
else()
	target_compile_options(BandwidthEstimator PRIVATE -Wall -Wextra -Wpedantic)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${BWE_HEADERS} ${BWE_PRIVATE_HEADERS} ${BWE_SOURCES})

install(TARGETS BandwidthEstimator ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

if(BWE_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

if(BWE_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()
