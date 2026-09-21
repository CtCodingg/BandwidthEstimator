#include "bwe/AimdAlgorithm.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bwe
{

namespace
{

/// Maximum time between two packets in seconds, gives the lower rate limit.
constexpr double kMaxPacketIntervalS = 64.0;

/// Classic TCP AIMD: cut the rate in half the instant any drop is reported.
constexpr double kMultiplicativeDecrease = 0.5;

}

AimdAlgorithm::AimdAlgorithm(uint32_t packet_size_bytes)
	: packet_size_bytes_(static_cast<double>(packet_size_bytes))
{
	if (packet_size_bytes == 0)
	{
		throw std::invalid_argument("bwe::AimdAlgorithm: packet_size_bytes must be > 0");
	}
}

double AimdAlgorithm::Estimate(const Input& input)
{
	if (!std::isfinite(input.rtt_ms) || input.rtt_ms <= 0.0)
	{
		throw std::invalid_argument("bwe::AimdAlgorithm: rtt_ms must be > 0");
	}
	if (!std::isfinite(input.drop_rate_percent) || input.drop_rate_percent < 0.0 || input.drop_rate_percent > 100.0)
	{
		throw std::invalid_argument("bwe::AimdAlgorithm: drop_rate_percent must be within 0 to 100");
	}
	if (!std::isfinite(input.receive_rate_bps) || input.receive_rate_bps < 0.0)
	{
		throw std::invalid_argument("bwe::AimdAlgorithm: receive_rate_bps must be >= 0");
	}

	const double min_rate_bps = packet_size_bytes_ * 8.0 / kMaxPacketIntervalS;

	if (!initialized_)
	{
		rate_bps_ = input.receive_rate_bps;
		initialized_ = true;
	}

	if (input.drop_rate_percent > 0.0)
	{
		rate_bps_ *= kMultiplicativeDecrease;
	}
	else
	{
		rate_bps_ += packet_size_bytes_ * 8.0;
	}

	rate_bps_ = std::max(rate_bps_, min_rate_bps);
	return rate_bps_;
}

}
