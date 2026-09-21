#pragma once

#include "bwe/IAlgorithm.hpp"

#include <cstdint>

namespace bwe
{

/// Additive-Increase/Multiplicative-Decrease rate control, the scheme classic TCP and RTP
/// congestion control use: grows the rate by one packet per call while there are no drops, halves
/// it the instant any drop is reported.
///
/// Stateful: unlike TfrcAlgorithm, every estimate depends on the previous one, so one instance
/// must be used for one channel for its whole lifetime (which is exactly how Estimator uses it).
class AimdAlgorithm : public IAlgorithm
{
public:
	/// @param packet_size_bytes Payload size of one packet in bytes, used as the additive
	///        increase step and the lower rate bound.
	/// @throws std::invalid_argument if @p packet_size_bytes is 0.
	explicit AimdAlgorithm(uint32_t packet_size_bytes);

	/// Calculates the total rate available on the channel, later split among its streams.
	/// The first call seeds the internal rate at @p input.receive_rate_bps.
	/// @param input Current condition of the channel.
	/// @return Estimated rate in bit/s.
	/// @throws std::invalid_argument if rtt_ms <= 0, drop_rate_percent is outside 0 to 100,
	///         receive_rate_bps < 0 or a value is not finite.
	double Estimate(const Input& input) override;

private:
	double packet_size_bytes_ = 0.;
	double rate_bps_ = 0.;
	bool initialized_ = false;
};

}
