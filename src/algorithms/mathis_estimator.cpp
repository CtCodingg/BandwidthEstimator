#include "algorithms/mathis_estimator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "parameter_reader.hpp"

namespace bwe
{
namespace internal
{

MathisParameters MathisCore::ParseParameters(const Parameters& parameters)
{
	MathisParameters result;
	ParameterReader reader(kName, parameters);
	result.constant =
		reader.Get("constant", result.constant, Range::OpenClosed(0.0, 10.0));
	result.min_loss_rate = reader.Get("min_loss_rate", result.min_loss_rate,
									  Range::Open(0.0, 1.0));
	result.smoothing =
		reader.Get("smoothing", result.smoothing, Range::OpenClosed(0.0, 1.0));
	reader.CheckNoUnknownKeys();
	return result;
}

MathisCore::MathisCore(const MathisParameters& parameters)
	: parameters_(parameters), filter_(parameters.smoothing)
{
}

void MathisCore::Update(const IntervalSample& sample) noexcept
{
	if (!sample.rtt || !sample.loss_rate || !sample.mss_bytes ||
		sample.rtt->count() <= 0 || *sample.mss_bytes == 0)
	{
		return;
	}

	const double rtt_seconds =
		std::chrono::duration<double>(*sample.rtt).count();
	const double loss_rate =
		std::max(*sample.loss_rate, parameters_.min_loss_rate);
	filter_.Update(static_cast<double>(*sample.mss_bytes) * 8.0 / rtt_seconds *
				   parameters_.constant / std::sqrt(loss_rate));
	timestamp_ = sample.timestamp;
}

void MathisCore::Reset() noexcept
{
	filter_.Reset();
	timestamp_ = Duration{0};
}

}  // namespace internal
}  // namespace bwe
