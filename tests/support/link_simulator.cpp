#include "support/link_simulator.hpp"

#include <algorithm>
#include <utility>

namespace bwe
{
namespace test_support
{
namespace
{

constexpr double kBitsPerByte = 8.0;

double Seconds(Duration duration)
{
	return std::chrono::duration<double>(duration).count();
}

std::uint64_t Packets(double bytes, double payload_bytes)
{
	return static_cast<std::uint64_t>(bytes / payload_bytes);
}

}  // namespace

LinkSimulator::LinkSimulator(LinkSimulatorConfig config)
	: config_(std::move(config)), random_(config_.seed)
{
	reported_capacity_bps_ = CapacityAt(now_);
}

void LinkSimulator::Step()
{
	const double dt = Seconds(config_.step);
	const Duration end = now_ + config_.sample_interval;
	while (now_ < end)
	{
		const double capacity = CapacityAt(now_);
		const double arrivals = config_.input_bps * dt / kBitsPerByte;

		queue_bytes_ += arrivals;
		const double served =
			std::min(queue_bytes_, capacity * dt / kBitsPerByte);
		queue_bytes_ -= served;
		const double dropped =
			std::max(0.0, queue_bytes_ - config_.queue_limit_bytes);
		queue_bytes_ -= dropped;

		bytes_sent_ += arrivals;
		bytes_delivered_ += served;
		bytes_lost_ += dropped;
		if (QueueDelay() > config_.latency)
		{
			bytes_belated_ += served;
		}
		now_ += config_.step;
	}

	// Deterministic on every platform: uses the raw generator output only.
	const double uniform = static_cast<double>(random_()) /
						   static_cast<double>(std::mt19937::max());
	double factor = 1.0 + config_.capacity_noise * (2.0 * uniform - 1.0);
	++samples_;
	if (config_.outlier_period > 0 && samples_ % config_.outlier_period == 0)
	{
		factor *= config_.outlier_factor;
	}
	reported_capacity_bps_ = CapacityAt(now_) * factor;
}

double LinkSimulator::CapacityBps() const
{
	return CapacityAt(now_);
}

double LinkSimulator::CapacityAt(Duration time) const
{
	double capacity = config_.capacity_profile.front().capacity_bps;
	for (const CapacityPhase& phase : config_.capacity_profile)
	{
		if (phase.start <= time)
		{
			capacity = phase.capacity_bps;
		}
	}
	return capacity;
}

Duration LinkSimulator::QueueDelay() const
{
	const double seconds =
		queue_bytes_ * kBitsPerByte / CapacityAt(now_);
	return std::chrono::duration_cast<Duration>(
		std::chrono::duration<double>(seconds));
}

CommonStats LinkSimulator::Common() const
{
	CommonStats common;
	common.timestamp = now_;
	common.rtt = config_.base_rtt + QueueDelay();
	common.link_capacity_bps = reported_capacity_bps_;
	common.mss_bytes = config_.mss_bytes;
	return common;
}

SenderMeasurement LinkSimulator::Sender() const
{
	SenderMeasurement measurement;
	measurement.common = Common();
	const double payload = config_.payload_bytes;
	measurement.packets_sent = Packets(bytes_sent_, payload);
	measurement.packets_sent_unique = Packets(bytes_sent_, payload);
	measurement.packets_lost = Packets(bytes_lost_, payload);
	measurement.bytes_sent = static_cast<std::uint64_t>(bytes_sent_);
	measurement.bytes_sent_unique = static_cast<std::uint64_t>(bytes_sent_);
	measurement.send_buffer_delay = measurement.common.rtt;
	return measurement;
}

ReceiverMeasurement LinkSimulator::Receiver() const
{
	ReceiverMeasurement measurement;
	measurement.common = Common();
	const double payload = config_.payload_bytes;
	measurement.packets_received = Packets(bytes_delivered_, payload);
	measurement.packets_received_unique = Packets(bytes_delivered_, payload);
	measurement.packets_lost = Packets(bytes_lost_, payload);
	measurement.packets_belated = Packets(bytes_belated_, payload);
	measurement.bytes_received = static_cast<std::uint64_t>(bytes_delivered_);
	measurement.bytes_received_unique =
		static_cast<std::uint64_t>(bytes_delivered_);
	measurement.bytes_lost = static_cast<std::uint64_t>(bytes_lost_);
	measurement.receive_buffer_delay =
		std::max(Duration{0}, config_.latency - QueueDelay());
	measurement.tsbpd_delay = config_.latency;
	return measurement;
}

}  // namespace test_support
}  // namespace bwe
