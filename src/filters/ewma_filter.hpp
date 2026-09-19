/// @file
/// @brief Exponentially weighted moving average with confidence.

#pragma once

#include <algorithm>

#include "bwe/bandwidth_estimate.hpp"
#include "bwe/measurement.hpp"

namespace bwe
{
namespace internal
{

/// @brief Exponentially weighted moving average.
///
/// The first value is taken as is. The confidence rises linearly with the
/// number of values and reaches 1 after `full_confidence_samples` values.
class EwmaFilter
{
public:
	/// @brief Default number of values for full confidence.
	static constexpr int kDefaultFullConfidenceSamples = 5;

	/// @param smoothing Weight of a new value in (0, 1]; 1 disables smoothing.
	/// @param full_confidence_samples Values needed for confidence 1; >= 1.
	explicit EwmaFilter(
		double smoothing,
		int full_confidence_samples = kDefaultFullConfidenceSamples) noexcept
		: smoothing_(smoothing),
		  full_confidence_samples_(std::max(full_confidence_samples, 1))
	{
	}

	/// @brief Adds a value.
	void Update(double value) noexcept
	{
		value_ = samples_ == 0 ? value : value_ + smoothing_ * (value - value_);
		samples_ = std::min(samples_ + 1, full_confidence_samples_);
	}

	/// @brief Returns true once a value has been added.
	bool HasValue() const noexcept
	{
		return samples_ > 0;
	}

	/// @brief Returns the smoothed value; 0 without values.
	double Value() const noexcept
	{
		return value_;
	}

	/// @brief Returns the confidence in [0, 1].
	double Confidence() const noexcept
	{
		return static_cast<double>(samples_) / full_confidence_samples_;
	}

	/// @brief Discards all values.
	void Reset() noexcept
	{
		value_ = 0.0;
		samples_ = 0;
	}

private:
	double smoothing_;
	int full_confidence_samples_;
	double value_ = 0.0;
	int samples_ = 0;
};

/// @brief Builds an estimate from the state of a filter.
inline BandwidthEstimate MakeEstimate(const EwmaFilter& filter,
									  Duration timestamp) noexcept
{
	BandwidthEstimate estimate;
	estimate.bits_per_second = filter.Value();
	estimate.confidence = filter.Confidence();
	estimate.timestamp = timestamp;
	estimate.valid = filter.HasValue();
	return estimate;
}

}  // namespace internal
}  // namespace bwe
