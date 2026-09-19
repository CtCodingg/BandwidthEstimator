/// @file
/// @brief Loss-based bandwidth estimation using the Mathis formula.
///
/// BW = MSS * 8 / RTT * C / sqrt(p)

#pragma once

#include <string_view>

#include "algorithms/side_adapter.hpp"
#include "bwe/bandwidth_estimate.hpp"
#include "bwe/estimator_factory.hpp"
#include "filters/ewma_filter.hpp"
#include "interval_sample.hpp"

namespace bwe
{
namespace internal
{

/// @brief Validated parameters of the Mathis algorithm.
struct MathisParameters
{
	/// @brief Constant C of the formula, range (0, 10].
	double constant = 1.22;
	/// @brief Lower bound of the loss rate, range (0, 1).
	double min_loss_rate = 1e-4;
	/// @brief Smoothing factor of the moving average, range (0, 1].
	double smoothing = 0.3;
};

/// @brief Side-independent Mathis calculation.
class MathisCore
{
public:
	/// @brief Name under which the algorithm is registered.
	static constexpr std::string_view kName = "mathis";

	/// @brief Parses and validates parameters.
	/// @throws std::invalid_argument Unknown parameter key.
	/// @throws std::out_of_range Parameter value out of range.
	static MathisParameters ParseParameters(const Parameters& parameters);

	/// @param parameters Validated parameters.
	explicit MathisCore(const MathisParameters& parameters);

	/// @brief Processes one sample; samples without RTT, loss rate or MSS
	///        are ignored.
	void Update(const IntervalSample& sample) noexcept;

	/// @brief Returns the smoothed estimate.
	BandwidthEstimate GetEstimate() const noexcept
	{
		return MakeEstimate(filter_, timestamp_);
	}

	/// @brief Discards the estimate.
	void Reset() noexcept;

private:
	MathisParameters parameters_;
	EwmaFilter filter_;
	Duration timestamp_{0};
};

/// @brief Mathis estimator for sender-side statistics.
using MathisSenderEstimator = SenderAdapter<MathisCore>;

/// @brief Mathis estimator for receiver-side statistics.
using MathisReceiverEstimator = ReceiverAdapter<MathisCore>;

}  // namespace internal
}  // namespace bwe
