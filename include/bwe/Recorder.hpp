#pragma once

#include "bwe/Types.hpp"

#include <cstdint>
#include <mutex>
#include <ostream>

namespace bwe
{

/// Writes the events accepted by Estimator (UpdateChannel/UpdateStream/RemoveStream) as CSV,
/// one line per event, in call order. Thread-safe.
/// Values use '.' as decimal separator and full precision, so a replay feeds identical values.
class Recorder
{
public:
	/// First line of every recording.
	static constexpr const char* kHeader = "step,kind,streamId,rttMs,dropRatePercent,receiveRateBps,weight,maxRateBps";

	/// Writes the header to @p out. @p out must outlive the recorder.
	/// @throws std::runtime_error if writing fails.
	explicit Recorder(std::ostream& out);

	/// Records a call to Estimator::UpdateChannel(). The step number starts at 1.
	/// @throws std::runtime_error if writing fails.
	void RecordChannel(double rtt_ms, double drop_rate_percent);

	/// Records a call to Estimator::UpdateStream().
	/// @throws std::runtime_error if writing fails.
	void RecordStream(const StreamInput& stream);

	/// Records a call to Estimator::RemoveStream().
	/// @throws std::runtime_error if writing fails.
	void RecordRemove(StreamId stream_id);

private:
	void WriteLine(const char* kind, StreamId stream_id, double rtt_ms, double drop_rate_percent,
		double receive_rate_bps, double weight, double max_rate_bps);

	std::mutex mutex_;
	std::ostream& out_;
	uint64_t step_ = 0;
};

}
