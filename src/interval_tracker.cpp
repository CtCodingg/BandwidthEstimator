#include "interval_tracker.hpp"

#include <algorithm>
#include <cstdint>

namespace bwe
{
namespace internal
{
namespace
{

// Returns the counter increase, or std::nullopt if a value is missing or the
// counter decreased (reset).
std::optional<std::uint64_t> Delta(const std::optional<std::uint64_t>& from,
								   const std::optional<std::uint64_t>& to)
{
	if (!from || !to || *to < *from)
	{
		return std::nullopt;
	}
	return *to - *from;
}

// Returns the first delta that is available.
std::optional<std::uint64_t> FirstOf(const std::optional<std::uint64_t>& a,
									 const std::optional<std::uint64_t>& b)
{
	return a ? a : b;
}

// Returns part / total clamped to [0, 1], or std::nullopt if not computable.
std::optional<double> Ratio(const std::optional<std::uint64_t>& part,
							const std::optional<std::uint64_t>& total)
{
	if (!part || !total || *total == 0)
	{
		return std::nullopt;
	}
	const double ratio =
		static_cast<double>(*part) / static_cast<double>(*total);
	return std::clamp(ratio, 0.0, 1.0);
}

// Returns a + b, or std::nullopt if one of them is missing.
std::optional<std::uint64_t> Sum(const std::optional<std::uint64_t>& a,
								 const std::optional<std::uint64_t>& b)
{
	if (!a || !b)
	{
		return std::nullopt;
	}
	return *a + *b;
}

std::optional<double> RateBps(const std::optional<std::uint64_t>& bytes,
							  Duration duration)
{
	if (!bytes)
	{
		return std::nullopt;
	}
	constexpr double kBitsPerByte = 8.0;
	constexpr double kMicrosecondsPerSecond = 1e6;
	return static_cast<double>(*bytes) * kBitsPerByte * kMicrosecondsPerSecond /
		   static_cast<double>(duration.count());
}

// Fills the fields shared by both sides.
IntervalSample MakeSample(Side side, const CommonStats& previous,
						  const CommonStats& current)
{
	IntervalSample sample;
	sample.side = side;
	sample.timestamp = current.timestamp;
	sample.duration = current.timestamp - previous.timestamp;
	sample.rtt = current.rtt;
	sample.link_capacity_bps = current.link_capacity_bps;
	sample.mss_bytes = current.mss_bytes;
	return sample;
}

}  // namespace

std::optional<IntervalSample> SenderIntervalTracker::Update(
	const SenderMeasurement& measurement) noexcept
{
	if (previous_ &&
		measurement.common.timestamp <= previous_->common.timestamp)
	{
		return std::nullopt;
	}
	if (!previous_)
	{
		previous_ = measurement;
		return std::nullopt;
	}

	const SenderMeasurement& prev = *previous_;
	const SenderMeasurement& cur = measurement;
	IntervalSample sample = MakeSample(Side::kSender, prev.common, cur.common);

	const auto sent = FirstOf(
		Delta(prev.packets_sent_unique, cur.packets_sent_unique),
		Delta(prev.packets_sent, cur.packets_sent));
	const auto lost = Delta(prev.packets_lost, cur.packets_lost);
	const auto dropped = Delta(prev.packets_dropped, cur.packets_dropped);
	const auto bytes =
		FirstOf(Delta(prev.bytes_sent_unique, cur.bytes_sent_unique),
				Delta(prev.bytes_sent, cur.bytes_sent));

	sample.loss_rate = Ratio(lost, sent);
	sample.throughput_bps = RateBps(bytes, sample.duration);
	if (sample.throughput_bps && sample.loss_rate)
	{
		// Only the share that is not lost reaches the receiver.
		*sample.throughput_bps *= 1.0 - *sample.loss_rate;
	}
	sample.drop_rate = Ratio(dropped, Sum(sent, dropped));
	sample.buffer_delay = cur.send_buffer_delay;
	sample.max_bandwidth_bps = cur.max_bandwidth_bps;

	previous_ = measurement;
	return sample;
}

void SenderIntervalTracker::Reset() noexcept
{
	previous_.reset();
}

std::optional<IntervalSample> ReceiverIntervalTracker::Update(
	const ReceiverMeasurement& measurement) noexcept
{
	if (previous_ &&
		measurement.common.timestamp <= previous_->common.timestamp)
	{
		return std::nullopt;
	}
	if (!previous_)
	{
		previous_ = measurement;
		return std::nullopt;
	}

	const ReceiverMeasurement& prev = *previous_;
	const ReceiverMeasurement& cur = measurement;
	IntervalSample sample =
		MakeSample(Side::kReceiver, prev.common, cur.common);

	const auto received = FirstOf(
		Delta(prev.packets_received_unique, cur.packets_received_unique),
		Delta(prev.packets_received, cur.packets_received));
	const auto lost = Delta(prev.packets_lost, cur.packets_lost);
	const auto dropped = Delta(prev.packets_dropped, cur.packets_dropped);
	const auto belated = Delta(prev.packets_belated, cur.packets_belated);
	const auto bytes =
		FirstOf(Delta(prev.bytes_received_unique, cur.bytes_received_unique),
				Delta(prev.bytes_received, cur.bytes_received));
	const auto expected = Sum(received, lost);

	sample.throughput_bps = RateBps(bytes, sample.duration);
	sample.loss_rate = Ratio(lost, expected);
	sample.drop_rate = Ratio(dropped, expected);
	sample.late_rate = Ratio(belated, received);
	sample.buffer_delay = cur.receive_buffer_delay;
	sample.buffer_target = cur.tsbpd_delay;

	previous_ = measurement;
	return sample;
}

void ReceiverIntervalTracker::Reset() noexcept
{
	previous_.reset();
}

}  // namespace internal
}  // namespace bwe
