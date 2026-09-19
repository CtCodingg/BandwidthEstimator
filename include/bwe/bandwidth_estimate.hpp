/// @file
/// @brief Output structure of the bandwidth estimators.

#pragma once

#include "bwe/measurement.hpp"

namespace bwe
{

/// @brief Result of a bandwidth estimation.
struct BandwidthEstimate
{
	/// @brief Estimated available bandwidth in bit/s.
	double bits_per_second = 0.0;
	/// @brief Confidence in the range [0, 1].
	double confidence = 0.0;
	/// @brief Timestamp of the latest measurement used.
	Duration timestamp{0};
	/// @brief False while there is not enough usable data.
	bool valid = false;
};

}  // namespace bwe
