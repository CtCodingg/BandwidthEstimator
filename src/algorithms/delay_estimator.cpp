#include "algorithms/delay_estimator.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "parameter_reader.hpp"

namespace bwe {
namespace internal {

DelayParameters DelayCore::ParseParameters(const Parameters& parameters) {
  DelayParameters result;
  ParameterReader reader(kName, parameters);
  result.low_threshold_ms = reader.Get(
      "low_threshold_ms", result.low_threshold_ms, Range::Closed(0.0, 1e4));
  result.high_threshold_ms =
      reader.Get("high_threshold_ms", result.high_threshold_ms,
                 Range::OpenClosed(0.0, 1e4));
  result.headroom =
      reader.Get("headroom", result.headroom, Range::Closed(1.0, 10.0));
  result.backoff =
      reader.Get("backoff", result.backoff, Range::OpenClosed(0.0, 1.0));
  result.base_rtt_window_s =
      reader.Get("base_rtt_window_s", result.base_rtt_window_s,
                 Range::OpenClosed(0.0, 3600.0));
  result.smoothing =
      reader.Get("smoothing", result.smoothing, Range::OpenClosed(0.0, 1.0));
  reader.CheckNoUnknownKeys();
  if (result.low_threshold_ms >= result.high_threshold_ms) {
    throw std::invalid_argument(
        "bwe: delay parameter 'low_threshold_ms' must be less than "
        "'high_threshold_ms'");
  }
  return result;
}

DelayCore::DelayCore(const DelayParameters& parameters)
    : parameters_(parameters), filter_(parameters.smoothing) {}

void DelayCore::Update(const IntervalSample& sample) noexcept {
  if (!sample.rtt || !sample.throughput_bps || sample.rtt->count() <= 0) {
    return;
  }

  using Seconds = std::chrono::duration<double>;
  const Duration rtt = *sample.rtt;
  if (!base_rtt_) {
    base_rtt_ = rtt;
    window_min_rtt_ = rtt;
    window_start_ = sample.timestamp;
  } else {
    base_rtt_ = std::min(*base_rtt_, rtt);
    window_min_rtt_ = std::min(window_min_rtt_, rtt);
    if (Seconds(sample.timestamp - window_start_).count() >
        parameters_.base_rtt_window_s) {
      // A standing queue must not become the new base: use the minimum
      // of the whole window, not the current RTT.
      base_rtt_ = window_min_rtt_;
      window_min_rtt_ = rtt;
      window_start_ = sample.timestamp;
    }
  }

  const double queue_ms =
      std::chrono::duration<double, std::milli>(rtt - *base_rtt_).count();
  const double throughput = *sample.throughput_bps;

  double bits_per_second = throughput;
  if (queue_ms <= parameters_.low_threshold_ms) {
    bits_per_second = parameters_.headroom * throughput;
  } else if (queue_ms >= parameters_.high_threshold_ms) {
    bits_per_second = parameters_.backoff * throughput;
  }
  if (sample.max_bandwidth_bps) {
    bits_per_second = std::min(bits_per_second, *sample.max_bandwidth_bps);
  }

  filter_.Update(bits_per_second);
  timestamp_ = sample.timestamp;
}

void DelayCore::Reset() noexcept {
  filter_.Reset();
  base_rtt_.reset();
  window_min_rtt_ = Duration{0};
  window_start_ = Duration{0};
  timestamp_ = Duration{0};
}

}  // namespace internal
}  // namespace bwe
