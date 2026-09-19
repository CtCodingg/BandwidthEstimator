/// @file
/// @brief Conversion of cumulative measurements into interval samples.

#ifndef BWE_SRC_INTERVAL_TRACKER_HPP_
#define BWE_SRC_INTERVAL_TRACKER_HPP_

#include <optional>

#include "bwe/measurement.hpp"
#include "interval_sample.hpp"

namespace bwe {
namespace internal {

/// @brief Builds interval samples from sender-side measurements.
/// @note Measurements with a timestamp not greater than the previous one
///       are ignored. A decreasing counter starts a new baseline.
class SenderIntervalTracker {
 public:
  /// @brief Processes a measurement.
  /// @return The sample of the elapsed interval, or std::nullopt for the
  ///         first or an ignored measurement.
  std::optional<IntervalSample> Update(
      const SenderMeasurement& measurement) noexcept;

  /// @brief Forgets the previous measurement.
  void Reset() noexcept;

 private:
  std::optional<SenderMeasurement> previous_;
};

/// @brief Builds interval samples from receiver-side measurements.
/// @note Measurements with a timestamp not greater than the previous one
///       are ignored. A decreasing counter starts a new baseline.
class ReceiverIntervalTracker {
 public:
  /// @brief Processes a measurement.
  /// @return The sample of the elapsed interval, or std::nullopt for the
  ///         first or an ignored measurement.
  std::optional<IntervalSample> Update(
      const ReceiverMeasurement& measurement) noexcept;

  /// @brief Forgets the previous measurement.
  void Reset() noexcept;

 private:
  std::optional<ReceiverMeasurement> previous_;
};

}  // namespace internal
}  // namespace bwe

#endif  // BWE_SRC_INTERVAL_TRACKER_HPP_
