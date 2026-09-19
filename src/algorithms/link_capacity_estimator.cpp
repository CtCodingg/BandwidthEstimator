#include "algorithms/link_capacity_estimator.hpp"

#include <algorithm>

#include "parameter_reader.hpp"

namespace bwe
{
namespace internal
{

LinkCapacityParameters LinkCapacityCore::ParseParameters(
	const Parameters& parameters)
{
	LinkCapacityParameters result;
	ParameterReader reader(kName, parameters);
	result.window_size = reader.GetInteger("window_size", result.window_size, 1,
										   MedianFilter::kMaxWindowSize);
	result.utilization = reader.Get("utilization", result.utilization,
									Range::OpenClosed(0.0, 1.0));
	result.smoothing =
		reader.Get("smoothing", result.smoothing, Range::OpenClosed(0.0, 1.0));
	reader.CheckNoUnknownKeys();
	return result;
}

LinkCapacityCore::LinkCapacityCore(const LinkCapacityParameters& parameters)
	: parameters_(parameters),
	  median_(parameters.window_size),
	  filter_(parameters.smoothing)
{
}

void LinkCapacityCore::Update(const IntervalSample& sample) noexcept
{
	if (!sample.link_capacity_bps || !(*sample.link_capacity_bps > 0.0))
	{
		return;
	}

	median_.Update(*sample.link_capacity_bps);
	double bits_per_second = parameters_.utilization * median_.Value();
	if (sample.loss_rate)
	{
		bits_per_second *= 1.0 - *sample.loss_rate;
	}
	if (sample.max_bandwidth_bps)
	{
		bits_per_second = std::min(bits_per_second, *sample.max_bandwidth_bps);
	}

	filter_.Update(bits_per_second);
	timestamp_ = sample.timestamp;
}

void LinkCapacityCore::Reset() noexcept
{
	median_.Reset();
	filter_.Reset();
	timestamp_ = Duration{0};
}

}  // namespace internal
}  // namespace bwe
