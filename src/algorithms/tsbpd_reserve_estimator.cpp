#include "algorithms/tsbpd_reserve_estimator.hpp"

#include <algorithm>

#include "parameter_reader.hpp"

namespace bwe
{
namespace internal
{

TsbpdReserveParameters TsbpdReserveCore::ParseParameters(
	const Parameters& parameters)
{
	TsbpdReserveParameters result;
	ParameterReader reader(kName, parameters);
	result.min_reserve = reader.Get("min_reserve", result.min_reserve,
									Range::OpenClosed(0.0, 1.0));
	result.late_threshold = reader.Get("late_threshold", result.late_threshold,
									   Range{0.0, 1.0, true, false});
	result.backoff =
		reader.Get("backoff", result.backoff, Range::OpenClosed(0.0, 1.0));
	result.headroom =
		reader.Get("headroom", result.headroom, Range::Closed(1.0, 10.0));
	result.capacity_window =
		reader.GetInteger("capacity_window", result.capacity_window, 1,
						  MedianFilter::kMaxWindowSize);
	result.smoothing =
		reader.Get("smoothing", result.smoothing, Range::OpenClosed(0.0, 1.0));
	reader.CheckNoUnknownKeys();
	return result;
}

TsbpdReserveCore::TsbpdReserveCore(const TsbpdReserveParameters& parameters)
	: parameters_(parameters),
	  capacity_median_(parameters.capacity_window),
	  filter_(parameters.smoothing)
{
}

void TsbpdReserveCore::Update(const IntervalSample& sample) noexcept
{
	if (sample.side != Side::kReceiver || !sample.buffer_delay ||
		!sample.buffer_target || sample.buffer_target->count() <= 0 ||
		!sample.throughput_bps)
	{
		return;
	}

	// Outliers of the reported capacity are removed by the median.
	if (sample.link_capacity_bps && *sample.link_capacity_bps > 0.0)
	{
		capacity_median_.Update(*sample.link_capacity_bps);
	}

	const double reserve = static_cast<double>(sample.buffer_delay->count()) /
						   static_cast<double>(sample.buffer_target->count());
	const double late_share =
		sample.late_rate.value_or(0.0) + sample.drop_rate.value_or(0.0);
	const bool congested = reserve < parameters_.min_reserve ||
						   late_share > parameters_.late_threshold;

	const double throughput = *sample.throughput_bps;
	double bits_per_second = 0.0;
	if (congested)
	{
		bits_per_second = parameters_.backoff * throughput;
	}
	else if (capacity_median_.HasValue())
	{
		bits_per_second = std::max(capacity_median_.Value(), throughput);
	}
	else
	{
		bits_per_second = parameters_.headroom * throughput;
	}

	filter_.Update(bits_per_second);
	timestamp_ = sample.timestamp;
}

void TsbpdReserveCore::Reset() noexcept
{
	capacity_median_.Reset();
	filter_.Reset();
	timestamp_ = Duration{0};
}

}  // namespace internal
}  // namespace bwe
