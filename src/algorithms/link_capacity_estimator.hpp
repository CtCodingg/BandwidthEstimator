/// @file
/// @brief Estimation from the link capacity reported by the transport.
///
/// BW = utilization * median(capacity) * (1 - p)

#ifndef BWE_SRC_ALGORITHMS_LINK_CAPACITY_ESTIMATOR_HPP_
#define BWE_SRC_ALGORITHMS_LINK_CAPACITY_ESTIMATOR_HPP_

#include <string_view>

#include "algorithms/side_adapter.hpp"
#include "bwe/bandwidth_estimate.hpp"
#include "bwe/estimator_factory.hpp"
#include "filters/ewma_filter.hpp"
#include "filters/median_filter.hpp"
#include "interval_sample.hpp"

namespace bwe {
namespace internal {

/// @brief Validated parameters of the link capacity algorithm.
struct LinkCapacityParameters {
  /// @brief Number of capacity values for the median, integer in [1, 31].
  int window_size = 5;
  /// @brief Usable share of the capacity, range (0, 1].
  double utilization = 0.9;
  /// @brief Smoothing factor of the moving average, range (0, 1].
  double smoothing = 0.3;
};

/// @brief Side-independent link capacity calculation.
class LinkCapacityCore {
 public:
  /// @brief Name under which the algorithm is registered.
  static constexpr std::string_view kName = "link_capacity";

  /// @brief Parses and validates parameters.
  /// @throws std::invalid_argument Unknown key or non-integral window size.
  /// @throws std::out_of_range Parameter value out of range.
  static LinkCapacityParameters ParseParameters(const Parameters& parameters);

  /// @param parameters Validated parameters.
  explicit LinkCapacityCore(const LinkCapacityParameters& parameters);

  /// @brief Processes one sample; samples without positive link capacity
  ///        are ignored.
  void Update(const IntervalSample& sample) noexcept;

  /// @brief Returns the smoothed estimate.
  BandwidthEstimate GetEstimate() const noexcept {
    return MakeEstimate(filter_, timestamp_);
  }

  /// @brief Discards the estimate.
  void Reset() noexcept;

 private:
  LinkCapacityParameters parameters_;
  MedianFilter median_;
  EwmaFilter filter_;
  Duration timestamp_{0};
};

/// @brief Link capacity estimator for sender-side statistics.
using LinkCapacitySenderEstimator = SenderAdapter<LinkCapacityCore>;

/// @brief Link capacity estimator for receiver-side statistics.
using LinkCapacityReceiverEstimator = ReceiverAdapter<LinkCapacityCore>;

}  // namespace internal
}  // namespace bwe

#endif  // BWE_SRC_ALGORITHMS_LINK_CAPACITY_ESTIMATOR_HPP_
