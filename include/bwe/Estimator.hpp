#pragma once

#include "bwe/IAlgorithm.hpp"
#include "bwe/Types.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace bwe
{

/// Default interval of the background thread, matches Config::updateIntervalMs.
constexpr std::uint32_t DefaultUpdateIntervalMs = 100;

/// Estimates the total rate of a shared channel and splits it among the streams on it, weighted.
///
/// Runs a background thread for its whole lifetime: updateChannel()/updateStream()/removeStream()
/// only record the latest known measurements, the thread recalculates all outputs on a fixed
/// interval. outputs() returns the most recent result, for every known stream at once.
/// All methods are thread-safe and never block on the background thread.
///
/// An exception thrown by the algorithm during a background recalculation is swallowed and the
/// previous outputs are kept; it never terminates the program.
class Estimator
{
public:
	/// Creates the estimator with the algorithm selected in @p config and starts its thread.
	/// @throws std::invalid_argument if @p config is invalid.
	explicit Estimator(const Config& config);

	/// Creates the estimator with a custom algorithm and starts its thread.
	/// @throws std::invalid_argument if @p algorithm is null or @p updateIntervalMs is 0.
	explicit Estimator(std::unique_ptr<IAlgorithm> algorithm, std::uint32_t updateIntervalMs = DefaultUpdateIntervalMs);

	/// Stops the background thread and waits for it to finish.
	~Estimator();

	Estimator(const Estimator&) = delete;
	Estimator& operator=(const Estimator&) = delete;

	/// Sets the channel's condition, used by every stream from the next recalculation on.
	/// @throws std::invalid_argument if rttMs <= 0 or dropRatePercent is outside 0 to 100.
	void updateChannel(double rttMs, double dropRatePercent);

	/// Adds @p stream, or updates it if its stream id is already known.
	/// @throws std::invalid_argument if receiveRateBps < 0 or weight <= 0.
	void updateStream(const StreamInput& stream);

	/// Removes a stream, freeing its share of the channel for the others. Does nothing if unknown.
	void removeStream(StreamId streamId);

	/// @return The result of the most recent recalculation, one entry per currently known stream.
	///         Empty until the channel condition and at least one stream have been set and the
	///         background thread has run at least once.
	std::vector<Output> outputs() const;

private:
	void start(std::uint32_t updateIntervalMs);
	void run();
	void recalculate();

	std::unique_ptr<IAlgorithm> m_algorithm;
	std::chrono::milliseconds m_interval{ 0 };

	mutable std::mutex m_mutex;
	std::condition_variable m_wake;
	bool m_running = false;
	std::thread m_thread;

	bool m_channelSet = false;
	double m_rttMs = 0.0;
	double m_dropRatePercent = 0.0;
	std::map<StreamId, StreamInput> m_streams;
	std::vector<Output> m_outputs;
};

}
