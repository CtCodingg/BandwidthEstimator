#pragma once

#include "bwe/Types.hpp"

#include <cstdint>
#include <mutex>
#include <ostream>

namespace bwe
{

/// Writes the events accepted by Estimator (updateChannel/updateStream/removeStream) as CSV,
/// one line per event, in call order. Thread-safe.
/// Values use '.' as decimal separator and full precision, so a replay feeds identical values.
class Recorder
{
public:
	/// First line of every recording.
	static constexpr const char* Header = "step,kind,streamId,rttMs,dropRatePercent,receiveRateBps,weight";

	/// Writes the header to @p out. @p out must outlive the recorder.
	/// @throws std::runtime_error if writing fails.
	explicit Recorder(std::ostream& out);

	/// Records a call to Estimator::updateChannel(). The step number starts at 1.
	/// @throws std::runtime_error if writing fails.
	void recordChannel(double rttMs, double dropRatePercent);

	/// Records a call to Estimator::updateStream().
	/// @throws std::runtime_error if writing fails.
	void recordStream(const StreamInput& stream);

	/// Records a call to Estimator::removeStream().
	/// @throws std::runtime_error if writing fails.
	void recordRemove(StreamId streamId);

private:
	void writeLine(const char* kind, StreamId streamId, double rttMs, double dropRatePercent,
		double receiveRateBps, double weight);

	std::mutex m_mutex;
	std::ostream& m_out;
	std::uint64_t m_step = 0;
};

}
