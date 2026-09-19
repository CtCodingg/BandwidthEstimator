#include "algorithms/send_buffer_estimator.hpp"

#include <algorithm>
#include <chrono>

#include "parameter_reader.hpp"

namespace bwe {
namespace internal {

SendBufferParameters SendBufferCore::ParseParameters(
    const Parameters& parameters) {
  SendBufferParameters result;
  ParameterReader reader(kName, parameters);
  result.buffer_threshold_ms =
      reader.Get("buffer_threshold_ms", result.buffer_threshold_ms,
                 Range::OpenClosed(0.0, 1e5));
  result.max_growth_rate = reader.Get(
      "max_growth_rate", result.max_growth_rate, Range::OpenClosed(0.0, 10.0));
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

SendBufferCore::SendBufferCore(const SendBufferParameters& parameters)
    : parameters_(parameters),
      capacity_median_(parameters.capacity_window),
      filter_(parameters.smoothing) {}

void SendBufferCore::Update(const IntervalSample& sample) noexcept {
  if (sample.side != Side::kSender || !sample.buffer_delay ||
      !sample.throughput_bps) {
    return;
  }

  // Outliers of the reported capacity are removed by the median.
  if (sample.link_capacity_bps && *sample.link_capacity_bps > 0.0) {
    capacity_median_.Update(*sample.link_capacity_bps);
  }

  using Seconds = std::chrono::duration<double>;
  const double delay_seconds = Seconds(*sample.buffer_delay).count();
  double growth_rate = 0.0;
  if (previous_delay_) {
    growth_rate = (delay_seconds - Seconds(*previous_delay_).count()) /
                  Seconds(sample.duration).count();
  }
  previous_delay_ = sample.buffer_delay;

  const double throughput = *sample.throughput_bps;
  const bool congested =
      delay_seconds * 1000.0 > parameters_.buffer_threshold_ms ||
      growth_rate > parameters_.max_growth_rate;

  double bits_per_second = 0.0;
  if (congested) {
    bits_per_second = parameters_.backoff * throughput;
  } else if (capacity_median_.HasValue()) {
    bits_per_second = std::max(capacity_median_.Value(), throughput);
  } else {
    bits_per_second = parameters_.headroom * throughput;
  }
  if (sample.max_bandwidth_bps) {
    bits_per_second = std::min(bits_per_second, *sample.max_bandwidth_bps);
  }

  filter_.Update(bits_per_second);
  timestamp_ = sample.timestamp;
}

void SendBufferCore::Reset() noexcept {
  capacity_median_.Reset();
  filter_.Reset();
  previous_delay_.reset();
  timestamp_ = Duration{0};
}

}  // namespace internal
}  // namespace bwe
