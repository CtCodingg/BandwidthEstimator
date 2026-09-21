#pragma once

#include <cstdint>

namespace bwe
{

/// Identifies one received stream, i.e. one sender.
using StreamId = std::uint32_t;

/// Available estimation algorithms.
enum class AlgorithmType
{
	Tfrc ///< TCP-Friendly Rate Control, throughput equation of RFC 5348.
};

/// Estimator configuration.
struct Config
{
	AlgorithmType algorithm = AlgorithmType::Tfrc; ///< Algorithm to use.
	std::uint32_t packetSizeBytes = 1316; ///< Payload size of one packet in bytes, must be > 0.
};

/// Measurements of one stream, taken at the receiver.
struct Input
{
	StreamId streamId = 0; ///< Stream (sender) the values belong to.
	double rttMs = 0.0; ///< Round-trip time in milliseconds, must be > 0.
	double dropRatePercent = 0.0; ///< Share of lost packets in percent, 0 to 100.
	double receiveRateBps = 0.0; ///< Currently received data rate in bit/s, must be >= 0.
};

/// Estimation result for one stream.
struct Output
{
	StreamId streamId = 0; ///< Stream (sender) the result belongs to.
	double rateBps = 0.0; ///< Rate the sender may use in bit/s.
};

}
