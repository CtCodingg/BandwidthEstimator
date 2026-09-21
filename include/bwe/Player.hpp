#pragma once

#include "bwe/Estimator.hpp"
#include "bwe/Types.hpp"

#include <cstdint>
#include <istream>
#include <vector>

namespace bwe
{

/// Kind of one recorded event, mirrors the Estimator method that produced it.
enum class EventKind
{
	kChannel, ///< Estimator::UpdateChannel(rtt_ms, drop_rate_percent).
	kStream, ///< Estimator::UpdateStream(stream).
	kRemove ///< Estimator::RemoveStream(stream.stream_id).
};

/// One recorded call to the Estimator.
struct RecordedEvent
{
	uint64_t step = 0; ///< Step number written by the Recorder.
	EventKind kind = EventKind::kChannel;
	double rtt_ms = 0.0; ///< Valid for EventKind::kChannel.
	double drop_rate_percent = 0.0; ///< Valid for EventKind::kChannel.
	StreamInput stream; ///< Valid for EventKind::kStream (all fields) and EventKind::kRemove (stream_id only).
};

/// Reads a recording and replays its events into an estimator.
class Player
{
public:
	/// Reads all lines from @p in. Empty lines are skipped.
	/// @throws std::runtime_error if the header is missing or a line is invalid.
	explicit Player(std::istream& in);

	/// @return All recorded events in file order.
	const std::vector<RecordedEvent>& Events() const;

	/// Feeds every recorded event into @p estimator, in file order.
	/// Since Estimator recalculates on its own background thread, this does not wait for or
	/// return results; poll estimator.Outputs() afterwards for the current state.
	/// @throws std::invalid_argument if a recorded value is out of range.
	void Replay(Estimator& estimator) const;

private:
	std::vector<RecordedEvent> events_;
};

}
