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
	/// @param packet_size_bytes Payload size of one packet in bytes.
	/// @throws std::invalid_argument if @p packet_size_bytes is 0.
	explicit TfrcAlgorithm(uint32_t packet_size_bytes);

	/// Calculates the total rate available on the channel, later split among its streams.
	/// @param input Current condition of the channel.
	/// @return Estimated rate in bit/s.
	/// @throws std::invalid_argument if rtt_ms <= 0, drop_rate_percent is outside 0 to 100,
	///         receive_rate_bps < 0 or a value is not finite.
	double Estimate(const Input& input) override;

private:
	double packet_size_bytes_ = 0.;
};

}
