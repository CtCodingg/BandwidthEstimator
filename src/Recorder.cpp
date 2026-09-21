#include "bwe/Recorder.hpp"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace bwe
{

Recorder::Recorder(std::ostream& out)
	: out_(out)
{
	out_ << kHeader << '\n';
	out_.flush();
	if (!out_)
	{
		throw std::runtime_error("bwe::Recorder: writing the header failed");
	}
}

void Recorder::RecordChannel(double rtt_ms, double drop_rate_percent)
{
	WriteLine("channel", 0, rtt_ms, drop_rate_percent, 0.0, 0.0, 0.0);
}

void Recorder::RecordStream(const StreamInput& stream)
{
	WriteLine("stream", stream.stream_id, 0.0, 0.0, stream.receive_rate_bps, stream.weight, stream.max_rate_bps);
}

void Recorder::RecordRemove(StreamId stream_id)
{
	WriteLine("remove", stream_id, 0.0, 0.0, 0.0, 0.0, 0.0);
}

void Recorder::WriteLine(const char* kind, StreamId stream_id, double rtt_ms, double drop_rate_percent,
	double receive_rate_bps, double weight, double max_rate_bps)
{
	std::ostringstream line;
	line.imbue(std::locale::classic());
	line << std::setprecision(std::numeric_limits<double>::max_digits10);

	std::lock_guard<std::mutex> lock(mutex_);
	++step_;
	line << step_ << ','
		<< kind << ','
		<< stream_id << ','
		<< rtt_ms << ','
		<< drop_rate_percent << ','
		<< receive_rate_bps << ','
		<< weight << ','
		<< max_rate_bps << '\n';
	out_ << line.str();
	out_.flush();
	if (!out_)
	{
		throw std::runtime_error("bwe::Recorder: writing a line failed");
	}
}

}
