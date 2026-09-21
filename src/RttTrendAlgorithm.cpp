#include "RttTrendAlgorithm.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bwe
{

namespace
{

/// Maximum time between two packets in seconds, gives the lower rate limit.
constexpr double kMaxPacketIntervalS = 64.0;

/// RTT above the observed baseline that counts as queueing, i.e. real congestion.
constexpr double kQueueDelayThresholdMs = 30.0;

/// Drop rate above which loss is treated as a congestion backstop instead of background noise.
constexpr double kLossBackstopPercent = 10.0;

/// Multiplicative decrease on overuse or severe loss (matches WebRTC's GCC).
constexpr double kDecreaseFactor = 0.85;

}

RttTrendAlgorithm::RttTrendAlgorithm(uint32_t packet_size_bytes)
	: packet_size_bytes_(static_cast<double>(packet_size_bytes))
{
	if (packet_size_bytes == 0)
	{
		throw std::invalid_argument("bwe::RttTrendAlgorithm: packet_size_bytes must be > 0");
	}
}

double RttTrendAlgorithm::Estimate(const Input& input)
{
	if (!std::isfinite(input.rtt_ms) || input.rtt_ms <= 0.0)
	{
		throw std::invalid_argument("bwe::RttTrendAlgorithm: rtt_ms must be > 0");
	}
	if (!std::isfinite(input.drop_rate_percent) || input.drop_rate_percent < 0.0 || input.drop_rate_percent > 100.0)
	{
		throw std::invalid_argument("bwe::RttTrendAlgorithm: drop_rate_percent must be within 0 to 100");
	}
	if (!std::isfinite(input.receive_rate_bps) || input.receive_rate_bps < 0.0)
	{
		throw std::invalid_argument("bwe::RttTrendAlgorithm: receive_rate_bps must be >= 0");
	}

	const double min_rate_bps = packet_size_bytes_ * 8.0 / kMaxPacketIntervalS;

	if (!initialized_)
	{
		rate_bps_ = input.receive_rate_bps;
		min_rtt_ms_ = input.rtt_ms;
		initialized_ = true;
	}
	min_rtt_ms_ = std::min(min_rtt_ms_, input.rtt_ms);

	const double queue_delay_ms = input.rtt_ms - min_rtt_ms_;
	const bool overuse = queue_delay_ms > kQueueDelayThresholdMs;
	const bool severe_loss = input.drop_rate_percent > kLossBackstopPercent;

	if (overuse || severe_loss)
	{
		rate_bps_ *= kDecreaseFactor;
	}
	else
	{
		rate_bps_ += packet_size_bytes_ * 8.0;
	}

	rate_bps_ = std::max(rate_bps_, min_rate_bps);
	return rate_bps_;
}

}
