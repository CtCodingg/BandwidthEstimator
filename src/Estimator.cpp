#include "bwe/Estimator.hpp"

#include "bwe/AlgorithmFactory.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace bwe
{

Estimator::Estimator(const Config& config)
	: m_algorithm(AlgorithmFactory::create(config))
{
	start(config.updateIntervalMs);
}

Estimator::Estimator(std::unique_ptr<IAlgorithm> algorithm, std::uint32_t updateIntervalMs)
	: m_algorithm(std::move(algorithm))
{
	if (!m_algorithm)
	{
		throw std::invalid_argument("bwe::Estimator: algorithm must not be null");
	}
	start(updateIntervalMs);
}

void Estimator::start(std::uint32_t updateIntervalMs)
{
	if (updateIntervalMs == 0)
	{
		throw std::invalid_argument("bwe::Estimator: updateIntervalMs must be > 0");
	}
	m_interval = std::chrono::milliseconds(updateIntervalMs);
	m_running = true;
	m_thread = std::thread(&Estimator::run, this);
}

Estimator::~Estimator()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_running = false;
	}
	m_wake.notify_all();
	if (m_thread.joinable())
	{
		m_thread.join();
	}
}

void Estimator::updateChannel(double rttMs, double dropRatePercent)
{
	if (!std::isfinite(rttMs) || rttMs <= 0.0)
	{
		throw std::invalid_argument("bwe::Estimator: rttMs must be > 0");
	}
	if (!std::isfinite(dropRatePercent) || dropRatePercent < 0.0 || dropRatePercent > 100.0)
	{
		throw std::invalid_argument("bwe::Estimator: dropRatePercent must be within 0 to 100");
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_rttMs = rttMs;
	m_dropRatePercent = dropRatePercent;
	m_channelSet = true;
}

void Estimator::updateStream(const StreamInput& stream)
{
	if (!std::isfinite(stream.receiveRateBps) || stream.receiveRateBps < 0.0)
	{
		throw std::invalid_argument("bwe::Estimator: stream.receiveRateBps must be >= 0");
	}
	if (!std::isfinite(stream.weight) || stream.weight <= 0.0)
	{
		throw std::invalid_argument("bwe::Estimator: stream.weight must be > 0");
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_streams[stream.streamId] = stream;
}

void Estimator::removeStream(StreamId streamId)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_streams.erase(streamId);
}

std::vector<Output> Estimator::outputs() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_outputs;
}

void Estimator::run()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	while (m_running)
	{
		m_wake.wait_for(lock, m_interval, [this]() { return !m_running; });
		if (!m_running)
		{
			break;
		}
		recalculate();
	}
}

// Assumes m_mutex is held by the caller (only run(), while holding the lock).
void Estimator::recalculate()
{
	if (!m_channelSet || m_streams.empty())
	{
		return;
	}

	double totalReceiveRateBps = 0.0;
	double totalWeight = 0.0;
	for (const auto& entry : m_streams)
	{
		totalReceiveRateBps += entry.second.receiveRateBps;
		totalWeight += entry.second.weight;
	}

	Input channelInput;
	channelInput.rttMs = m_rttMs;
	channelInput.dropRatePercent = m_dropRatePercent;
	channelInput.receiveRateBps = totalReceiveRateBps;

	double totalRateBps;
	try
	{
		totalRateBps = m_algorithm->estimate(channelInput);
	}
	catch (const std::exception&)
	{
		// Keep the previous outputs; a misbehaving algorithm must not crash the background thread.
		return;
	}

	std::vector<Output> outputs;
	outputs.reserve(m_streams.size());
	for (const auto& entry : m_streams)
	{
		Output output;
		output.streamId = entry.first;
		output.rateBps = totalRateBps * entry.second.weight / totalWeight;
		outputs.push_back(output);
	}
	m_outputs = std::move(outputs);
}

}
