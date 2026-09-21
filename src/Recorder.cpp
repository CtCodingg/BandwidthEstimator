#include "bwe/Recorder.hpp"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace bwe
{

Recorder::Recorder(std::ostream& out)
	: m_out(out)
{
	m_out << Header << '\n';
	m_out.flush();
	if (!m_out)
	{
		throw std::runtime_error("bwe::Recorder: writing the header failed");
	}
}

void Recorder::recordChannel(double rttMs, double dropRatePercent)
{
	writeLine("channel", 0, rttMs, dropRatePercent, 0.0, 0.0);
}

void Recorder::recordStream(const StreamInput& stream)
{
	writeLine("stream", stream.streamId, 0.0, 0.0, stream.receiveRateBps, stream.weight);
}

void Recorder::recordRemove(StreamId streamId)
{
	writeLine("remove", streamId, 0.0, 0.0, 0.0, 0.0);
}

void Recorder::writeLine(const char* kind, StreamId streamId, double rttMs, double dropRatePercent,
	double receiveRateBps, double weight)
{
	std::ostringstream line;
	line.imbue(std::locale::classic());
	line << std::setprecision(std::numeric_limits<double>::max_digits10);

	std::lock_guard<std::mutex> lock(m_mutex);
	++m_step;
	line << m_step << ','
		<< kind << ','
		<< streamId << ','
		<< rttMs << ','
		<< dropRatePercent << ','
		<< receiveRateBps << ','
		<< weight << '\n';
	m_out << line.str();
	m_out.flush();
	if (!m_out)
	{
		throw std::runtime_error("bwe::Recorder: writing a line failed");
	}
}

}
