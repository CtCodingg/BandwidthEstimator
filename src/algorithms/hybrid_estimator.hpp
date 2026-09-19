/// @file
/// @brief Combination of several algorithms.
///
/// Feeds every sample to aimd, delay, link_capacity, send_buffer (sender)
/// and tsbpd_reserve (receiver) and returns the lowest valid estimate.
/// Parameters are passed with the component name as prefix, e.g.
/// "aimd.headroom".

#ifndef BWE_SRC_ALGORITHMS_HYBRID_ESTIMATOR_HPP_
#define BWE_SRC_ALGORITHMS_HYBRID_ESTIMATOR_HPP_

#include <string_view>

#include "algorithms/aimd_estimator.hpp"
#include "algorithms/delay_estimator.hpp"
#include "algorithms/link_capacity_estimator.hpp"
#include "algorithms/send_buffer_estimator.hpp"
#include "algorithms/side_adapter.hpp"
#include "algorithms/tsbpd_reserve_estimator.hpp"
#include "bwe/bandwidth_estimate.hpp"
#include "bwe/estimator_factory.hpp"
#include "interval_sample.hpp"

namespace bwe {
namespace internal {

/// @brief Validated parameters of all components.
struct HybridParameters {
  AimdParameters aimd;
  DelayParameters delay;
  LinkCapacityParameters link_capacity;
  SendBufferParameters send_buffer;
  TsbpdReserveParameters tsbpd_reserve;
};

/// @brief Side-independent combination of several cores.
class HybridCore {
 public:
  /// @brief Name under which the algorithm is registered.
  static constexpr std::string_view kName = "hybrid";

  /// @brief Parses and validates parameters of all components.
  /// @throws std::invalid_argument Key without known component prefix, or
  ///         invalid component parameters.
  /// @throws std::out_of_range Component parameter value out of range.
  static HybridParameters ParseParameters(const Parameters& parameters);

  /// @param parameters Validated parameters.
  explicit HybridCore(const HybridParameters& parameters);

  /// @brief Passes the sample to all components.
  void Update(const IntervalSample& sample) noexcept;

  /// @brief Returns the lowest valid component estimate.
  BandwidthEstimate GetEstimate() const noexcept;

  /// @brief Discards the estimates of all components.
  void Reset() noexcept;

 private:
  AimdCore aimd_;
  DelayCore delay_;
  LinkCapacityCore link_capacity_;
  SendBufferCore send_buffer_;
  TsbpdReserveCore tsbpd_reserve_;
};

/// @brief Hybrid estimator for sender-side statistics.
using HybridSenderEstimator = SenderAdapter<HybridCore>;

/// @brief Hybrid estimator for receiver-side statistics.
using HybridReceiverEstimator = ReceiverAdapter<HybridCore>;

}  // namespace internal
}  // namespace bwe

#endif  // BWE_SRC_ALGORITHMS_HYBRID_ESTIMATOR_HPP_
