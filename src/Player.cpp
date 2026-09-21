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

constexpr std::size_t FieldCount = 6;

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

		Record record;
		record.sequence = parse<std::uint64_t>(fields[0], lineNumber);
		record.input.streamId = parse<StreamId>(fields[1], lineNumber);
		record.input.rttMs = parse<double>(fields[2], lineNumber);
		record.input.dropRatePercent = parse<double>(fields[3], lineNumber);
		record.input.receiveRateBps = parse<double>(fields[4], lineNumber);
		record.output.streamId = record.input.streamId;
		record.output.rateBps = parse<double>(fields[5], lineNumber);
		m_records.push_back(record);
	}
}

const std::vector<Record>& Player::records() const
{
	return m_records;
}

std::vector<Output> Player::replay(Estimator& estimator) const
{
	std::vector<Output> outputs;
	outputs.reserve(m_records.size());
	for (const Record& record : m_records)
	{
		outputs.push_back(estimator.update(record.input));
	}
	return outputs;
}

}
