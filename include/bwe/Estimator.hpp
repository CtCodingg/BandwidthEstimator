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

/// Default interval of the background thread, matches Config::update_interval_ms.
constexpr uint32_t kDefaultUpdateIntervalMs = 100;

/// Estimates the total rate of a shared channel and splits it among the streams on it, weighted,
/// each capped at its own max_rate_bps.
///
/// Runs a background thread for its whole lifetime: UpdateChannel()/UpdateStream()/RemoveStream()
/// only record the latest known measurements, the thread recalculates all outputs on a fixed
/// interval. Outputs() returns the most recent result, for every known stream at once.
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
	/// @throws std::invalid_argument if @p algorithm is null or @p update_interval_ms is 0.
	explicit Estimator(std::unique_ptr<IAlgorithm> algorithm, uint32_t update_interval_ms = kDefaultUpdateIntervalMs);

	/// Stops the background thread and waits for it to finish.
	~Estimator();

	Estimator(const Estimator&) = delete;
	Estimator& operator=(const Estimator&) = delete;

	/// Sets the channel's condition, used by every stream from the next recalculation on.
	/// @throws std::invalid_argument if rtt_ms <= 0 or drop_rate_percent is outside 0 to 100.
	void UpdateChannel(double rtt_ms, double drop_rate_percent);

	/// Adds @p stream, or updates it if its stream id is already known.
	/// @throws std::invalid_argument if receive_rate_bps < 0, weight <= 0 or max_rate_bps <= 0.
	void UpdateStream(const StreamInput& stream);

	/// Removes a stream, freeing its share of the channel for the others. Does nothing if unknown.
	void RemoveStream(StreamId stream_id);

	/// @return The result of the most recent recalculation, one entry per currently known stream.
	///         Empty until the channel condition and at least one stream have been set and the
	///         background thread has run at least once.
	std::vector<Output> Outputs() const;

private:
	void Start(uint32_t update_interval_ms);
	void Run();
	void Recalculate();

	std::unique_ptr<IAlgorithm> algorithm_;
	std::chrono::milliseconds interval_{ 0 };

	mutable std::mutex mutex_;
	std::condition_variable wake_;
	bool running_ = false;
	std::thread thread_;

	bool channel_set_ = false;
	double rtt_ms_ = 0.0;
	double drop_rate_percent_ = 0.0;
	std::map<StreamId, StreamInput> streams_;
	std::vector<Output> outputs_;
};

}
