/// @file
/// @brief Adapters that turn a side-independent core into an estimator.
///
/// A core must provide:
/// - `static constexpr std::string_view kName`
/// - `static CoreParameters ParseParameters(const Parameters&)`, throwing
///   std::invalid_argument or std::out_of_range on invalid parameters
/// - a constructor taking the parsed parameters
/// - `void Update(const IntervalSample&) noexcept`
/// - `BandwidthEstimate GetEstimate() const noexcept`
/// - `void Reset() noexcept`

#ifndef BWE_SRC_ALGORITHMS_SIDE_ADAPTER_HPP_
#define BWE_SRC_ALGORITHMS_SIDE_ADAPTER_HPP_

#include <memory>
#include <string_view>
#include <utility>

#include "bwe/bandwidth_estimator.hpp"
#include "bwe/estimator_factory.hpp"
#include "interval_tracker.hpp"

namespace bwe {
namespace internal {

/// @brief Sender-side estimator built from a core.
template <typename Core>
class SenderAdapter final : public SenderEstimator {
 public:
  /// @brief Forwards all arguments to the constructor of the core.
  template <typename... Args>
  explicit SenderAdapter(Args&&... args)
      : core_(std::forward<Args>(args)...) {}

  std::string_view Name() const noexcept override { return Core::kName; }

 private:
  void DoUpdate(const SenderMeasurement& measurement) noexcept override {
    if (const auto sample = tracker_.Update(measurement)) {
      core_.Update(*sample);
    }
  }

  BandwidthEstimate DoGetEstimate() const noexcept override {
    return core_.GetEstimate();
  }

  void DoReset() noexcept override {
    tracker_.Reset();
    core_.Reset();
  }

  SenderIntervalTracker tracker_;
  Core core_;
};

/// @brief Receiver-side estimator built from a core.
template <typename Core>
class ReceiverAdapter final : public ReceiverEstimator {
 public:
  /// @brief Forwards all arguments to the constructor of the core.
  template <typename... Args>
  explicit ReceiverAdapter(Args&&... args)
      : core_(std::forward<Args>(args)...) {}

  std::string_view Name() const noexcept override { return Core::kName; }

 private:
  void DoUpdate(const ReceiverMeasurement& measurement) noexcept override {
    if (const auto sample = tracker_.Update(measurement)) {
      core_.Update(*sample);
    }
  }

  BandwidthEstimate DoGetEstimate() const noexcept override {
    return core_.GetEstimate();
  }

  void DoReset() noexcept override {
    tracker_.Reset();
    core_.Reset();
  }

  ReceiverIntervalTracker tracker_;
  Core core_;
};

/// @brief Factory creator for a sender-side core.
/// @throws std::invalid_argument Unknown parameter key.
/// @throws std::out_of_range Parameter value out of range.
template <typename Core>
std::unique_ptr<SenderEstimator> CreateSenderAdapter(
    const Parameters& parameters) {
  return std::make_unique<SenderAdapter<Core>>(
      Core::ParseParameters(parameters));
}

/// @brief Factory creator for a receiver-side core.
/// @throws std::invalid_argument Unknown parameter key.
/// @throws std::out_of_range Parameter value out of range.
template <typename Core>
std::unique_ptr<ReceiverEstimator> CreateReceiverAdapter(
    const Parameters& parameters) {
  return std::make_unique<ReceiverAdapter<Core>>(
      Core::ParseParameters(parameters));
}

}  // namespace internal
}  // namespace bwe

#endif  // BWE_SRC_ALGORITHMS_SIDE_ADAPTER_HPP_
