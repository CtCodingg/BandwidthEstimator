#pragma once

#include "bwe/IAlgorithm.hpp"
#include "bwe/Types.hpp"

#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace bwe
{

/// Estimates the rate of N streams at the receiver and passes each result to the sender of the stream.
/// All methods are thread-safe. Callbacks are called outside the internal lock,
/// exceptions thrown by callbacks are passed to the caller of update().
class Estimator
{
public:
	/// Receives the result for one stream, typically forwards it to the sender.
	using Callback = std::function<void(const Output&)>;

	/// Receives every input together with its result, e.g. for recording.
	using Observer = std::function<void(const Input&, const Output&)>;

	/// Creates the estimator with the algorithm selected in @p config.
	/// @throws std::invalid_argument if @p config is invalid.
	explicit Estimator(const Config& config);

	/// Creates the estimator with a custom algorithm.
	/// @throws std::invalid_argument if @p algorithm is null.
	explicit Estimator(std::unique_ptr<IAlgorithm> algorithm);

	/// Registers the callback for stream @p streamId, replaces an existing one.
	/// @throws std::invalid_argument if @p callback is empty.
	void subscribe(StreamId streamId, Callback callback);

	/// Removes the callback for stream @p streamId, does nothing if none is registered.
	void unsubscribe(StreamId streamId);

	/// Sets the observer, an empty function removes it.
	void setObserver(Observer observer);

	/// Calculates a new estimate for one stream, then notifies the observer and the callback of the stream.
	/// @return The new estimate.
	/// @throws std::invalid_argument if a value of @p input is out of range.
	Output update(const Input& input);

private:
	std::mutex m_mutex;
	std::unique_ptr<IAlgorithm> m_algorithm;
	std::map<StreamId, Callback> m_callbacks;
	Observer m_observer;
};

}
