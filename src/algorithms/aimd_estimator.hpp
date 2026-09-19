/// @file
/// @brief Loss-based bandwidth estimation with additive increase and
///        multiplicative decrease (AIMD).
///
/// Above the loss threshold the estimate drops to at most decrease_factor
/// times the delivered throughput. Otherwise it rises by increase_bps per
/// second, limited to headroom times the throughput.

#ifndef BWE_SRC_ALGORITHMS_AIMD_ESTIMATOR_HPP_
#define BWE_SRC_ALGORITHMS_AIMD_ESTIMATOR_HPP_

#include <string_view>

#include "algorithms/side_adapter.hpp"
#include "bwe/bandwidth_estimate.hpp"
#include "bwe/estimator_factory.hpp"
#include "interval_sample.hpp"

namespace bwe {
namespace internal {

/// @brief Validated parameters of the AIMD algorithm.
struct AimdParameters {
  /// @brief Loss rate above which the estimate decreases, range (0, 1).
  double loss_threshold = 0.02;
  /// @brief Factor applied on decrease, range (0, 1).
  double decrease_factor = 0.85;
  /// @brief Increase per second in bit/s, range (0, 1e10].
  double increase_bps = 250e3;
  /// @brief Upper limit as multiple of the throughput, range [1, 10].
  double headroom = 1.5;
  /// @brief Lower limit of the estimate in bit/s, range [0, 1e10].
  double min_bps = 100e3;
};

/// @brief Side-independent AIMD calculation.
class AimdCore {
 public:
  /// @brief Name under which the algorithm is registered.
  static constexpr std::string_view kName = "aimd";

  /// @brief Parses and validates parameters.
  /// @throws std::invalid_argument Unknown parameter key.
  /// @throws std::out_of_range Parameter value out of range.
  static AimdParameters ParseParameters(const Parameters& parameters);

  /// @param parameters Validated parameters.
  explicit AimdCore(const AimdParameters& parameters);

  /// @brief Processes one sample; samples without loss rate are ignored.
  void Update(const IntervalSample& sample) noexcept;

  /// @brief Returns the current estimate.
  BandwidthEstimate GetEstimate() const noexcept;

  /// @brief Discards the estimate.
  void Reset() noexcept;

 private:
  AimdParameters parameters_;
  double rate_bps_ = 0.0;
  int samples_used_ = 0;
  Duration timestamp_{0};
};

/// @brief AIMD estimator for sender-side statistics.
using AimdSenderEstimator = SenderAdapter<AimdCore>;

/// @brief AIMD estimator for receiver-side statistics.
using AimdReceiverEstimator = ReceiverAdapter<AimdCore>;

}  // namespace internal
}  // namespace bwe

#endif  // BWE_SRC_ALGORITHMS_AIMD_ESTIMATOR_HPP_
