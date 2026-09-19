/// @file
/// @brief Estimation and distribution of the bandwidth of a channel that is
///        shared by several senders.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "bwe/bandwidth_estimate.hpp"
#include "bwe/estimator_factory.hpp"
#include "bwe/export.hpp"
#include "bwe/measurement.hpp"

namespace bwe
{

/// @brief Identifies one sender (flow) on the shared channel.
using FlowId = std::uint64_t;

/// @brief Distribution settings of one flow.
struct FlowConfig
{
	/// @brief Relative share, range (0, 1e6].
	double weight = 1.0;
	/// @brief Rate in bit/s that is always granted, >= 0.
	double min_bps = 0.0;
	/// @brief Rate in bit/s that is never exceeded, e.g. the encoder maximum;
	///        > 0 and >= min_bps. Unset means unlimited.
	std::optional<double> max_bps;
};

/// @brief Target rate of one flow.
struct FlowAllocation
{
	/// @brief Flow the target belongs to.
	FlowId flow = 0;
	/// @brief Target rate in bit/s.
	double target_bps = 0.0;
};

/// @brief Estimates the total bandwidth of a shared channel at the receiver
///        and distributes it among the senders.
///
/// The statistics of all flows are combined into one measurement: counters
/// are summed, RTT and link capacity are the median over the flows, and the
/// receive buffer reserve is taken from the flow with the lowest reserve.
/// This measurement is processed by a receiver-side estimator. The time base
/// is the `now` passed to Tick(); flow timestamps are not used.
/// @note Thread-safe: all member functions may be called concurrently, e.g.
///       Update() from the statistics thread of each connection and
///       Tick()/Allocate() from a timer thread.
class BWE_API SharedLinkAllocator
{
public:
	/// @param algorithm Receiver-side algorithm of the EstimatorFactory.
	/// @param parameters Parameters of the algorithm.
	/// @param flow_timeout Flows without statistics for this long are left out
	///        of the combined measurement; > 0.
	/// @throws std::invalid_argument Unknown algorithm or invalid parameters.
	/// @throws std::out_of_range Parameter value or flow_timeout out of range.
	explicit SharedLinkAllocator(
		std::string_view algorithm = "hybrid",
		const Parameters& parameters = {},
		Duration flow_timeout = std::chrono::seconds(3));
	~SharedLinkAllocator();

	SharedLinkAllocator(const SharedLinkAllocator&) = delete;
	SharedLinkAllocator& operator=(const SharedLinkAllocator&) = delete;

	/// @brief Adds a flow.
	/// @throws std::invalid_argument Flow already exists or min_bps > max_bps.
	/// @throws std::out_of_range Weight or rates out of range.
	void AddFlow(FlowId flow, const FlowConfig& config = {});

	/// @brief Removes a flow; unknown flows are ignored.
	void RemoveFlow(FlowId flow) noexcept;

	/// @brief Stores the latest statistics of a flow; unknown flows are
	///        ignored.
	void Update(FlowId flow, const ReceiverMeasurement& measurement) noexcept;

	/// @brief Combines the latest statistics of all flows and updates the
	///        estimate; call periodically with increasing `now`.
	void Tick(Duration now) noexcept;

	/// @brief Returns the estimated total bandwidth of the channel.
	BandwidthEstimate GetTotalEstimate() const noexcept;

	/// @brief Distributes the total estimate by weight, respecting minimum and
	///        maximum rates. Minimum rates are granted even if they exceed the
	///        estimate.
	/// @return One target per flow sorted by flow id; empty while the
	///         estimate is not valid.
	std::vector<FlowAllocation> Allocate() const;

private:
	struct Impl;
	// Plain pointer instead of a member of a standard library class type:
	// exported classes must not contain such members (MSVC warning C4251).
	Impl* impl_;
};

}  // namespace bwe
