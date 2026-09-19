#include "bwe/recording.hpp"

#include <charconv>
#include <cmath>
#include <functional>
#include <istream>
#include <limits>
#include <locale>
#include <map>
#include <mutex>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace bwe
{
namespace
{

constexpr std::string_view kFormatLine = "# bwe-recording v1";

// All columns in the order they are written.
constexpr std::string_view kColumns[] = {
	"record",
	"time_us",
	"flow",
	"timestamp_us",
	"rtt_us",
	"link_capacity_bps",
	"mss_bytes",
	"packets_sent",
	"packets_sent_unique",
	"packets_received",
	"packets_received_unique",
	"packets_lost",
	"packets_retransmitted",
	"packets_dropped",
	"packets_belated",
	"bytes_sent",
	"bytes_sent_unique",
	"bytes_received",
	"bytes_received_unique",
	"bytes_lost",
	"bytes_retransmitted",
	"bytes_dropped",
	"send_buffer_delay_us",
	"send_buffer_bytes",
	"send_buffer_packets",
	"flight_size_packets",
	"congestion_window_packets",
	"max_bandwidth_bps",
	"receive_buffer_delay_us",
	"receive_buffer_bytes",
	"receive_buffer_packets",
	"tsbpd_delay_us",
	"reorder_distance_packets",
	"algorithm",
	"bits_per_second",
	"confidence",
	"valid",
	"target_bps",
	"capacity_bps",
};
constexpr std::size_t kColumnCount = sizeof(kColumns) / sizeof(kColumns[0]);

std::size_t ColumnIndex(std::string_view name)
{
	for (std::size_t i = 0; i < kColumnCount; ++i)
	{
		if (kColumns[i] == name)
		{
			return i;
		}
	}
	return kColumnCount;
}

// ---------------------------------------------------------------------------
// Formatting and parsing, independent of the global locale.
// ---------------------------------------------------------------------------

// Thrown by the parse helpers; ReadRecording() adds the line number.
struct ParseError : std::runtime_error
{
	using std::runtime_error::runtime_error;
};

std::string FormatDouble(double value)
{
	if (std::isnan(value))
	{
		return "nan";
	}
	if (std::isinf(value))
	{
		return value > 0 ? "inf" : "-inf";
	}
	// Shortest representation that reads back exactly.
	for (int precision = 15; precision <= 17; ++precision)
	{
		std::ostringstream stream;
		stream.imbue(std::locale::classic());
		stream.precision(precision);
		stream << value;
		std::istringstream check(stream.str());
		check.imbue(std::locale::classic());
		double parsed = 0.0;
		check >> parsed;
		if (parsed == value || precision == 17)
		{
			return stream.str();
		}
	}
	return {};
}

template <typename T>
T ParseInteger(std::string_view text)
{
	T value{};
	const char* end = text.data() + text.size();
	const auto result = std::from_chars(text.data(), end, value);
	if (text.empty() || result.ec != std::errc() || result.ptr != end)
	{
		throw ParseError("invalid integer '" + std::string(text) + "'");
	}
	return value;
}

double ParseDouble(std::string_view text)
{
	if (text == "nan")
	{
		return std::numeric_limits<double>::quiet_NaN();
	}
	if (text == "inf")
	{
		return std::numeric_limits<double>::infinity();
	}
	if (text == "-inf")
	{
		return -std::numeric_limits<double>::infinity();
	}
	std::istringstream stream{std::string(text)};
	stream.imbue(std::locale::classic());
	double value = 0.0;
	stream >> value;
	if (text.empty() || stream.fail() || !stream.eof())
	{
		throw ParseError("invalid number '" + std::string(text) + "'");
	}
	return value;
}

Duration ParseDuration(std::string_view text)
{
	return Duration(ParseInteger<Duration::rep>(text));
}

std::string FormatDuration(Duration value)
{
	return std::to_string(value.count());
}

// ---------------------------------------------------------------------------
// Bindings between measurement fields and columns.
// ---------------------------------------------------------------------------

template <typename M>
struct Binding
{
	std::size_t column;
	std::function<void(const M&, std::string&)> format;
	std::function<void(M&, std::string_view)> parse;
};

template <typename M, typename T, typename Access>
Binding<M> Integer(std::string_view column, Access access)
{
	return {ColumnIndex(column),
			[access](const M& m, std::string& cell)
	{
				if (const auto& value = access(m))
		{
					cell = std::to_string(*value);
				}
			},
			[access](M& m, std::string_view text)
	{
				access(m) = ParseInteger<T>(text);
			}
	};
}

template <typename M, typename Access>
Binding<M> Real(std::string_view column, Access access)
{
	return {ColumnIndex(column),
			[access](const M& m, std::string& cell)
	{
				if (const auto& value = access(m))
		{
					cell = FormatDouble(*value);
				}
			},
			[access](M& m, std::string_view text)
	{
				access(m) = ParseDouble(text);
			}
	};
}

template <typename M, typename Access>
Binding<M> Time(std::string_view column, Access access)
{
	return {ColumnIndex(column),
			[access](const M& m, std::string& cell)
	{
				if (const auto& value = access(m))
		{
					cell = FormatDuration(*value);
				}
			},
			[access](M& m, std::string_view text)
	{
				access(m) = ParseDuration(text);
			}
	};
}

// Accessors are generic lambdas so they work for const and non-const.
#define BWE_FIELD(expression) \
	[](auto& m) -> auto& \
	{ \
		return m.expression; \
	}

template <typename M>
std::vector<Binding<M>> CommonBindings()
{
	return {
		Time<M>("rtt_us", BWE_FIELD(common.rtt)),
		Real<M>("link_capacity_bps", BWE_FIELD(common.link_capacity_bps)),
		Integer<M, std::uint32_t>("mss_bytes", BWE_FIELD(common.mss_bytes)),
	};
}

const std::vector<Binding<SenderMeasurement>>& SenderBindings()
{
	using M = SenderMeasurement;
	static const auto* bindings = []
	{
		auto* result = new std::vector<Binding<M>>(CommonBindings<M>());
		const std::vector<Binding<M>> fields = {
			Integer<M, std::uint64_t>("packets_sent", BWE_FIELD(packets_sent)),
			Integer<M, std::uint64_t>("packets_sent_unique",
									  BWE_FIELD(packets_sent_unique)),
			Integer<M, std::uint64_t>("packets_lost", BWE_FIELD(packets_lost)),
			Integer<M, std::uint64_t>("packets_retransmitted",
									  BWE_FIELD(packets_retransmitted)),
			Integer<M, std::uint64_t>("packets_dropped",
									  BWE_FIELD(packets_dropped)),
			Integer<M, std::uint64_t>("bytes_sent", BWE_FIELD(bytes_sent)),
			Integer<M, std::uint64_t>("bytes_sent_unique",
									  BWE_FIELD(bytes_sent_unique)),
			Integer<M, std::uint64_t>("bytes_retransmitted",
									  BWE_FIELD(bytes_retransmitted)),
			Integer<M, std::uint64_t>("bytes_dropped", BWE_FIELD(bytes_dropped)),
			Time<M>("send_buffer_delay_us", BWE_FIELD(send_buffer_delay)),
			Integer<M, std::uint64_t>("send_buffer_bytes",
									  BWE_FIELD(send_buffer_bytes)),
			Integer<M, std::uint32_t>("send_buffer_packets",
									  BWE_FIELD(send_buffer_packets)),
			Integer<M, std::uint32_t>("flight_size_packets",
									  BWE_FIELD(flight_size_packets)),
			Integer<M, std::uint32_t>("congestion_window_packets",
									  BWE_FIELD(congestion_window_packets)),
			Real<M>("max_bandwidth_bps", BWE_FIELD(max_bandwidth_bps)),
		};
		result->insert(result->end(), fields.begin(), fields.end());
		return result;
	}();
	return *bindings;
}

const std::vector<Binding<ReceiverMeasurement>>& ReceiverBindings()
{
	using M = ReceiverMeasurement;
	static const auto* bindings = []
	{
		auto* result = new std::vector<Binding<M>>(CommonBindings<M>());
		const std::vector<Binding<M>> fields = {
			Integer<M, std::uint64_t>("packets_received",
									  BWE_FIELD(packets_received)),
			Integer<M, std::uint64_t>("packets_received_unique",
									  BWE_FIELD(packets_received_unique)),
			Integer<M, std::uint64_t>("packets_lost", BWE_FIELD(packets_lost)),
			Integer<M, std::uint64_t>("packets_dropped",
									  BWE_FIELD(packets_dropped)),
			Integer<M, std::uint64_t>("packets_belated",
									  BWE_FIELD(packets_belated)),
			Integer<M, std::uint64_t>("bytes_received",
									  BWE_FIELD(bytes_received)),
			Integer<M, std::uint64_t>("bytes_received_unique",
									  BWE_FIELD(bytes_received_unique)),
			Integer<M, std::uint64_t>("bytes_lost", BWE_FIELD(bytes_lost)),
			Integer<M, std::uint64_t>("bytes_dropped", BWE_FIELD(bytes_dropped)),
			Time<M>("receive_buffer_delay_us", BWE_FIELD(receive_buffer_delay)),
			Integer<M, std::uint64_t>("receive_buffer_bytes",
									  BWE_FIELD(receive_buffer_bytes)),
			Integer<M, std::uint32_t>("receive_buffer_packets",
									  BWE_FIELD(receive_buffer_packets)),
			Time<M>("tsbpd_delay_us", BWE_FIELD(tsbpd_delay)),
			Integer<M, std::uint32_t>("reorder_distance_packets",
									  BWE_FIELD(reorder_distance_packets)),
		};
		result->insert(result->end(), fields.begin(), fields.end());
		return result;
	}();
	return *bindings;
}

#undef BWE_FIELD

// ---------------------------------------------------------------------------
// CSV cells.
// ---------------------------------------------------------------------------

void AppendCell(std::string& line, const std::string& cell)
{
	if (cell.find_first_of(",\"\r\n") == std::string::npos)
	{
		line += cell;
		return;
	}
	line += '"';
	for (char c : cell)
	{
		if (c == '"')
		{
			line += '"';
		}
		line += c;
	}
	line += '"';
}

std::vector<std::string> SplitCells(std::string_view line)
{
	std::vector<std::string> cells(1);
	bool quoted = false;
	for (std::size_t i = 0; i < line.size(); ++i)
	{
		const char c = line[i];
		if (quoted)
		{
			if (c == '"' && i + 1 < line.size() && line[i + 1] == '"')
			{
				cells.back() += '"';
				++i;
			}
			else if (c == '"')
			{
				quoted = false;
			}
			else
			{
				cells.back() += c;
			}
		}
		else if (c == '"')
		{
			quoted = true;
		}
		else if (c == ',')
		{
			cells.emplace_back();
		}
		else
		{
			cells.back() += c;
		}
	}
	if (quoted)
	{
		throw ParseError("unterminated quote");
	}
	return cells;
}

template <typename M>
void FormatMeasurement(const M& measurement,
					   const std::vector<Binding<M>>& bindings,
					   std::vector<std::string>& cells)
{
	cells[ColumnIndex("timestamp_us")] =
		FormatDuration(measurement.common.timestamp);
	for (const Binding<M>& binding : bindings)
	{
		binding.format(measurement, cells[binding.column]);
	}
}

}  // namespace

// ---------------------------------------------------------------------------
// Writer.
// ---------------------------------------------------------------------------

RecordingWriter::RecordingWriter(std::ostream& out) : out_(&out)
{
	std::string header;
	for (std::size_t i = 0; i < kColumnCount; ++i)
	{
		if (i > 0)
		{
			header += ',';
		}
		header += kColumns[i];
	}
	*out_ << kFormatLine << '\n' << header << '\n';
}

void RecordingWriter::WriteSender(Duration time, FlowId flow,
								  const SenderMeasurement& measurement)
{
	Write(Record{time, flow, measurement});
}

void RecordingWriter::WriteReceiver(Duration time, FlowId flow,
									const ReceiverMeasurement& measurement)
{
	Write(Record{time, flow, measurement});
}

void RecordingWriter::WriteEstimate(Duration time,
									std::optional<FlowId> flow,
									std::string_view algorithm,
									const BandwidthEstimate& estimate)
{
	Write(Record{time, flow, EstimateRecord{std::string(algorithm), estimate}});
}

void RecordingWriter::WriteAllocation(Duration time,
									  const FlowAllocation& allocation)
{
	Write(Record{time, allocation.flow, allocation});
}

void RecordingWriter::WriteTrueCapacity(Duration time, double capacity_bps)
{
	Write(Record{time, std::nullopt, TrueCapacityRecord{capacity_bps}});
}

void RecordingWriter::Write(const Record& record)
{
	std::vector<std::string> cells(kColumnCount);
	cells[ColumnIndex("time_us")] = FormatDuration(record.time);
	if (record.flow)
	{
		cells[ColumnIndex("flow")] = std::to_string(*record.flow);
	}

	std::string& type = cells[ColumnIndex("record")];
	if (const auto* sender = std::get_if<SenderMeasurement>(&record.data))
	{
		type = "sender";
		FormatMeasurement(*sender, SenderBindings(), cells);
	}
	else if (const auto* receiver =
				   std::get_if<ReceiverMeasurement>(&record.data))
	{
		type = "receiver";
		FormatMeasurement(*receiver, ReceiverBindings(), cells);
	}
	else if (const auto* estimate =
				   std::get_if<EstimateRecord>(&record.data))
	{
		type = "estimate";
		cells[ColumnIndex("algorithm")] = estimate->algorithm;
		cells[ColumnIndex("bits_per_second")] =
			FormatDouble(estimate->estimate.bits_per_second);
		cells[ColumnIndex("confidence")] =
			FormatDouble(estimate->estimate.confidence);
		cells[ColumnIndex("timestamp_us")] =
			FormatDuration(estimate->estimate.timestamp);
		cells[ColumnIndex("valid")] = estimate->estimate.valid ? "1" : "0";
	}
	else if (const auto* allocation =
				   std::get_if<FlowAllocation>(&record.data))
	{
		type = "allocation";
		cells[ColumnIndex("target_bps")] = FormatDouble(allocation->target_bps);
	}
	else if (const auto* truth =
				   std::get_if<TrueCapacityRecord>(&record.data))
	{
		type = "truth";
		cells[ColumnIndex("capacity_bps")] = FormatDouble(truth->capacity_bps);
	}

	std::string line;
	for (std::size_t i = 0; i < kColumnCount; ++i)
	{
		if (i > 0)
		{
			line += ',';
		}
		AppendCell(line, cells[i]);
	}
	line += '\n';

	// The line is complete before locking, so the lock is held briefly.
	std::lock_guard<std::mutex> lock(mutex_);
	*out_ << line;
}

// ---------------------------------------------------------------------------
// Reader.
// ---------------------------------------------------------------------------

namespace
{

// Cells of one line, addressed by the writer's column index.
class Row
{
public:
	Row(const std::vector<std::size_t>& positions,
		std::vector<std::string> cells)
		: positions_(&positions), cells_(std::move(cells))
	{
	}

	std::string_view Cell(std::string_view column) const
	{
		const std::size_t position = (*positions_)[ColumnIndex(column)];
		return position < cells_.size() ? std::string_view(cells_[position])
										: std::string_view();
	}

	std::string_view Required(std::string_view column) const
	{
		const std::string_view cell = Cell(column);
		if (cell.empty())
		{
			throw ParseError("missing value in column '" + std::string(column) +
							 "'");
		}
		return cell;
	}

private:
	const std::vector<std::size_t>* positions_;
	std::vector<std::string> cells_;
};

template <typename M>
M ParseMeasurement(const Row& row, const std::vector<Binding<M>>& bindings)
{
	M measurement;
	measurement.common.timestamp =
		ParseDuration(row.Required("timestamp_us"));
	for (const Binding<M>& binding : bindings)
	{
		const std::string_view cell = row.Cell(kColumns[binding.column]);
		if (!cell.empty())
		{
			binding.parse(measurement, cell);
		}
	}
	return measurement;
}

Record ParseRecord(const Row& row)
{
	Record record;
	record.time = ParseDuration(row.Required("time_us"));
	if (!row.Cell("flow").empty())
	{
		record.flow = ParseInteger<FlowId>(row.Cell("flow"));
	}

	const std::string_view type = row.Required("record");
	if (type == "sender")
	{
		record.data = ParseMeasurement(row, SenderBindings());
	}
	else if (type == "receiver")
	{
		record.data = ParseMeasurement(row, ReceiverBindings());
	}
	else if (type == "estimate")
	{
		EstimateRecord estimate;
		estimate.algorithm = std::string(row.Required("algorithm"));
		estimate.estimate.bits_per_second =
			ParseDouble(row.Required("bits_per_second"));
		estimate.estimate.confidence = ParseDouble(row.Required("confidence"));
		if (!row.Cell("timestamp_us").empty())
		{
			estimate.estimate.timestamp = ParseDuration(row.Cell("timestamp_us"));
		}
		const std::string_view valid = row.Required("valid");
		if (valid != "0" && valid != "1")
		{
			throw ParseError("invalid value '" + std::string(valid) +
							 "' in column 'valid'");
		}
		estimate.estimate.valid = valid == "1";
		record.data = estimate;
	}
	else if (type == "allocation")
	{
		if (!record.flow)
		{
			throw ParseError("missing value in column 'flow'");
		}
		record.data = FlowAllocation{*record.flow,
									 ParseDouble(row.Required("target_bps"))};
	}
	else if (type == "truth")
	{
		record.data = TrueCapacityRecord{ParseDouble(row.Required("capacity_bps"))};
	}
	else
	{
		throw ParseError("unknown record type '" + std::string(type) + "'");
	}
	return record;
}

std::string_view WithoutCarriageReturn(const std::string& line)
{
	std::string_view view(line);
	if (!view.empty() && view.back() == '\r')
	{
		view.remove_suffix(1);
	}
	return view;
}

}  // namespace

std::vector<Record> ReadRecording(std::istream& in)
{
	std::string line;
	std::size_t line_number = 0;
	const auto fail = [&line_number](const std::string& message)
	{
		throw std::invalid_argument("bwe: recording line " +
									std::to_string(line_number) + ": " + message);
	};

	++line_number;
	if (!std::getline(in, line) || WithoutCarriageReturn(line) != kFormatLine)
	{
		fail("expected '" + std::string(kFormatLine) + "'");
	}

	// Map the writer's column index to the position in this file.
	++line_number;
	if (!std::getline(in, line))
	{
		fail("missing column names");
	}
	std::vector<std::size_t> positions(kColumnCount + 1,
									   std::numeric_limits<std::size_t>::max());
	std::size_t column_count = 0;
	try
	{
		const std::vector<std::string> names =
			SplitCells(WithoutCarriageReturn(line));
		column_count = names.size();
		for (std::size_t position = 0; position < names.size(); ++position)
		{
			const std::size_t index = ColumnIndex(names[position]);
			if (index == kColumnCount)
			{
				fail("unknown column '" + names[position] + "'");
			}
			if (positions[index] != std::numeric_limits<std::size_t>::max())
			{
				fail("duplicate column '" + names[position] + "'");
			}
			positions[index] = position;
		}
	}
	catch (const ParseError& error)
	{
		fail(error.what());
	}

	std::vector<Record> records;
	while (std::getline(in, line))
	{
		++line_number;
		const std::string_view content = WithoutCarriageReturn(line);
		if (content.empty())
		{
			continue;
		}
		try
		{
			std::vector<std::string> cells = SplitCells(content);
			if (cells.size() != column_count)
			{
				throw ParseError("expected " + std::to_string(column_count) +
								 " cells, found " + std::to_string(cells.size()));
			}
			records.push_back(ParseRecord(Row(positions, std::move(cells))));
		}
		catch (const ParseError& error)
		{
			fail(error.what());
		}
	}
	return records;
}

}  // namespace bwe
