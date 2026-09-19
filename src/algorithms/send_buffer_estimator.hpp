/// @file
/// @brief Sender-side estimation from the fill level of the send buffer.
///
/// A send buffer above the threshold or growing faster than the maximum
/// growth rate means the link cannot carry the input rate; the estimate is
/// then backoff times the throughput. Otherwise it is the link capacity
/// reported by the transport (at least the throughput) or, without it,
/// headroom times the throughput.

#ifndef BWE_SRC_ALGORITHMS_SEND_BUFFER_ESTIMATOR_HPP_
#define BWE_SRC_ALGORITHMS_SEND_BUFFER_ESTIMATOR_HPP_

#include <optional>
#include <string_view>

#include "algorithms/side_adapter.hpp"
#include "bwe/bandwidth_estimate.hpp"
#include "bwe/estimator_factory.hpp"
#include "filters/ewma_filter.hpp"
#include "filters/median_filter.hpp"
#include "interval_sample.hpp"

namespace bwe {
namespace internal {

/// @brief Validated parameters of the send buffer algorithm.
struct SendBufferParameters {
  /// @brief Buffer delay in ms that indicates congestion, range (0, 1e5].
  double buffer_threshold_ms = 100.0;
  /// @brief Buffer growth in seconds per second that indicates congestion,
  ///        range (0, 10].
  double max_growth_rate = 0.05;
  /// @brief Factor applied to the throughput on congestion, range (0, 1].
  double backoff = 0.9;
  /// @brief Multiple of the throughput without congestion and without
  ///        link capacity, range [1, 10].
  double headroom = 1.2;
  /// @brief Number of link capacity reports for the median, integer in
  ///        [1, 31].
  int capacity_window = 5;
  /// @brief Smoothing factor of the moving average, range (0, 1].
  double smoothing = 0.3;
};

/// @brief Send buffer calculation; processes sender samples only.
class SendBufferCore {
 public:
  /// @brief Name under which the algorithm is registered.
  static constexpr std::string_view kName = "send_buffer";

  /// @brief Parses and validates parameters.
  /// @throws std::invalid_argument Unknown key or non-integral
  ///         capacity_window.
  /// @throws std::out_of_range Parameter value out of range.
  static SendBufferParameters ParseParameters(const Parameters& parameters);

  /// @param parameters Validated parameters.
  explicit SendBufferCore(const SendBufferParameters& parameters);

  /// @brief Processes one sample; receiver samples and samples without
  ///        buffer delay or throughput are ignored.
  void Update(const IntervalSample& sample) noexcept;

  /// @brief Returns the smoothed estimate.
  BandwidthEstimate GetEstimate() const noexcept {
    return MakeEstimate(filter_, timestamp_);
  }

  /// @brief Discards the estimate.
  void Reset() noexcept;

 private:
  SendBufferParameters parameters_;
  MedianFilter capacity_median_;
  EwmaFilter filter_;
  std::optional<Duration> previous_delay_;
  Duration timestamp_{0};
};

/// @brief Send buffer estimator; available on the sender side only.
using SendBufferSenderEstimator = SenderAdapter<SendBufferCore>;

}  // namespace internal
}  // namespace bwe

#endif  // BWE_SRC_ALGORITHMS_SEND_BUFFER_ESTIMATOR_HPP_
