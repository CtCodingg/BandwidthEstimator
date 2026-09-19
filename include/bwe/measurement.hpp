/// @file
/// @brief Input structures for the bandwidth estimators.
///
/// Unset optional fields make an estimator report an invalid estimate.
/// Counters are cumulative totals; the library computes the deltas itself.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace bwe
{

/// @brief Time type used for all timestamps and durations.
using Duration = std::chrono::microseconds;

/// @brief Statistics available on sender and receiver side.
struct CommonStats
{
	/// @brief Monotonic time at which the statistics were sampled.
	Duration timestamp{0};
	/// @brief Smoothed round-trip time.
	std::optional<Duration> rtt;
	/// @brief Link capacity estimated by the transport layer in bit/s.
	std::optional<double> link_capacity_bps;
	/// @brief Maximum segment size in bytes.
	std::optional<std::uint32_t> mss_bytes;
};

/// @brief Statistics observed on the sending side.
struct SenderMeasurement
{
	/// @brief Statistics common to both sides.
	CommonStats common;

	/// @brief Packets sent, including retransmissions.
	std::optional<std::uint64_t> packets_sent;
	/// @brief Packets sent, excluding retransmissions.
	std::optional<std::uint64_t> packets_sent_unique;
	/// @brief Packets reported as lost by the receiver.
	std::optional<std::uint64_t> packets_lost;
	/// @brief Packets retransmitted.
	std::optional<std::uint64_t> packets_retransmitted;
	/// @brief Packets dropped by the sender because they were too late.
	std::optional<std::uint64_t> packets_dropped;
	/// @brief Bytes sent, including retransmissions.
	std::optional<std::uint64_t> bytes_sent;
	/// @brief Bytes sent, excluding retransmissions.
	std::optional<std::uint64_t> bytes_sent_unique;
	/// @brief Bytes retransmitted.
	std::optional<std::uint64_t> bytes_retransmitted;
	/// @brief Bytes dropped by the sender because they were too late.
	std::optional<std::uint64_t> bytes_dropped;

	/// @brief Time span of the data queued in the send buffer.
	std::optional<Duration> send_buffer_delay;
	/// @brief Bytes queued in the send buffer.
	std::optional<std::uint64_t> send_buffer_bytes;
	/// @brief Packets queued in the send buffer.
	std::optional<std::uint32_t> send_buffer_packets;
	/// @brief Packets sent but not yet acknowledged.
	std::optional<std::uint32_t> flight_size_packets;
	/// @brief Current congestion window in packets.
	std::optional<std::uint32_t> congestion_window_packets;
	/// @brief Configured sending rate limit in bit/s; unset if unlimited.
	std::optional<double> max_bandwidth_bps;
};

/// @brief Statistics observed on the receiving side.
struct ReceiverMeasurement
{
	/// @brief Statistics common to both sides.
	CommonStats common;

	/// @brief Packets received, including retransmissions.
	std::optional<std::uint64_t> packets_received;
	/// @brief Packets received, excluding duplicates.
	std::optional<std::uint64_t> packets_received_unique;
	/// @brief Packets detected as lost.
	std::optional<std::uint64_t> packets_lost;
	/// @brief Packets dropped because they could not be delivered in time.
	std::optional<std::uint64_t> packets_dropped;
	/// @brief Packets that arrived too late to be used.
	std::optional<std::uint64_t> packets_belated;
	/// @brief Bytes received, including retransmissions.
	std::optional<std::uint64_t> bytes_received;
	/// @brief Bytes received, excluding duplicates.
	std::optional<std::uint64_t> bytes_received_unique;
	/// @brief Bytes detected as lost.
	std::optional<std::uint64_t> bytes_lost;
	/// @brief Bytes dropped because they could not be delivered in time.
	std::optional<std::uint64_t> bytes_dropped;

	/// @brief Time span of the data queued in the receive buffer.
	std::optional<Duration> receive_buffer_delay;
	/// @brief Bytes queued in the receive buffer.
	std::optional<std::uint64_t> receive_buffer_bytes;
	/// @brief Packets queued in the receive buffer.
	std::optional<std::uint32_t> receive_buffer_packets;
	/// @brief Configured receiver latency for timestamp-based delivery.
	std::optional<Duration> tsbpd_delay;
	/// @brief Largest observed packet reordering distance in packets.
	std::optional<std::uint32_t> reorder_distance_packets;
};

}  // namespace bwe
