#pragma once

#include "bwe/IAlgorithm.hpp"

#include <cstdint>

namespace bwe
{

/// Delay-primary, loss-backstop rate control: grows the rate by one packet per call while RTT
/// stays near its observed baseline and drops are only ordinary background noise; cuts it
/// (multiplicative decrease) once RTT rises meaningfully above baseline (queueing, i.e. real
/// congestion) or drops become severe (a backstop for a fully congested link with too little
/// buffering to show up as queueing delay first).
///
/// Unlike TfrcAlgorithm/AimdAlgorithm, ordinary loss alone does not cut the rate: on a radio link,
/// most loss is corruption from interference or fading, not a congestion signal, and treating it
/// as one (as RFC 5348 and classic AIMD both do) needlessly starves a link that is otherwise fine.
///
/// Stateful, like AimdAlgorithm: every estimate depends on the previous one and on the lowest RTT
/// observed so far (its baseline never decreases, so a permanent route change to a higher-latency
/// path is read as sustained congestion until the next process restart - a known limitation, not
/// handled here). One instance must be used for one channel for its whole lifetime.
///
/// The RTT-rise and loss thresholds below are heuristics, not a standardized reference equation
/// like TFRC's: tune them to the link if the defaults over- or under-react.
class RttTrendAlgorithm : public IAlgorithm
{
public:
	/// @param packet_size_bytes Payload size of one packet in bytes, used as the additive
	///        increase step and the lower rate bound.
	/// @throws std::invalid_argument if @p packet_size_bytes is 0.
	explicit RttTrendAlgorithm(uint32_t packet_size_bytes);

	/// Calculates the total rate available on the channel, later split among its streams.
	/// The first call seeds the internal rate at @p input.receive_rate_bps and the RTT baseline at
	/// @p input.rtt_ms.
	/// @param input Current condition of the channel.
	/// @return Estimated rate in bit/s.
	/// @throws std::invalid_argument if rtt_ms <= 0, drop_rate_percent is outside 0 to 100,
	///         receive_rate_bps < 0 or a value is not finite.
	double Estimate(const Input& input) override;

private:
	double packet_size_bytes_ = 0.;
	double rate_bps_ = 0.;
	double min_rtt_ms_ = 0.;
	bool initialized_ = false;
};

}
