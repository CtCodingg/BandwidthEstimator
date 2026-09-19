#include "support/shared_channel_simulator.hpp"

#include <algorithm>
#include <cmath>

namespace bwe {
namespace test_support {
namespace {

constexpr double kBitsPerByte = 8.0;
constexpr double kPi = 3.14159265358979323846;

std::uint64_t Packets(double bytes, double payload_bytes) {
  return static_cast<std::uint64_t>(bytes / payload_bytes);
}

}  // namespace

SharedChannelSimulator::SharedChannelSimulator(SharedChannelConfig config)
    : config_(std::move(config)), random_(config_.seed) {
  for (std::size_t i = 0; i < config_.max_bps.size(); ++i) {
    Sender sender;
    sender.max_bps = config_.max_bps[i];
    sender.radio.loss = config_.radio_loss;
    if (i < config_.radio.size()) {
      sender.radio = config_.radio[i];
    }
    const double max_bps = sender.max_bps;
    sender.rate_bps = std::min(config_.initial_rate_bps, max_bps);
    sender.reported_capacity_bps = CapacityAt(now_);
    senders_.push_back(sender);
  }
}

void SharedChannelSimulator::SetActive(int sender, bool active) {
  senders_[sender].active = active;
  if (active) {
    senders_[sender].rate_bps =
        std::min(config_.initial_rate_bps, senders_[sender].max_bps);
  }
}

void SharedChannelSimulator::SendFeedback(int sender, double target_bps) {
  senders_[sender].feedback.emplace_back(now_ + config_.feedback_delay,
                                         target_bps);
}

double SharedChannelSimulator::RateBps(int sender) const {
  return senders_[sender].active ? senders_[sender].rate_bps : 0.0;
}

double SharedChannelSimulator::Uniform() {
  // Deterministic on every platform: uses the raw generator output only.
  return static_cast<double>(random_()) /
         static_cast<double>(std::mt19937::max());
}

double SharedChannelSimulator::AirtimeSeconds(int sender) const {
  return senders_[sender].airtime_seconds;
}

void SharedChannelSimulator::Step() {
  const double dt = std::chrono::duration<double>(config_.step).count();
  const Duration end = now_ + config_.sample_interval;
  // The generator is only used for enabled effects, so results without
  // jitter stay unchanged.
  if (config_.jitter_max > Duration{0} || config_.spike_probability > 0.0) {
    for (Sender& sender : senders_) {
      const double jitter_seconds =
          std::chrono::duration<double>(config_.jitter_max).count() *
          Uniform();
      sender.jitter = std::chrono::duration_cast<Duration>(
          std::chrono::duration<double>(jitter_seconds));
      if (Uniform() < config_.spike_probability) {
        sender.jitter += config_.spike_delay;
      }
    }
  }
  while (now_ < end) {
    for (Sender& sender : senders_) {
      while (!sender.feedback.empty() &&
             sender.feedback.front().first <= now_) {
        sender.rate_bps = std::min(sender.feedback.front().second,
                                   sender.max_bps);
        sender.feedback.pop_front();
      }
      if (sender.active) {
        sender.queue_bytes += sender.rate_bps * dt / kBitsPerByte;
      }
    }

    const double capacity = CapacityAt(now_);
    const double queued = QueueBytes();
    const double served =
        std::min(queued, EffectiveCapacity() * dt / kBitsPerByte);
    const Duration queue_delay = QueueDelay();
    for (Sender& sender : senders_) {
      if (queued <= 0.0) {
        break;
      }
      const double share = served * sender.queue_bytes / queued;
      const double lost_on_air = share * sender.radio.loss;
      sender.queue_bytes -= share;
      sender.queue_bytes += lost_on_air;  // Retransmission.
      sender.delivered_bytes += share - lost_on_air;
      sender.lost_bytes += lost_on_air;
      sender.airtime_seconds +=
          share * kBitsPerByte / (capacity * sender.radio.efficiency);
      if (queue_delay + sender.jitter > config_.latency) {
        sender.belated_bytes += share - lost_on_air;
      }
    }

    const double after = QueueBytes();
    if (after > config_.queue_limit_bytes) {
      const double excess = after - config_.queue_limit_bytes;
      for (Sender& sender : senders_) {
        const double drop = excess * sender.queue_bytes / after;
        sender.queue_bytes -= drop;
        sender.dropped_bytes += drop;
        sender.lost_bytes += drop;
      }
    }
    now_ += config_.step;
  }

  // Deterministic on every platform: uses the raw generator output only.
  for (Sender& sender : senders_) {
    const double uniform = Uniform();
    double factor = 1.0 + config_.capacity_noise * (2.0 * uniform - 1.0);
    ++sender.reports;
    if (config_.outlier_period > 0 &&
        sender.reports % config_.outlier_period == 0) {
      factor *= config_.outlier_factor;
    }
    sender.reported_capacity_bps =
        CapacityAt(now_) * sender.radio.efficiency * factor;
  }
}

ReceiverMeasurement SharedChannelSimulator::Receiver(int sender) const {
  const Sender& state = senders_[sender];
  const double payload = config_.payload_bytes;
  const Duration delay = QueueDelay() + state.jitter;

  ReceiverMeasurement measurement;
  measurement.common.timestamp = now_;
  measurement.common.rtt = config_.base_rtt + delay;
  measurement.common.link_capacity_bps = state.reported_capacity_bps;
  measurement.common.mss_bytes = config_.mss_bytes;
  measurement.packets_received = Packets(state.delivered_bytes, payload);
  measurement.packets_received_unique =
      Packets(state.delivered_bytes, payload);
  measurement.packets_lost = Packets(state.lost_bytes, payload);
  measurement.packets_dropped = Packets(state.dropped_bytes, payload);
  measurement.packets_belated = Packets(state.belated_bytes, payload);
  measurement.bytes_received =
      static_cast<std::uint64_t>(state.delivered_bytes);
  measurement.bytes_received_unique =
      static_cast<std::uint64_t>(state.delivered_bytes);
  measurement.bytes_lost = static_cast<std::uint64_t>(state.lost_bytes);
  measurement.bytes_dropped = static_cast<std::uint64_t>(state.dropped_bytes);
  measurement.receive_buffer_delay =
      std::max(Duration{0}, config_.latency - delay);
  measurement.tsbpd_delay = config_.latency;
  return measurement;
}

double SharedChannelSimulator::CapacityAt(Duration time) const {
  double capacity = config_.capacity_profile.front().capacity_bps;
  for (const CapacityPhase& phase : config_.capacity_profile) {
    if (phase.start <= time) {
      capacity = phase.capacity_bps;
    }
  }
  if (config_.fading_amplitude > 0.0) {
    const double phase = std::chrono::duration<double>(time).count() /
                         std::chrono::duration<double>(config_.fading_period)
                             .count();
    capacity *= 1.0 + config_.fading_amplitude * std::sin(2.0 * kPi * phase);
  }
  return capacity;
}

double SharedChannelSimulator::QueueBytes() const {
  double total = 0.0;
  for (const Sender& sender : senders_) {
    total += sender.queue_bytes;
  }
  return total;
}

// Byte rate of the channel for the current queue mix: every byte of a
// sender with efficiency e takes 1/e of the airtime.
double SharedChannelSimulator::EffectiveCapacity() const {
  const double queued = QueueBytes();
  if (queued <= 0.0) {
    return CapacityAt(now_);
  }
  double airtime_per_byte = 0.0;
  for (const Sender& sender : senders_) {
    airtime_per_byte +=
        sender.queue_bytes / queued / sender.radio.efficiency;
  }
  return CapacityAt(now_) / airtime_per_byte;
}

Duration SharedChannelSimulator::QueueDelay() const {
  const double seconds = QueueBytes() * kBitsPerByte / EffectiveCapacity();
  return std::chrono::duration_cast<Duration>(
      std::chrono::duration<double>(seconds));
}

}  // namespace test_support
}  // namespace bwe
