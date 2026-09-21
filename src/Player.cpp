#include "bwe/Player.hpp"

#include "bwe/Recorder.hpp"

#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace bwe
{

namespace
{

constexpr std::size_t FieldCount = 7;

std::runtime_error lineError(const std::string& reason, std::size_t lineNumber)
{
	return std::runtime_error("bwe::Player: " + reason + " in line " + std::to_string(lineNumber));
}

void removeCarriageReturn(std::string& line)
{
	if (!line.empty() && line.back() == '\r')
	{
		line.pop_back();
	}
}

std::vector<std::string> split(const std::string& line)
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
T parse(const std::string& text, std::size_t lineNumber)
{
	std::istringstream stream(text);
	stream.imbue(std::locale::classic());
	T value = T();
	stream >> value;
	if (stream.fail() || !stream.eof())
	{
		throw lineError("invalid value '" + text + "'", lineNumber);
	}
	return value;
}

EventKind parseKind(const std::string& text, std::size_t lineNumber)
{
	if (text == "channel")
	{
		return EventKind::Channel;
	}
	if (text == "stream")
	{
		return EventKind::Stream;
	}
	if (text == "remove")
	{
		return EventKind::Remove;
	}
	throw lineError("unknown kind '" + text + "'", lineNumber);
}

}

Player::Player(std::istream& in)
{
	std::string line;
	std::size_t lineNumber = 1;
	if (!std::getline(in, line))
	{
		throw std::runtime_error("bwe::Player: header is missing");
	}
	removeCarriageReturn(line);
	if (line != Recorder::Header)
	{
		throw lineError("invalid header", lineNumber);
	}

	while (std::getline(in, line))
	{
		++lineNumber;
		removeCarriageReturn(line);
		if (line.empty())
		{
			continue;
		}

		const std::vector<std::string> fields = split(line);
		if (fields.size() != FieldCount)
		{
			throw lineError("wrong number of values", lineNumber);
		}

		RecordedEvent event;
		event.step = parse<std::uint64_t>(fields[0], lineNumber);
		event.kind = parseKind(fields[1], lineNumber);
		event.stream.streamId = parse<StreamId>(fields[2], lineNumber);
		event.rttMs = parse<double>(fields[3], lineNumber);
		event.dropRatePercent = parse<double>(fields[4], lineNumber);
		event.stream.receiveRateBps = parse<double>(fields[5], lineNumber);
		event.stream.weight = parse<double>(fields[6], lineNumber);
		m_events.push_back(event);
	}
}

const std::vector<RecordedEvent>& Player::events() const
{
	return m_events;
}

void Player::replay(Estimator& estimator) const
{
	for (const RecordedEvent& event : m_events)
	{
		switch (event.kind)
		{
		case EventKind::Channel:
			estimator.updateChannel(event.rttMs, event.dropRatePercent);
			break;
		case EventKind::Stream:
			estimator.updateStream(event.stream);
			break;
		case EventKind::Remove:
			estimator.removeStream(event.stream.streamId);
			break;
		}
	}
}

}
