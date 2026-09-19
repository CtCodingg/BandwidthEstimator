#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "bwe/shared_link_allocator.hpp"
#include "support/shared_channel_simulator.hpp"

namespace bwe {
namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;
using test_support::SenderRadio;
using test_support::SharedChannelConfig;
using test_support::SharedChannelSimulator;

// Receiver-side hybrid tuned for radio links.
const Parameters kRadioParameters = {{"aimd.loss_threshold", 0.05},
                                     {"delay.low_threshold_ms", 30.0},
                                     {"delay.high_threshold_ms", 100.0}};

// Additionally follows capacity fading: a lower utilization and no time
// median make the link capacity bound react within one sample.
const Parameters kFadingParameters = {{"aimd.loss_threshold", 0.05},
                                      {"delay.low_threshold_ms", 30.0},
                                      {"delay.high_threshold_ms", 100.0},
                                      {"link_capacity.utilization", 0.8},
                                      {"link_capacity.window_size", 1.0},
                                      {"link_capacity.smoothing", 1.0}};

constexpr double kSampleSeconds = 0.5;

struct Sample {
  double time_s = 0.0;
  double capacity_bps = 0.0;
  std::vector<double> rate_bps;
  std::vector<ReceiverMeasurement> receiver;
  std::vector<double> airtime_s;
};

double Sum(const std::vector<double>& values) {
  double sum = 0.0;
  for (double value : values) {
    sum += value;
  }
  return sum;
}

// Receiver statistics -> allocator -> delayed feedback -> senders.
std::vector<Sample> RunClosedLoop(const SharedChannelConfig& config,
                                  const Parameters& parameters,
                                  const std::vector<double>& weights,
                                  double duration_s) {
  SharedChannelSimulator simulator(config);
  SharedLinkAllocator allocator("hybrid", parameters);
  const int senders = simulator.SenderCount();
  for (int i = 0; i < senders; ++i) {
    FlowConfig flow;
    flow.weight = weights.empty() ? 1.0 : weights[i];
    flow.max_bps = config.max_bps[i];
    allocator.AddFlow(i, flow);
  }

  std::vector<Sample> samples;
  const int count = static_cast<int>(duration_s / kSampleSeconds);
  for (int k = 0; k < count; ++k) {
    simulator.Step();
    Sample sample;
    sample.time_s = (k + 1) * kSampleSeconds;
    sample.capacity_bps = simulator.CapacityBps();
    for (int i = 0; i < senders; ++i) {
      const ReceiverMeasurement stats = simulator.Receiver(i);
      allocator.Update(i, stats);
      sample.receiver.push_back(stats);
      sample.rate_bps.push_back(simulator.RateBps(i));
      sample.airtime_s.push_back(simulator.AirtimeSeconds(i));
    }
    allocator.Tick(simulator.Now());
    for (const FlowAllocation& allocation : allocator.Allocate()) {
      simulator.SendFeedback(static_cast<int>(allocation.flow),
                             allocation.target_bps);
    }
    samples.push_back(sample);
  }
  return samples;
}

// Samples with from_s <= time <= to_s.
std::vector<const Sample*> Window(const std::vector<Sample>& samples,
                                  double from_s, double to_s) {
  std::vector<const Sample*> window;
  for (const Sample& sample : samples) {
    if (sample.time_s >= from_s && sample.time_s <= to_s) {
      window.push_back(&sample);
    }
  }
  return window;
}

// Share of dropped and of late packets of all flows within the window.
struct LossShares {
  double dropped = 0.0;
  double late = 0.0;
};

LossShares SharesOf(const std::vector<const Sample*>& window) {
  const Sample& first = *window.front();
  const Sample& last = *window.back();
  double received = 0.0;
  double dropped = 0.0;
  double late = 0.0;
  for (std::size_t i = 0; i < first.receiver.size(); ++i) {
    received += static_cast<double>(*last.receiver[i].packets_received -
                                    *first.receiver[i].packets_received);
    dropped += static_cast<double>(*last.receiver[i].packets_dropped -
                                   *first.receiver[i].packets_dropped);
    late += static_cast<double>(*last.receiver[i].packets_belated -
                                *first.receiver[i].packets_belated);
  }
  return {dropped / (received + dropped), late / received};
}

SharedChannelConfig ChannelOf(double capacity_bps) {
  SharedChannelConfig config;
  config.capacity_profile = {{seconds(0), capacity_bps}};
  return config;
}

TEST(SharedChannelRadioTest, StaysStableWithJitter) {
  SharedChannelConfig config = ChannelOf(12e6);
  config.jitter_max = milliseconds(20);
  config.spike_probability = 0.05;
  config.spike_delay = milliseconds(80);
  const auto samples = RunClosedLoop(config, kRadioParameters, {}, 30.0);
  const auto window = Window(samples, 20.0, 30.0);

  double sum = 0.0;
  double squares = 0.0;
  for (const Sample* sample : window) {
    const double total = Sum(sample->rate_bps);
    EXPECT_GE(total, 0.7 * sample->capacity_bps) << "t=" << sample->time_s;
    sum += total;
    squares += total * total;
  }
  const double mean = sum / static_cast<double>(window.size());
  const double deviation = std::sqrt(
      std::max(0.0, squares / static_cast<double>(window.size()) -
                        mean * mean));
  EXPECT_LE(deviation, 0.1 * mean);
  EXPECT_LE(SharesOf(window).late, 0.01);
}

TEST(SharedChannelRadioTest, HandlesMixedRadioQuality) {
  SharedChannelConfig config = ChannelOf(12e6);
  config.radio = {SenderRadio{1.0, 0.0}, SenderRadio{1.0, 0.0},
                  SenderRadio{0.5, 0.05}};
  const auto samples = RunClosedLoop(config, kRadioParameters, {}, 30.0);
  const auto window = Window(samples, 20.0, 30.0);

  const LossShares shares = SharesOf(window);
  EXPECT_LE(shares.dropped, 0.01);
  EXPECT_LE(shares.late, 0.01);

  // Every sender gets its rate through.
  const Sample& first = *window.front();
  const Sample& last = *window.back();
  const double seconds_in_window = last.time_s - first.time_s;
  for (std::size_t i = 0; i < first.rate_bps.size(); ++i) {
    const double delivered_bps =
        static_cast<double>(*last.receiver[i].bytes_received -
                            *first.receiver[i].bytes_received) *
        8.0 / seconds_in_window;
    double sending_bps = 0.0;
    for (const Sample* sample : window) {
      sending_bps += sample->rate_bps[i];
    }
    sending_bps /= static_cast<double>(window.size());
    EXPECT_GE(delivered_bps, 0.8 * sending_bps) << "sender " << i;
  }
}

TEST(SharedChannelRadioTest, WeightsByEfficiencyEqualizeAirtime) {
  SharedChannelConfig config = ChannelOf(12e6);
  config.radio = {SenderRadio{1.0, 0.0}, SenderRadio{1.0, 0.0},
                  SenderRadio{0.5, 0.05}};
  const auto samples =
      RunClosedLoop(config, kRadioParameters, {1.0, 1.0, 0.5}, 30.0);
  const auto window = Window(samples, 20.0, 30.0);

  const Sample& first = *window.front();
  const Sample& last = *window.back();
  std::vector<double> airtime;
  for (std::size_t i = 0; i < first.airtime_s.size(); ++i) {
    airtime.push_back(last.airtime_s[i] - first.airtime_s[i]);
  }
  const double mean = Sum(airtime) / static_cast<double>(airtime.size());
  for (std::size_t i = 0; i < airtime.size(); ++i) {
    EXPECT_NEAR(airtime[i], mean, 0.1 * mean) << "sender " << i;
  }
}

TEST(SharedChannelRadioTest, FollowsCapacityFading) {
  // Capacity 12 Mbit/s +-30 % with a period of 10 s.
  SharedChannelConfig config = ChannelOf(12e6);
  config.fading_amplitude = 0.3;
  const auto samples = RunClosedLoop(config, kFadingParameters, {}, 60.0);
  const auto window = Window(samples, 10.0, 60.0);

  int below_capacity = 0;
  for (const Sample* sample : window) {
    if (Sum(sample->rate_bps) <= sample->capacity_bps) {
      ++below_capacity;
    }
  }
  EXPECT_GE(below_capacity, 0.9 * static_cast<double>(window.size()));
  const LossShares shares = SharesOf(window);
  EXPECT_LE(shares.dropped, 0.02);
  EXPECT_LE(shares.late, 0.01);
}

}  // namespace
}  // namespace bwe
