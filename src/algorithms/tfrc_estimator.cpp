#include "algorithms/tfrc_estimator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "parameter_reader.hpp"

namespace bwe
{
namespace internal
{

TfrcParameters TfrcCore::ParseParameters(const Parameters& parameters)
{
	TfrcParameters result;
	ParameterReader reader(kName, parameters);
	result.packets_per_ack = reader.Get(
		"packets_per_ack", result.packets_per_ack, Range::OpenClosed(0.0, 10.0));
	result.rto_factor = reader.Get("rto_factor", result.rto_factor,
								   Range::OpenClosed(0.0, 100.0));
	result.min_loss_rate = reader.Get("min_loss_rate", result.min_loss_rate,
									  Range::Open(0.0, 1.0));
	result.smoothing =
		reader.Get("smoothing", result.smoothing, Range::OpenClosed(0.0, 1.0));
	reader.CheckNoUnknownKeys();
	return result;
}

TfrcCore::TfrcCore(const TfrcParameters& parameters)
	: parameters_(parameters), filter_(parameters.smoothing)
{
}

void TfrcCore::Update(const IntervalSample& sample) noexcept
{
	if (!sample.rtt || !sample.loss_rate || !sample.mss_bytes ||
		sample.rtt->count() <= 0 || *sample.mss_bytes == 0)
	{
		return;
	}

	const double s = static_cast<double>(*sample.mss_bytes);
	const double r = std::chrono::duration<double>(*sample.rtt).count();
	const double p = std::max(*sample.loss_rate, parameters_.min_loss_rate);
	const double b = parameters_.packets_per_ack;
	const double t_rto = parameters_.rto_factor * r;

	const double denominator =
		r * std::sqrt(2.0 * b * p / 3.0) +
		t_rto * 3.0 * std::sqrt(3.0 * b * p / 8.0) * p * (1.0 + 32.0 * p * p);
	filter_.Update(s * 8.0 / denominator);
	timestamp_ = sample.timestamp;
}

void TfrcCore::Reset() noexcept
{
	filter_.Reset();
	timestamp_ = Duration{0};
}

}  // namespace internal
}  // namespace bwe
