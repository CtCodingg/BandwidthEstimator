#include "bwe/Estimator.hpp"

#include "bwe/AlgorithmFactory.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace bwe
{

Estimator::Estimator(const Config& config)
	: algorithm_(AlgorithmFactory::Create(config))
{
	Start(config.update_interval_ms);
}

Estimator::Estimator(std::unique_ptr<IAlgorithm> algorithm, uint32_t update_interval_ms)
	: algorithm_(std::move(algorithm))
{
	if (!algorithm_)
	{
		throw std::invalid_argument("bwe::Estimator: algorithm must not be null");
	}
	Start(update_interval_ms);
}

void Estimator::Start(uint32_t update_interval_ms)
{
	if (update_interval_ms == 0)
	{
		throw std::invalid_argument("bwe::Estimator: update_interval_ms must be > 0");
	}
	interval_ = std::chrono::milliseconds(update_interval_ms);
	running_ = true;
	thread_ = std::thread(&Estimator::Run, this);
}

Estimator::~Estimator()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		running_ = false;
	}
	wake_.notify_all();
	if (thread_.joinable())
	{
		thread_.join();
	}
}

void Estimator::UpdateChannel(double rtt_ms, double drop_rate_percent)
{
	if (!std::isfinite(rtt_ms) || rtt_ms <= 0.0)
	{
		throw std::invalid_argument("bwe::Estimator: rtt_ms must be > 0");
	}
	if (!std::isfinite(drop_rate_percent) || drop_rate_percent < 0.0 || drop_rate_percent > 100.0)
	{
		throw std::invalid_argument("bwe::Estimator: drop_rate_percent must be within 0 to 100");
	}

	std::lock_guard<std::mutex> lock(mutex_);
	rtt_ms_ = rtt_ms;
	drop_rate_percent_ = drop_rate_percent;
	channel_set_ = true;
}

void Estimator::UpdateStream(const StreamInput& stream)
{
	if (!std::isfinite(stream.receive_rate_bps) || stream.receive_rate_bps < 0.0)
	{
		throw std::invalid_argument("bwe::Estimator: stream.receive_rate_bps must be >= 0");
	}
	if (!std::isfinite(stream.weight) || stream.weight <= 0.0)
	{
		throw std::invalid_argument("bwe::Estimator: stream.weight must be > 0");
	}
	if (!(stream.max_rate_bps > 0.0)) // also rejects NaN; +infinity (no cap) passes
	{
		throw std::invalid_argument("bwe::Estimator: stream.max_rate_bps must be > 0");
	}

	std::lock_guard<std::mutex> lock(mutex_);
	streams_[stream.stream_id] = stream;
}

void Estimator::RemoveStream(StreamId stream_id)
{
	std::lock_guard<std::mutex> lock(mutex_);
	streams_.erase(stream_id);
}

std::vector<Output> Estimator::Outputs() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return outputs_;
}

void Estimator::Run()
{
	std::unique_lock<std::mutex> lock(mutex_);
	while (running_)
	{
		wake_.wait_for(lock, interval_, [this]() { return !running_; });
		if (!running_)
		{
			break;
		}
		Recalculate();
	}
}

// Assumes mutex_ is held by the caller (only Run(), while holding the lock).
void Estimator::Recalculate()
{
	if (!channel_set_ || streams_.empty())
	{
		return;
	}

	double total_receive_rate_bps = 0.0;
	double total_weight = 0.0;
	for (const auto& [stream_id, stream] : streams_)
	{
		total_receive_rate_bps += stream.receive_rate_bps;
		total_weight += stream.weight;
	}

	Input channel_input;
	channel_input.rtt_ms = rtt_ms_;
	channel_input.drop_rate_percent = drop_rate_percent_;
	channel_input.receive_rate_bps = total_receive_rate_bps;

	double total_rate_bps;
	try
	{
		total_rate_bps = algorithm_->Estimate(channel_input);
	}
	catch (const std::exception&)
	{
		// Keep the previous outputs; a misbehaving algorithm must not crash the background thread.
		return;
	}

	std::vector<Output> outputs;
	outputs.reserve(streams_.size());
	for (const auto& [stream_id, stream] : streams_)
	{
		Output output;
		output.stream_id = stream_id;
		output.rate_bps = std::min(total_rate_bps * stream.weight / total_weight, stream.max_rate_bps);
		outputs.push_back(output);
	}
	outputs_ = std::move(outputs);
}

}
