include(FetchContent)

# Prefer an existing GoogleTest installation, but fall back to fetching it
# automatically so the tests build without requiring a system-wide install
# (e.g. via vcpkg) or manual setup on Windows.
find_package(GTest QUIET)

if(NOT GTest_FOUND)
	message(STATUS "GTest not found on the system, fetching it via FetchContent")

	set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
	set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
	set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

	FetchContent_Declare(
		googletest
		GIT_REPOSITORY https://github.com/google/googletest.git
		GIT_TAG v1.15.2
	)
	FetchContent_MakeAvailable(googletest)

	add_library(GTest::GTest ALIAS gtest)
	add_library(GTest::Main ALIAS gtest_main)

	# Keep fetched GTest binaries out of the main build output; they are a
	# test-only dependency, not part of the library's output.
	set_target_properties(gtest gtest_main PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
		ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
		LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
	)
endif()

set(BWE_TEST_SOURCES
	EstimatorTest.cpp
	RecordReplayTest.cpp
	TfrcAlgorithmTest.cpp
)

add_executable(BandwidthEstimatorTests ${BWE_TEST_SOURCES})

target_link_libraries(BandwidthEstimatorTests PRIVATE BandwidthEstimator GTest::GTest GTest::Main)

set_target_properties(BandwidthEstimatorTests PROPERTIES
	CXX_EXTENSIONS OFF
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${BWE_TEST_SOURCES})

add_test(NAME BandwidthEstimatorTests COMMAND BandwidthEstimatorTests)
