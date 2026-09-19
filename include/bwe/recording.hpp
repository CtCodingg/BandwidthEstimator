/// @file
/// @brief Recording of measurements, estimates and allocations as CSV.
///
/// Format: the first line is "# bwe-recording v1", the second line holds
/// the column names, then one line per record. The column `record` names
/// the record type (sender, receiver, estimate, allocation, truth). Empty
/// cells mean "not set". Times and durations are integers in microseconds,
/// rates are in bit/s. Numbers are written independently of the global
/// locale and read back exactly.

#pragma once

#include <iosfwd>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "bwe/bandwidth_estimate.hpp"
#include "bwe/export.hpp"
#include "bwe/measurement.hpp"
#include "bwe/shared_link_allocator.hpp"

namespace bwe
{

/// @brief Estimate of one algorithm.
struct EstimateRecord
{
	/// @brief Name of the algorithm.
	std::string algorithm;
	/// @brief The estimate.
	BandwidthEstimate estimate;
};

/// @brief True channel capacity; only known in simulated data.
struct TrueCapacityRecord
{
	/// @brief Capacity in bit/s.
	double capacity_bps = 0.0;
};

/// @brief One entry of a recording.
struct Record
{
	/// @brief Application time of the entry.
	Duration time{0};
	/// @brief Flow the entry belongs to; unset for channel-wide entries.
	std::optional<FlowId> flow;
	/// @brief Content of the entry.
	std::variant<SenderMeasurement, ReceiverMeasurement, EstimateRecord,
				 FlowAllocation, TrueCapacityRecord>
		data;
};

/// @brief Writes records as CSV to a stream.
/// @note Thread-safe: records written concurrently never interleave.
///       Writes to the same stream from outside the writer are not
///       synchronized. Throws only std::bad_alloc or what the stream
///       throws.
class BWE_API RecordingWriter
{
public:
	/// @brief Writes the format line and the column names.
	/// @param out Target stream; must outlive the writer.
	explicit RecordingWriter(std::ostream& out);

	/// @brief Writes sender statistics of a flow.
	void WriteSender(Duration time, FlowId flow,
					 const SenderMeasurement& measurement);

	/// @brief Writes receiver statistics of a flow.
	void WriteReceiver(Duration time, FlowId flow,
					   const ReceiverMeasurement& measurement);

	/// @brief Writes an estimate; `flow` unset for a channel-wide estimate.
	void WriteEstimate(Duration time, std::optional<FlowId> flow,
					   std::string_view algorithm,
					   const BandwidthEstimate& estimate);

	/// @brief Writes the target rate of a flow.
	void WriteAllocation(Duration time, const FlowAllocation& allocation);

	/// @brief Writes the true channel capacity.
	void WriteTrueCapacity(Duration time, double capacity_bps);

	/// @brief Writes any record.
	void Write(const Record& record);

private:
	std::mutex mutex_;
	std::ostream* out_;
};

/// @brief Reads a recording written by RecordingWriter.
///
/// Columns may be missing (treated as empty) or in any order.
/// @throws std::invalid_argument Unsupported format, unknown or duplicate
///         column, or malformed line; the message names the line number.
BWE_API std::vector<Record> ReadRecording(std::istream& in);

}  // namespace bwe
