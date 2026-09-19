#include "algorithms/aimd_estimator.hpp"

#include <algorithm>
#include <chrono>

#include "filters/ewma_filter.hpp"
#include "parameter_reader.hpp"

namespace bwe {
namespace internal {

AimdParameters AimdCore::ParseParameters(const Parameters& parameters) {
  AimdParameters result;
  ParameterReader reader(kName, parameters);
  result.loss_threshold = reader.Get("loss_threshold", result.loss_threshold,
                                     Range::Open(0.0, 1.0));
  result.decrease_factor = reader.Get(
      "decrease_factor", result.decrease_factor, Range::Open(0.0, 1.0));
  result.increase_bps = reader.Get("increase_bps", result.increase_bps,
                                   Range::OpenClosed(0.0, 1e10));
  result.headroom =
      reader.Get("headroom", result.headroom, Range::Closed(1.0, 10.0));
  result.min_bps =
      reader.Get("min_bps", result.min_bps, Range::Closed(0.0, 1e10));
  reader.CheckNoUnknownKeys();
  return result;
}

AimdCore::AimdCore(const AimdParameters& parameters)
    : parameters_(parameters) {}

void AimdCore::Update(const IntervalSample& sample) noexcept {
  if (!sample.loss_rate) {
    return;
  }

  if (samples_used_ == 0) {
    if (sample.throughput_bps && *sample.throughput_bps > 0.0) {
      rate_bps_ = *sample.throughput_bps;
    } else if (sample.link_capacity_bps) {
      rate_bps_ = *sample.link_capacity_bps;
    } else {
      rate_bps_ = parameters_.min_bps;
    }
  }

  if (*sample.loss_rate > parameters_.loss_threshold) {
    if (sample.throughput_bps) {
      // Relative to the delivered rate; repeated loss does not compound.
      rate_bps_ = std::min(
          rate_bps_, parameters_.decrease_factor * *sample.throughput_bps);
    } else {
      rate_bps_ *= parameters_.decrease_factor;
    }
  } else {
    const double seconds =
        std::chrono::duration<double>(sample.duration).count();
    rate_bps_ += parameters_.increase_bps * seconds;
    if (sample.throughput_bps) {
      rate_bps_ =
          std::min(rate_bps_, parameters_.headroom * *sample.throughput_bps);
    }
  }

  if (sample.max_bandwidth_bps) {
    rate_bps_ = std::min(rate_bps_, *sample.max_bandwidth_bps);
  }
  rate_bps_ = std::max(rate_bps_, parameters_.min_bps);

  samples_used_ =
      std::min(samples_used_ + 1, EwmaFilter::kDefaultFullConfidenceSamples);
  timestamp_ = sample.timestamp;
}

BandwidthEstimate AimdCore::GetEstimate() const noexcept {
  BandwidthEstimate estimate;
  estimate.bits_per_second = rate_bps_;
  estimate.confidence = static_cast<double>(samples_used_) /
                        EwmaFilter::kDefaultFullConfidenceSamples;
  estimate.timestamp = timestamp_;
  estimate.valid = samples_used_ > 0;
  return estimate;
}

void AimdCore::Reset() noexcept {
  rate_bps_ = 0.0;
  samples_used_ = 0;
  timestamp_ = Duration{0};
}

}  // namespace internal
}  // namespace bwe
