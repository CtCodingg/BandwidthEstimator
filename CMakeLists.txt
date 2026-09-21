cmake_minimum_required(VERSION 3.11)

project(BandwidthEstimator VERSION 1.0.0 LANGUAGES CXX)

option(BWE_BUILD_TESTS "Build the unit tests (requires GoogleTest)" ON)
option(BWE_BUILD_EXAMPLES "Build the example application" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(BWE_HEADERS
	include/bwe/AimdAlgorithm.hpp
	include/bwe/AlgorithmFactory.hpp
	include/bwe/Estimator.hpp
	include/bwe/IAlgorithm.hpp
	include/bwe/Player.hpp
	include/bwe/Recorder.hpp
	include/bwe/TfrcAlgorithm.hpp
	include/bwe/Types.hpp
)

set(BWE_SOURCES
	src/AimdAlgorithm.cpp
	src/AlgorithmFactory.cpp
	src/Estimator.cpp
	src/Player.cpp
	src/Recorder.cpp
	src/TfrcAlgorithm.cpp
)

add_library(BandwidthEstimator STATIC ${BWE_HEADERS} ${BWE_SOURCES})

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

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${BWE_HEADERS} ${BWE_SOURCES})

install(TARGETS BandwidthEstimator ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

if(BWE_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

if(BWE_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()
