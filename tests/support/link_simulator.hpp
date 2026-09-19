/// @file
/// @brief Simple deterministic link model producing transport statistics.
///
/// A constant-rate live source feeds a bottleneck queue whose capacity
/// follows a profile. Overflowing bytes are lost, queued bytes add to the
/// RTT, and bytes delayed beyond the receiver latency arrive too late.
/// Lost packets are not retransmitted. The send buffer delay equals the RTT
/// (data waiting for acknowledgement). The reported link capacity carries
/// deterministic noise and periodic outliers.

#ifndef BWE_TESTS_SUPPORT_LINK_SIMULATOR_HPP_
#define BWE_TESTS_SUPPORT_LINK_SIMULATOR_HPP_

#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

#include "bwe/measurement.hpp"

namespace bwe {
namespace test_support {

/// @brief Capacity valid from `start` until the next phase.
struct CapacityPhase {
  Duration start{0};
  double capacity_bps = 0.0;
};

/// @brief Configuration of the link model.
struct LinkSimulatorConfig {
  /// @brief Constant input rate of the source in bit/s.
  double input_bps = 6e6;
  /// @brief Capacity profile, sorted by start time; must not be empty.
  std::vector<CapacityPhase> capacity_profile;
  /// @brief RTT without queueing.
  Duration base_rtt = std::chrono::milliseconds(40);
  /// @brief Receiver latency for timestamp-based delivery.
  Duration latency = std::chrono::milliseconds(120);
  /// @brief Size of the bottleneck queue in bytes.
  double queue_limit_bytes = 250'000.0;
  /// @brief Payload per packet in bytes.
  double payload_bytes = 1316.0;
  /// @brief Maximum segment size reported by the transport.
  std::uint32_t mss_bytes = 1500;
  /// @brief Interval between two measurements.
  Duration sample_interval = std::chrono::milliseconds(500);
  /// @brief Resolution of the queue model.
  Duration step = std::chrono::milliseconds(10);
  /// @brief Relative amplitude of the capacity noise, e.g. 0.15 for +-15 %.
  double capacity_noise = 0.15;
  /// @brief Every n-th capacity report is an outlier; 0 disables outliers.
  int outlier_period = 7;
  /// @brief Factor applied to outlier capacity reports.
  double outlier_factor = 2.5;
  /// @brief Seed of the noise generator.
  std::uint32_t seed = 42;
};

/// @brief Deterministic link model.
class LinkSimulator {
 public:
  /// @param config Model configuration.
  explicit LinkSimulator(LinkSimulatorConfig config);

  /// @brief Advances the model by one sample interval.
  void Step();

  /// @brief Returns the current simulation time.
  Duration Now() const { return now_; }

  /// @brief Returns the true capacity at the current time in bit/s.
  double CapacityBps() const;

  /// @brief Returns the sender statistics at the current time.
  SenderMeasurement Sender() const;

  /// @brief Returns the receiver statistics at the current time.
  ReceiverMeasurement Receiver() const;

 private:
  double CapacityAt(Duration time) const;
  Duration QueueDelay() const;
  CommonStats Common() const;

  LinkSimulatorConfig config_;
  std::mt19937 random_;
  Duration now_{0};
  int samples_ = 0;
  double reported_capacity_bps_ = 0.0;
  double queue_bytes_ = 0.0;
  double bytes_sent_ = 0.0;
  double bytes_delivered_ = 0.0;
  double bytes_lost_ = 0.0;
  double bytes_belated_ = 0.0;
};

}  // namespace test_support
}  // namespace bwe

#endif  // BWE_TESTS_SUPPORT_LINK_SIMULATOR_HPP_
