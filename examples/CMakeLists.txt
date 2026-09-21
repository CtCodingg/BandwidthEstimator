add_executable(BandwidthEstimatorExample Example.cpp)

target_link_libraries(BandwidthEstimatorExample PRIVATE BandwidthEstimator)

set_target_properties(BandwidthEstimatorExample PROPERTIES
	CXX_EXTENSIONS OFF
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES Example.cpp)
