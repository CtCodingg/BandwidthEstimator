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
	Channel, ///< Estimator::updateChannel(rttMs, dropRatePercent).
	Stream, ///< Estimator::updateStream(stream).
	Remove ///< Estimator::removeStream(stream.streamId).
};

/// One recorded call to the Estimator.
struct RecordedEvent
{
	std::uint64_t step = 0; ///< Step number written by the Recorder.
	EventKind kind = EventKind::Channel;
	double rttMs = 0.0; ///< Valid for Kind::Channel.
	double dropRatePercent = 0.0; ///< Valid for Kind::Channel.
	StreamInput stream; ///< Valid for Kind::Stream (all fields) and Kind::Remove (streamId only).
};

/// Reads a recording and replays its events into an estimator.
class Player
{
public:
	/// Reads all lines from @p in. Empty lines are skipped.
	/// @throws std::runtime_error if the header is missing or a line is invalid.
	explicit Player(std::istream& in);

	/// @return All recorded events in file order.
	const std::vector<RecordedEvent>& events() const;

	/// Feeds every recorded event into @p estimator, in file order.
	/// Since Estimator recalculates on its own background thread, this does not wait for or
	/// return results; poll estimator.outputs() afterwards for the current state.
	/// @throws std::invalid_argument if a recorded value is out of range.
	void replay(Estimator& estimator) const;

private:
	std::vector<RecordedEvent> m_events;
};

}
