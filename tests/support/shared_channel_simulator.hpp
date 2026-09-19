/// @file
/// @brief Deterministic model of several senders on one shared radio
///        channel, for closed-loop tests.
///
/// All active senders feed one bottleneck queue that is served in
/// proportion to each sender's share of the queue. The channel provides
/// airtime: a sender with efficiency e needs 1/e of the airtime per byte.
/// A fraction of the served bytes is lost on the radio link and queued again
/// (retransmission). Bytes overflowing the queue are dropped. Bytes delayed
/// beyond the receiver latency (queueing plus jitter) arrive too late. Rate
/// feedback reaches a sender after the feedback delay; the sender then sends
/// at min(target, its maximum). Jitter, per-sender radio quality and fading
/// are disabled by default.

#ifndef BWE_TESTS_SUPPORT_SHARED_CHANNEL_SIMULATOR_HPP_
#define BWE_TESTS_SUPPORT_SHARED_CHANNEL_SIMULATOR_HPP_

#include <chrono>
#include <cstdint>
#include <deque>
#include <random>
#include <utility>
#include <vector>

#include "bwe/measurement.hpp"
#include "support/link_simulator.hpp"

namespace bwe {
namespace test_support {

/// @brief Radio quality of one sender.
struct SenderRadio {
  /// @brief Share of the channel rate the sender achieves, (0, 1].
  double efficiency = 1.0;
  /// @brief Share of served bytes lost on the radio link, [0, 1).
  double loss = 0.0;
};

/// @brief Configuration of the shared channel model.
struct SharedChannelConfig {
  /// @brief Channel capacity profile, sorted by start time; not empty.
  std::vector<CapacityPhase> capacity_profile;
  /// @brief Maximum rate of each sender in bit/s; defines the sender count.
  std::vector<double> max_bps = {6e6, 6e6, 6e6};
  /// @brief Rate of each sender before the first feedback.
  double initial_rate_bps = 1e6;
  /// @brief Share of served bytes lost on the radio link, [0, 1); used
  ///        for all senders unless `radio` is set.
  double radio_loss = 0.0;
  /// @brief Radio quality per sender; empty means efficiency 1 and
  ///        `radio_loss` for everyone.
  std::vector<SenderRadio> radio;
  /// @brief Maximum of the uniformly distributed extra delay per sample.
  Duration jitter_max{0};
  /// @brief Probability of an extra delay spike per sender and sample.
  double spike_probability = 0.0;
  /// @brief Additional delay of a spike.
  Duration spike_delay{0};
  /// @brief Relative amplitude of the sinusoidal capacity fading; 0 = off.
  double fading_amplitude = 0.0;
  /// @brief Period of the capacity fading.
  Duration fading_period = std::chrono::seconds(10);
  /// @brief RTT without queueing.
  Duration base_rtt = std::chrono::milliseconds(20);
  /// @brief Receiver latency for timestamp-based delivery.
  Duration latency = std::chrono::milliseconds(120);
  /// @brief Size of the shared queue in bytes.
  double queue_limit_bytes = 150'000.0;
  /// @brief Payload per packet in bytes.
  double payload_bytes = 1316.0;
  /// @brief Maximum segment size reported by the transport.
  std::uint32_t mss_bytes = 1500;
  /// @brief Resolution of the model.
  Duration step = std::chrono::milliseconds(10);
  /// @brief Interval between two statistics samples.
  Duration sample_interval = std::chrono::milliseconds(500);
  /// @brief Delay until a rate feedback reaches the sender.
  Duration feedback_delay = std::chrono::milliseconds(200);
  /// @brief Relative amplitude of the capacity report noise.
  double capacity_noise = 0.15;
  /// @brief Every n-th report of a sender is an outlier; 0 disables them.
  int outlier_period = 7;
  /// @brief Factor applied to outlier reports.
  double outlier_factor = 2.5;
  /// @brief Seed of the noise generator.
  std::uint32_t seed = 7;
};

/// @brief Deterministic shared channel model.
class SharedChannelSimulator {
 public:
  /// @param config Model configuration.
  explicit SharedChannelSimulator(SharedChannelConfig config);

  /// @brief Advances the model by one sample interval.
  void Step();

  /// @brief Returns the current simulation time.
  Duration Now() const { return now_; }

  /// @brief Returns the true channel capacity at the current time.
  double CapacityBps() const { return CapacityAt(now_); }

  /// @brief Returns the number of senders.
  int SenderCount() const { return static_cast<int>(senders_.size()); }

  /// @brief Starts or stops a sender.
  void SetActive(int sender, bool active);

  /// @brief Sends a rate target to a sender; applied after the feedback
  ///        delay.
  void SendFeedback(int sender, double target_bps);

  /// @brief Returns the current sending rate of a sender; 0 if inactive.
  double RateBps(int sender) const;

  /// @brief Returns the receiver statistics of a sender's flow.
  ReceiverMeasurement Receiver(int sender) const;

  /// @brief Returns the airtime used by a sender so far, in seconds.
  double AirtimeSeconds(int sender) const;

 private:
  struct Sender {
    bool active = true;
    double max_bps = 0.0;
    double rate_bps = 0.0;
    double queue_bytes = 0.0;
    double delivered_bytes = 0.0;
    double lost_bytes = 0.0;
    double dropped_bytes = 0.0;
    double belated_bytes = 0.0;
    double reported_capacity_bps = 0.0;
    double airtime_seconds = 0.0;
    SenderRadio radio;
    Duration jitter{0};
    int reports = 0;
    std::deque<std::pair<Duration, double>> feedback;
  };

  double CapacityAt(Duration time) const;
  double QueueBytes() const;
  double EffectiveCapacity() const;
  Duration QueueDelay() const;
  double Uniform();

  SharedChannelConfig config_;
  std::mt19937 random_;
  Duration now_{0};
  std::vector<Sender> senders_;
};

}  // namespace test_support
}  // namespace bwe

#endif  // BWE_TESTS_SUPPORT_SHARED_CHANNEL_SIMULATOR_HPP_
