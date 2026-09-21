#include "bwe/TfrcAlgorithm.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bwe
{

namespace
{

/// Maximum time between two packets in seconds, gives the lower rate limit.
constexpr double MaxPacketIntervalS = 64.0;

}

TfrcAlgorithm::TfrcAlgorithm(std::uint32_t packetSizeBytes)
	: m_packetSizeBytes(static_cast<double>(packetSizeBytes))
{
	if (packetSizeBytes == 0)
	{
		throw std::invalid_argument("bwe::TfrcAlgorithm: packetSizeBytes must be > 0");
	}
}

double TfrcAlgorithm::estimate(const Input& input)
{
	if (!std::isfinite(input.rttMs) || input.rttMs <= 0.0)
	{
		throw std::invalid_argument("bwe::TfrcAlgorithm: rttMs must be > 0");
	}
	if (!std::isfinite(input.dropRatePercent) || input.dropRatePercent < 0.0 || input.dropRatePercent > 100.0)
	{
		throw std::invalid_argument("bwe::TfrcAlgorithm: dropRatePercent must be within 0 to 100");
	}
	if (!std::isfinite(input.receiveRateBps) || input.receiveRateBps < 0.0)
	{
		throw std::invalid_argument("bwe::TfrcAlgorithm: receiveRateBps must be >= 0");
	}

	const double minRateBps = m_packetSizeBytes * 8.0 / MaxPacketIntervalS;
	const double maxRateBps = 2.0 * input.receiveRateBps;

	const double p = input.dropRatePercent / 100.0;
	if (p == 0.0)
	{
		return std::max(maxRateBps, minRateBps);
	}

	const double rttS = input.rttMs / 1000.0;
	const double rtoS = 4.0 * rttS;
	const double denominator = rttS * std::sqrt(2.0 * p / 3.0)
		+ rtoS * 3.0 * std::sqrt(3.0 * p / 8.0) * p * (1.0 + 32.0 * p * p);
	const double equationRateBps = m_packetSizeBytes * 8.0 / denominator;

	return std::max(std::min(equationRateBps, maxRateBps), minRateBps);
}

}
