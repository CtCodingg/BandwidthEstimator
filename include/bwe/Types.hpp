#pragma once

#include <cstdint>
#include <limits>

namespace bwe
{

/// Identifies one stream sharing the channel, i.e. one sender.
using StreamId = uint32_t;

/// Available estimation algorithms.
enum class AlgorithmType
{
	kTfrc ///< TCP-Friendly Rate Control, throughput equation of RFC 5348.
};

/// Estimator configuration.
struct Config
{
	AlgorithmType algorithm = AlgorithmType::kTfrc; ///< Algorithm to use.
	uint32_t packet_size_bytes = 1316; ///< Payload size of one packet in bytes, must be > 0.
	uint32_t update_interval_ms = 100; ///< How often the background thread recalculates the outputs, must be > 0.
};

/// Condition of the channel, used by IAlgorithm to estimate its total rate.
/// Carries no per-stream information; it is the aggregate an algorithm call sees.
struct Input
{
	double rtt_ms = 0.0; ///< Round-trip time in milliseconds, must be > 0.
	double drop_rate_percent = 0.0; ///< Share of lost packets in percent, 0 to 100.
	double receive_rate_bps = 0.0; ///< Currently received data rate of the channel in bit/s, must be >= 0.
};

/// Measurement of one stream sharing the channel, taken at the receiver.
struct StreamInput
{
	StreamId stream_id = 0; ///< Stream (sender) the values belong to.
	double receive_rate_bps = 0.0; ///< Currently received data rate of this stream in bit/s, must be >= 0.
	double weight = 1.0; ///< Share of the channel this stream gets, relative to the other streams' weights. Must be > 0.
	double max_rate_bps = std::numeric_limits<double>::infinity(); ///< Upper bound of the output rate. Must be > 0.
};

/// Estimation result for one stream.
struct Output
{
	StreamId stream_id = 0; ///< Stream (sender) the result belongs to.
	double rate_bps = 0.0; ///< Rate the sender may use in bit/s.
};

}
