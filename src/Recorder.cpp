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

void Recorder::record(const Input& input, const Output& output)
{
	std::ostringstream line;
	line.imbue(std::locale::classic());
	line << std::setprecision(std::numeric_limits<double>::max_digits10);

	std::lock_guard<std::mutex> lock(m_mutex);
	++m_sequence;
	line << m_sequence << ','
		<< input.streamId << ','
		<< input.rttMs << ','
		<< input.dropRatePercent << ','
		<< input.receiveRateBps << ','
		<< output.rateBps << '\n';
	m_out << line.str();
	m_out.flush();
	if (!m_out)
	{
		throw std::runtime_error("bwe::Recorder: writing a line failed");
	}
}

}
