/// @file
/// @brief Delay-based bandwidth estimation (Vegas-like).
///
/// The queueing delay q = RTT - base RTT is compared with two thresholds:
/// below the low one the estimate is headroom * throughput, above the high
/// one backoff * throughput, in between the throughput.

#ifndef BWE_SRC_ALGORITHMS_DELAY_ESTIMATOR_HPP_
#define BWE_SRC_ALGORITHMS_DELAY_ESTIMATOR_HPP_

#include <optional>
#include <string_view>

#include "algorithms/side_adapter.hpp"
#include "bwe/bandwidth_estimate.hpp"
#include "bwe/estimator_factory.hpp"
#include "filters/ewma_filter.hpp"
#include "interval_sample.hpp"

namespace bwe {
namespace internal {

/// @brief Validated parameters of the delay algorithm.
struct DelayParameters {
  /// @brief Queueing delay in ms below which the estimate rises,
  ///        range [0, 1e4]; must be less than high_threshold_ms.
  double low_threshold_ms = 10.0;
  /// @brief Queueing delay in ms above which the estimate falls,
  ///        range (0, 1e4].
  double high_threshold_ms = 50.0;
  /// @brief Multiple of the throughput below the low threshold,
  ///        range [1, 10].
  double headroom = 1.25;
  /// @brief Multiple of the throughput above the high threshold,
  ///        range (0, 1].
  double backoff = 0.9;
  /// @brief Window length in s; the base RTT is replaced by the minimum
  ///        RTT of the last window when it ends, range (0, 3600].
  double base_rtt_window_s = 30.0;
  /// @brief Smoothing factor of the moving average, range (0, 1].
  double smoothing = 0.3;
};

/// @brief Side-independent delay calculation.
class DelayCore {
 public:
  /// @brief Name under which the algorithm is registered.
  static constexpr std::string_view kName = "delay";

  /// @brief Parses and validates parameters.
  /// @throws std::invalid_argument Unknown key or low_threshold_ms not less
  ///         than high_threshold_ms.
  /// @throws std::out_of_range Parameter value out of range.
  static DelayParameters ParseParameters(const Parameters& parameters);

  /// @param parameters Validated parameters.
  explicit DelayCore(const DelayParameters& parameters);

  /// @brief Processes one sample; samples without RTT or throughput are
  ///        ignored.
  void Update(const IntervalSample& sample) noexcept;

  /// @brief Returns the smoothed estimate.
  BandwidthEstimate GetEstimate() const noexcept {
    return MakeEstimate(filter_, timestamp_);
  }

  /// @brief Discards the estimate and the base RTT.
  void Reset() noexcept;

 private:
  DelayParameters parameters_;
  EwmaFilter filter_;
  std::optional<Duration> base_rtt_;
  Duration window_min_rtt_{0};
  Duration window_start_{0};
  Duration timestamp_{0};
};

/// @brief Delay estimator for sender-side statistics.
using DelaySenderEstimator = SenderAdapter<DelayCore>;

/// @brief Delay estimator for receiver-side statistics.
using DelayReceiverEstimator = ReceiverAdapter<DelayCore>;

}  // namespace internal
}  // namespace bwe

#endif  // BWE_SRC_ALGORITHMS_DELAY_ESTIMATOR_HPP_
