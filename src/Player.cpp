#include "bwe/Player.hpp"

#include "bwe/Recorder.hpp"

#include <cstddef>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace bwe
{

namespace
{

constexpr size_t kFieldCount = 9;

std::runtime_error LineError(const std::string& reason, size_t line_number)
{
	return std::runtime_error("bwe::Player: " + reason + " in line " + std::to_string(line_number));
}

void RemoveCarriageReturn(std::string& line)
{
	if (!line.empty() && line.back() == '\r')
	{
		line.pop_back();
	}
}

std::vector<std::string> Split(const std::string& line)
{
	std::vector<std::string> fields;
	std::istringstream stream(line);
	std::string field;
	while (std::getline(stream, field, ','))
	{
		fields.push_back(field);
	}
	return fields;
}

template <typename T>
T Parse(const std::string& text, size_t line_number)
{
	std::istringstream stream(text);
	stream.imbue(std::locale::classic());
	T value = T();
	stream >> value;
	if (stream.fail() || !stream.eof())
	{
		throw LineError("invalid value '" + text + "'", line_number);
	}
	return value;
}

// MSVC's operator<< writes "inf" for infinity, but its operator>> cannot parse it back;
// handled explicitly instead of relying on locale-dependent istream parsing.
double ParseDouble(const std::string& text, size_t line_number)
{
	if (text == "inf" || text == "infinity" || text == "Infinity")
	{
		return std::numeric_limits<double>::infinity();
	}
	if (text == "-inf" || text == "-infinity" || text == "-Infinity")
	{
		return -std::numeric_limits<double>::infinity();
	}
	return Parse<double>(text, line_number);
}

EventKind ParseKind(const std::string& text, size_t line_number)
{
	if (text == "channel")
	{
		return EventKind::kChannel;
	}
	if (text == "stream")
	{
		return EventKind::kStream;
	}
	if (text == "remove")
	{
		return EventKind::kRemove;
	}
	if (text == "output")
	{
		return EventKind::kOutput;
	}
	throw LineError("unknown kind '" + text + "'", line_number);
}

}

Player::Player(std::istream& in)
{
	std::string line;
	size_t line_number = 1;
	if (!std::getline(in, line))
	{
		throw std::runtime_error("bwe::Player: header is missing");
	}
	RemoveCarriageReturn(line);
	if (line != Recorder::kHeader)
	{
		throw LineError("invalid header", line_number);
	}

	while (std::getline(in, line))
	{
		++line_number;
		RemoveCarriageReturn(line);
		if (line.empty())
		{
			continue;
		}

		const std::vector<std::string> fields = Split(line);
		if (fields.size() != kFieldCount)
		{
			throw LineError("wrong number of values", line_number);
		}

		RecordedEvent event;
		event.step = Parse<uint64_t>(fields[0], line_number);
		event.kind = ParseKind(fields[1], line_number);
		event.stream.stream_id = Parse<StreamId>(fields[2], line_number);
		event.rtt_ms = ParseDouble(fields[3], line_number);
		event.drop_rate_percent = ParseDouble(fields[4], line_number);
		event.stream.receive_rate_bps = ParseDouble(fields[5], line_number);
		event.stream.weight = ParseDouble(fields[6], line_number);
		event.stream.max_rate_bps = ParseDouble(fields[7], line_number);
		event.estimated_rate_bps = ParseDouble(fields[8], line_number);
		events_.push_back(event);
	}
}

const std::vector<RecordedEvent>& Player::Events() const
{
	return events_;
}

void Player::Replay(Estimator& estimator) const
{
	for (const RecordedEvent& event : events_)
	{
		switch (event.kind)
		{
		case EventKind::kChannel:
			estimator.UpdateChannel(event.rtt_ms, event.drop_rate_percent);
			break;
		case EventKind::kStream:
			estimator.UpdateStream(event.stream);
			break;
		case EventKind::kRemove:
			estimator.RemoveStream(event.stream.stream_id);
			break;
		case EventKind::kOutput:
			break; // informational only, not an Estimator input
		}
	}
}

}
