#include "bwe/Estimator.hpp"

#include "bwe/AlgorithmFactory.hpp"

#include <stdexcept>
#include <utility>

namespace bwe
{

Estimator::Estimator(const Config& config)
	: m_algorithm(AlgorithmFactory::create(config))
{
}

Estimator::Estimator(std::unique_ptr<IAlgorithm> algorithm)
	: m_algorithm(std::move(algorithm))
{
	if (!m_algorithm)
	{
		throw std::invalid_argument("bwe::Estimator: algorithm must not be null");
	}
}

void Estimator::subscribe(StreamId streamId, Callback callback)
{
	if (!callback)
	{
		throw std::invalid_argument("bwe::Estimator: callback must not be empty");
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	m_callbacks[streamId] = std::move(callback);
}

void Estimator::unsubscribe(StreamId streamId)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_callbacks.erase(streamId);
}

void Estimator::setObserver(Observer observer)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_observer = std::move(observer);
}

Output Estimator::update(const Input& input)
{
	Output output;
	Callback callback;
	Observer observer;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		output.streamId = input.streamId;
		output.rateBps = m_algorithm->estimate(input);
		const auto it = m_callbacks.find(input.streamId);
		if (it != m_callbacks.end())
		{
			callback = it->second;
		}
		observer = m_observer;
	}

	if (observer)
	{
		observer(input, output);
	}
	if (callback)
	{
		callback(output);
	}
	return output;
}

}
