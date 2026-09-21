#include "bwe/TfrcAlgorithm.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bwe
{

namespace
{

/// Maximum time between two packets in seconds, gives the lower rate limit.
constexpr double kMaxPacketIntervalS = 64.0;

}

TfrcAlgorithm::TfrcAlgorithm(uint32_t packet_size_bytes)
	: packet_size_bytes_(static_cast<double>(packet_size_bytes))
{
	if (packet_size_bytes == 0)
	{
		throw std::invalid_argument("bwe::TfrcAlgorithm: packet_size_bytes must be > 0");
	}
}

double TfrcAlgorithm::Estimate(const Input& input)
{
	if (!std::isfinite(input.rtt_ms) || input.rtt_ms <= 0.0)
	{
		throw std::invalid_argument("bwe::TfrcAlgorithm: rtt_ms must be > 0");
	}
	if (!std::isfinite(input.drop_rate_percent) || input.drop_rate_percent < 0.0 || input.drop_rate_percent > 100.0)
	{
		throw std::invalid_argument("bwe::TfrcAlgorithm: drop_rate_percent must be within 0 to 100");
	}
	if (!std::isfinite(input.receive_rate_bps) || input.receive_rate_bps < 0.0)
	{
		throw std::invalid_argument("bwe::TfrcAlgorithm: receive_rate_bps must be >= 0");
	}

	const double min_rate_bps = packet_size_bytes_ * 8.0 / kMaxPacketIntervalS;
	const double max_rate_bps = 2.0 * input.receive_rate_bps;

	const double p = input.drop_rate_percent / 100.0;
	if (p == 0.0)
	{
		return std::max(max_rate_bps, min_rate_bps);
	}

	const double rtt_s = input.rtt_ms / 1000.0;
	const double rto_s = 4.0 * rtt_s;
	const double denominator = rtt_s * std::sqrt(2.0 * p / 3.0)
		+ rto_s * 3.0 * std::sqrt(3.0 * p / 8.0) * p * (1.0 + 32.0 * p * p);
	const double equation_rate_bps = packet_size_bytes_ * 8.0 / denominator;

	return std::max(std::min(equation_rate_bps, max_rate_bps), min_rate_bps);
}

}
