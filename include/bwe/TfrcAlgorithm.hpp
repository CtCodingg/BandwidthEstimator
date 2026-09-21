#pragma once

#include "bwe/IAlgorithm.hpp"

#include <cstdint>

namespace bwe
{

/// TCP-Friendly Rate Control (RFC 5348 throughput equation).
/// Stateless: every estimate depends only on the given input.
class TfrcAlgorithm : public IAlgorithm
{
public:
	/// @param packetSizeBytes Payload size of one packet in bytes.
	/// @throws std::invalid_argument if @p packetSizeBytes is 0.
	explicit TfrcAlgorithm(std::uint32_t packetSizeBytes);

	/// Calculates the rate the sender of a stream may use.
	/// @param input Current measurements of the stream.
	/// @return Estimated rate in bit/s.
	/// @throws std::invalid_argument if rttMs <= 0, dropRatePercent is outside 0 to 100,
	///         receiveRateBps < 0 or a value is not finite.
	double estimate(const Input& input) override;

private:
	double m_packetSizeBytes;
};

}
