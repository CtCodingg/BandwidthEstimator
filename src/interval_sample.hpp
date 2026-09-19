/// @file
/// @brief Normalized per-interval values shared by all algorithms.

#pragma once

#include <cstdint>
#include <optional>

#include "bwe/measurement.hpp"

namespace bwe
{
namespace internal
{

/// @brief Side of the connection a sample was observed on.
enum class Side
{
	kSender, kReceiver
};

/// @brief Values of one interval between two consecutive measurements.
struct IntervalSample
{
	/// @brief Side the sample was observed on.
	Side side = Side::kSender;
	/// @brief Timestamp of the end of the interval.
	Duration timestamp{0};
	/// @brief Length of the interval; always greater than zero.
	Duration duration{0};

	/// @brief Round-trip time at the end of the interval.
	std::optional<Duration> rtt;
	/// @brief Link capacity estimated by the transport layer in bit/s.
	std::optional<double> link_capacity_bps;
	/// @brief Maximum segment size in bytes.
	std::optional<std::uint32_t> mss_bytes;

	/// @brief Payload rate delivered to the receiver in bit/s, without
	///        retransmissions. On the sender side approximated as unique
	///        send rate * (1 - loss rate).
	std::optional<double> throughput_bps;
	/// @brief Share of lost packets in [0, 1].
	std::optional<double> loss_rate;
	/// @brief Share of dropped packets in [0, 1].
	std::optional<double> drop_rate;
	/// @brief Share of packets that arrived too late in [0, 1].
	/// Receiver only.
	std::optional<double> late_rate;

	/// @brief Time span of the data queued in the send or receive buffer.
	std::optional<Duration> buffer_delay;
	/// @brief Configured receiver latency. Receiver only.
	std::optional<Duration> buffer_target;
	/// @brief Configured sending rate limit in bit/s. Sender only.
	std::optional<double> max_bandwidth_bps;
};

}  // namespace internal
}  // namespace bwe
