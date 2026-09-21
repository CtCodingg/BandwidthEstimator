#pragma once

#include "bwe/Estimator.hpp"
#include "bwe/Types.hpp"

#include <cstdint>
#include <istream>
#include <vector>

namespace bwe
{

/// One line of a recording.
struct Record
{
	std::uint64_t sequence = 0; ///< Sequence number written by the Recorder.
	Input input; ///< Recorded input.
	Output output; ///< Recorded result.
};

/// Reads a recording and replays its inputs into an estimator.
class Player
{
public:
	/// Reads all lines from @p in. Empty lines are skipped.
	/// @throws std::runtime_error if the header is missing or a line is invalid.
	explicit Player(std::istream& in);

	/// @return All recorded lines in file order.
	const std::vector<Record>& records() const;

	/// Feeds all recorded inputs into @p estimator in file order.
	/// The estimator notifies its observer and callbacks as in live operation.
	/// @return The new results, one per record.
	/// @throws std::invalid_argument if a recorded input is out of range.
	std::vector<Output> replay(Estimator& estimator) const;

private:
	std::vector<Record> m_records;
};

}
