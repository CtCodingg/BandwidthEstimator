/// @file
/// @brief Loss-based bandwidth estimation using the TFRC throughput
///        equation (RFC 5348, section 3.1).
///
/// X = s / (R * sqrt(2bp/3) + t_RTO * 3 * sqrt(3bp/8) * p * (1 + 32p^2))
///
/// The packet loss rate is used as an approximation of the loss event rate.

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

/// @brief Validated parameters of the TFRC algorithm.
struct TfrcParameters
{
	/// @brief Packets acknowledged per acknowledgement (b), range (0, 10].
	double packets_per_ack = 1.0;
	/// @brief Retransmission timeout as multiple of the RTT, range (0, 100].
	double rto_factor = 4.0;
	/// @brief Lower bound of the loss rate, range (0, 1).
	double min_loss_rate = 1e-4;
	/// @brief Smoothing factor of the moving average, range (0, 1].
	double smoothing = 0.3;
};

/// @brief Side-independent TFRC calculation.
class TfrcCore
{
public:
	/// @brief Name under which the algorithm is registered.
	static constexpr std::string_view kName = "tfrc";

	/// @brief Parses and validates parameters.
	/// @throws std::invalid_argument Unknown parameter key.
	/// @throws std::out_of_range Parameter value out of range.
	static TfrcParameters ParseParameters(const Parameters& parameters);

	/// @param parameters Validated parameters.
	explicit TfrcCore(const TfrcParameters& parameters);

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
	TfrcParameters parameters_;
	EwmaFilter filter_;
	Duration timestamp_{0};
};

/// @brief TFRC estimator for sender-side statistics.
using TfrcSenderEstimator = SenderAdapter<TfrcCore>;

/// @brief TFRC estimator for receiver-side statistics.
using TfrcReceiverEstimator = ReceiverAdapter<TfrcCore>;

}  // namespace internal
}  // namespace bwe
