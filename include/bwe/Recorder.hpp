#pragma once

#include "bwe/Types.hpp"

#include <cstdint>
#include <mutex>
#include <ostream>

namespace bwe
{

/// Writes inputs and results as CSV, one line per estimate. Thread-safe.
/// Values use '.' as decimal separator and full precision, so a replay gives identical results.
class Recorder
{
public:
	/// First line of every recording.
	static constexpr const char* Header = "sequence,streamId,rttMs,dropRatePercent,receiveRateBps,rateBps";

	/// Writes the header to @p out. @p out must outlive the recorder.
	/// @throws std::runtime_error if writing fails.
	explicit Recorder(std::ostream& out);

	/// Writes one line; the sequence number starts at 1.
	/// @throws std::runtime_error if writing fails.
	void record(const Input& input, const Output& output);

private:
	std::mutex m_mutex;
	std::ostream& m_out;
	std::uint64_t m_sequence = 0;
};

}
