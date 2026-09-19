#include "bwe/shared_link_allocator.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace bwe
{
namespace
{

using std::chrono::milliseconds;
using std::chrono::seconds;

ReceiverMeasurement Stats(std::uint64_t bytes_received)
{
	ReceiverMeasurement measurement;
	measurement.common.rtt = milliseconds(20);
	measurement.bytes_received = bytes_received;
	measurement.bytes_received_unique = bytes_received;
	return measurement;
}

ReceiverMeasurement StatsWithReserve(std::uint64_t bytes_received,
									 milliseconds receive_buffer)
{
	ReceiverMeasurement measurement = Stats(bytes_received);
	measurement.receive_buffer_delay = receive_buffer;
	measurement.tsbpd_delay = milliseconds(120);
	return measurement;
}

// "delay" without smoothing estimates 1.25 * throughput without queueing.
SharedLinkAllocator MakeDelayAllocator()
{
	return SharedLinkAllocator("delay", {{"smoothing", 1.0}});
}

// Drives the total estimate of a delay allocator to 10 Mbit/s via `flow`.
void ReachTotalOfTenMbps(SharedLinkAllocator& allocator, FlowId flow)
{
	allocator.Update(flow, Stats(0));
	allocator.Tick(seconds(1));
	allocator.Update(flow, Stats(0));
	allocator.Tick(seconds(2));
	allocator.Update(flow, Stats(1'000'000));  // 8 Mbit/s.
	allocator.Tick(seconds(3));
}

double TargetOf(const std::vector<FlowAllocation>& allocations, FlowId flow)
{
	for (const FlowAllocation& allocation : allocations)
	{
		if (allocation.flow == flow)
		{
			return allocation.target_bps;
		}
	}
	return std::numeric_limits<double>::quiet_NaN();
}

TEST(SharedLinkAllocatorTest, ValidatesConstruction)
{
	EXPECT_THROW(SharedLinkAllocator("unknown"), std::invalid_argument);
	EXPECT_THROW(SharedLinkAllocator("send_buffer"), std::invalid_argument);
	EXPECT_THROW(SharedLinkAllocator("delay", {{"headroom", 0.5}}),
				 std::out_of_range);
	EXPECT_THROW(SharedLinkAllocator("hybrid", {}, Duration{0}),
				 std::out_of_range);
}

TEST(SharedLinkAllocatorTest, ValidatesFlows)
{
	SharedLinkAllocator allocator;
	allocator.AddFlow(1);
	EXPECT_THROW(allocator.AddFlow(1), std::invalid_argument);

	FlowConfig zero_weight;
	zero_weight.weight = 0.0;
	EXPECT_THROW(allocator.AddFlow(2, zero_weight), std::out_of_range);

	FlowConfig nan_weight;
	nan_weight.weight = std::numeric_limits<double>::quiet_NaN();
	EXPECT_THROW(allocator.AddFlow(2, nan_weight), std::out_of_range);

	FlowConfig negative_min;
	negative_min.min_bps = -1.0;
	EXPECT_THROW(allocator.AddFlow(2, negative_min), std::out_of_range);

	FlowConfig zero_max;
	zero_max.max_bps = 0.0;
	EXPECT_THROW(allocator.AddFlow(2, zero_max), std::out_of_range);

	FlowConfig min_above_max;
	min_above_max.min_bps = 2e6;
	min_above_max.max_bps = 1e6;
	EXPECT_THROW(allocator.AddFlow(2, min_above_max), std::invalid_argument);
}

TEST(SharedLinkAllocatorTest, AllocatesNothingWithoutEstimate)
{
	SharedLinkAllocator allocator;
	allocator.AddFlow(1);
	EXPECT_FALSE(allocator.GetTotalEstimate().valid);
	EXPECT_TRUE(allocator.Allocate().empty());
}

TEST(SharedLinkAllocatorTest, SumsThroughputOfAllFlows)
{
	SharedLinkAllocator allocator = MakeDelayAllocator();
	allocator.AddFlow(1);
	allocator.AddFlow(2);
	for (int second = 1; second <= 2; ++second)
	{
		allocator.Update(1, Stats(0));
		allocator.Update(2, Stats(0));
		allocator.Tick(seconds(second));
	}
	allocator.Update(1, Stats(125'000));
	allocator.Update(2, Stats(250'000));
	allocator.Tick(seconds(3));

	EXPECT_DOUBLE_EQ(allocator.GetTotalEstimate().bits_per_second, 1.25 * 3e6);
}

TEST(SharedLinkAllocatorTest, IgnoresHistoryOfLateJoiningFlow)
{
	SharedLinkAllocator allocator = MakeDelayAllocator();
	allocator.AddFlow(1);
	for (int second = 1; second <= 2; ++second)
	{
		allocator.Update(1, Stats(0));
		allocator.Tick(seconds(second));
	}
	allocator.AddFlow(2);
	allocator.Update(1, Stats(125'000));
	allocator.Update(2, Stats(1'000'000'000));
	allocator.Tick(seconds(3));
	EXPECT_DOUBLE_EQ(allocator.GetTotalEstimate().bits_per_second, 1.25e6);

	allocator.Update(1, Stats(250'000));
	allocator.Update(2, Stats(1'000'125'000));
	allocator.Tick(seconds(4));
	EXPECT_DOUBLE_EQ(allocator.GetTotalEstimate().bits_per_second, 2.5e6);
}

TEST(SharedLinkAllocatorTest, UsesMedianLinkCapacity)
{
	SharedLinkAllocator allocator(
		"link_capacity",
		{{"window_size", 1.0}, {"utilization", 1.0}, {"smoothing", 1.0}});
	const double capacities[] = {10e6, 11e6, 50e6};
	for (FlowId flow = 0; flow < 3; ++flow)
	{
		allocator.AddFlow(flow);
	}
	for (int second = 1; second <= 2; ++second)
	{
		for (FlowId flow = 0; flow < 3; ++flow)
		{
			ReceiverMeasurement stats = Stats(0);
			stats.common.link_capacity_bps = capacities[flow];
			allocator.Update(flow, stats);
		}
		allocator.Tick(seconds(second));
	}
	EXPECT_DOUBLE_EQ(allocator.GetTotalEstimate().bits_per_second, 11e6);
}

TEST(SharedLinkAllocatorTest, UsesLowestReceiveBufferReserve)
{
	SharedLinkAllocator allocator(
		"tsbpd_reserve", {{"smoothing", 1.0}, {"capacity_window", 1.0}});
	allocator.AddFlow(1);
	allocator.AddFlow(2);
	for (int second = 1; second <= 3; ++second)
	{
		const std::uint64_t bytes = second == 3 ? 125'000 : 0;
		allocator.Update(1, StatsWithReserve(bytes, milliseconds(120)));
		allocator.Update(2, StatsWithReserve(bytes, milliseconds(30)));
		allocator.Tick(seconds(second));
	}
	// Flow 2 has a reserve of 0.25 < 0.5: congestion, backoff 0.9.
	EXPECT_DOUBLE_EQ(allocator.GetTotalEstimate().bits_per_second, 0.9 * 2e6);
}

TEST(SharedLinkAllocatorTest, LeavesOutTimedOutFlows)
{
	SharedLinkAllocator allocator(
		"tsbpd_reserve", {{"smoothing", 1.0}, {"capacity_window", 1.0}},
		seconds(1));
	allocator.AddFlow(1);
	allocator.AddFlow(2);
	allocator.Update(2, StatsWithReserve(0, milliseconds(30)));
	for (int second = 1; second <= 3; ++second)
	{
		const std::uint64_t bytes = second == 3 ? 125'000 : 0;
		allocator.Update(1, StatsWithReserve(bytes, milliseconds(120)));
		allocator.Tick(seconds(second));
	}
	// Flow 2 was last seen 2 s ago: no congestion, headroom 1.2.
	EXPECT_DOUBLE_EQ(allocator.GetTotalEstimate().bits_per_second, 1.2e6);
}

TEST(SharedLinkAllocatorTest, SplitsEquallyByDefault)
{
	SharedLinkAllocator allocator = MakeDelayAllocator();
	for (FlowId flow = 0; flow < 4; ++flow)
	{
		allocator.AddFlow(flow);
	}
	ReachTotalOfTenMbps(allocator, 0);

	const std::vector<FlowAllocation> allocations = allocator.Allocate();
	ASSERT_EQ(allocations.size(), 4u);
	for (const FlowAllocation& allocation : allocations)
	{
		EXPECT_DOUBLE_EQ(allocation.target_bps, 2.5e6);
	}
}

TEST(SharedLinkAllocatorTest, SplitsByWeight)
{
	SharedLinkAllocator allocator = MakeDelayAllocator();
	FlowConfig heavy;
	heavy.weight = 2.0;
	allocator.AddFlow(0, heavy);
	allocator.AddFlow(1);
	allocator.AddFlow(2);
	ReachTotalOfTenMbps(allocator, 0);

	const std::vector<FlowAllocation> allocations = allocator.Allocate();
	EXPECT_DOUBLE_EQ(TargetOf(allocations, 0), 5e6);
	EXPECT_DOUBLE_EQ(TargetOf(allocations, 1), 2.5e6);
	EXPECT_DOUBLE_EQ(TargetOf(allocations, 2), 2.5e6);
}

TEST(SharedLinkAllocatorTest, RedistributesAboveMaximum)
{
	SharedLinkAllocator allocator = MakeDelayAllocator();
	FlowConfig limited;
	limited.max_bps = 1e6;
	allocator.AddFlow(0);
	allocator.AddFlow(1, limited);
	allocator.AddFlow(2);
	ReachTotalOfTenMbps(allocator, 0);

	const std::vector<FlowAllocation> allocations = allocator.Allocate();
	EXPECT_DOUBLE_EQ(TargetOf(allocations, 0), 4.5e6);
	EXPECT_DOUBLE_EQ(TargetOf(allocations, 1), 1e6);
	EXPECT_DOUBLE_EQ(TargetOf(allocations, 2), 4.5e6);
}

TEST(SharedLinkAllocatorTest, StopsAtAllMaxima)
{
	SharedLinkAllocator allocator = MakeDelayAllocator();
	FlowConfig limited;
	limited.max_bps = 1e6;
	for (FlowId flow = 0; flow < 3; ++flow)
	{
		allocator.AddFlow(flow, limited);
	}
	ReachTotalOfTenMbps(allocator, 0);

	for (const FlowAllocation& allocation : allocator.Allocate())
	{
		EXPECT_DOUBLE_EQ(allocation.target_bps, 1e6);
	}
}

TEST(SharedLinkAllocatorTest, GrantsMinimumFirst)
{
	SharedLinkAllocator allocator = MakeDelayAllocator();
	FlowConfig guaranteed;
	guaranteed.min_bps = 4e6;
	allocator.AddFlow(0);
	allocator.AddFlow(1, guaranteed);
	allocator.AddFlow(2);
	ReachTotalOfTenMbps(allocator, 0);

	const std::vector<FlowAllocation> allocations = allocator.Allocate();
	EXPECT_DOUBLE_EQ(TargetOf(allocations, 0), 2e6);
	EXPECT_DOUBLE_EQ(TargetOf(allocations, 1), 6e6);
	EXPECT_DOUBLE_EQ(TargetOf(allocations, 2), 2e6);
}

TEST(SharedLinkAllocatorTest, GrantsMinimaAboveEstimate)
{
	SharedLinkAllocator allocator = MakeDelayAllocator();
	FlowConfig guaranteed;
	guaranteed.min_bps = 6e6;
	allocator.AddFlow(0, guaranteed);
	allocator.AddFlow(1, guaranteed);
	ReachTotalOfTenMbps(allocator, 0);

	for (const FlowAllocation& allocation : allocator.Allocate())
	{
		EXPECT_DOUBLE_EQ(allocation.target_bps, 6e6);
	}
}

TEST(SharedLinkAllocatorTest, SortsByFlowAndHandlesRemoval)
{
	SharedLinkAllocator allocator = MakeDelayAllocator();
	allocator.AddFlow(5);
	allocator.AddFlow(1);
	allocator.AddFlow(3);
	allocator.RemoveFlow(3);
	allocator.RemoveFlow(99);
	allocator.Update(99, Stats(0));
	ReachTotalOfTenMbps(allocator, 1);

	const std::vector<FlowAllocation> allocations = allocator.Allocate();
	ASSERT_EQ(allocations.size(), 2u);
	EXPECT_EQ(allocations[0].flow, 1u);
	EXPECT_EQ(allocations[1].flow, 5u);
	EXPECT_DOUBLE_EQ(allocations[0].target_bps, 5e6);
}

}  // namespace
}  // namespace bwe
