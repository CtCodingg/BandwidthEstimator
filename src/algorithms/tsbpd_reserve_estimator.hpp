/// @file
/// @brief Receiver-side estimation from the reserve of the receive buffer.
///
/// The reserve r = receive buffer delay / configured latency falls when
/// packets arrive later than planned. Below the minimum reserve, or when
/// the share of late and dropped packets exceeds the threshold, the
/// estimate is backoff * throughput. Otherwise it is the link capacity (at
/// least the throughput) or, without it, headroom * throughput.

#pragma once

#include <string_view>

#include "algorithms/side_adapter.hpp"
#include "bwe/bandwidth_estimate.hpp"
#include "bwe/estimator_factory.hpp"
#include "filters/ewma_filter.hpp"
#include "filters/median_filter.hpp"
#include "interval_sample.hpp"

namespace bwe
{
namespace internal
{

/// @brief Validated parameters of the receive buffer reserve algorithm.
struct TsbpdReserveParameters
{
	/// @brief Reserve below which congestion is assumed, range (0, 1].
	double min_reserve = 0.5;
	/// @brief Share of late and dropped packets above which congestion is
	///        assumed, range [0, 1).
	double late_threshold = 0.01;
	/// @brief Factor applied to the throughput on congestion, range (0, 1].
	double backoff = 0.9;
	/// @brief Multiple of the throughput without congestion and without
	///        link capacity, range [1, 10].
	double headroom = 1.2;
	/// @brief Number of link capacity reports for the median, integer in
	///        [1, 31].
	int capacity_window = 5;
	/// @brief Smoothing factor of the moving average, range (0, 1].
	double smoothing = 0.3;
};

/// @brief Receive buffer reserve calculation; processes receiver samples
///        only.
class TsbpdReserveCore
{
public:
	/// @brief Name under which the algorithm is registered.
	static constexpr std::string_view kName = "tsbpd_reserve";

	/// @brief Parses and validates parameters.
	/// @throws std::invalid_argument Unknown key or non-integral
	///         capacity_window.
	/// @throws std::out_of_range Parameter value out of range.
	static TsbpdReserveParameters ParseParameters(const Parameters& parameters);

	/// @param parameters Validated parameters.
	explicit TsbpdReserveCore(const TsbpdReserveParameters& parameters);

	/// @brief Processes one sample; sender samples and samples without buffer
	///        delay, latency or throughput are ignored.
	void Update(const IntervalSample& sample) noexcept;

	/// @brief Returns the smoothed estimate.
	BandwidthEstimate GetEstimate() const noexcept
	{
		return MakeEstimate(filter_, timestamp_);
	}

	/// @brief Discards the estimate.
	void Reset() noexcept;

private:
	TsbpdReserveParameters parameters_;
	MedianFilter capacity_median_;
	EwmaFilter filter_;
	Duration timestamp_{0};
};

/// @brief Receive buffer reserve estimator; receiver side only.
using TsbpdReserveReceiverEstimator = ReceiverAdapter<TsbpdReserveCore>;

}  // namespace internal
}  // namespace bwe
