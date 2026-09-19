#include "algorithms/link_capacity_estimator.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>

namespace bwe
{
namespace internal
{
namespace
{

IntervalSample MakeSample(double link_capacity_bps)
{
	IntervalSample sample;
	sample.timestamp = std::chrono::seconds(1);
	sample.duration = std::chrono::seconds(1);
	sample.link_capacity_bps = link_capacity_bps;
	return sample;
}

LinkCapacityCore MakeUnsmoothedCore(int window_size)
{
	LinkCapacityParameters parameters;
	parameters.window_size = window_size;
	parameters.utilization = 1.0;
	parameters.smoothing = 1.0;
	return LinkCapacityCore(parameters);
}

TEST(LinkCapacityParametersTest, UsesDefaultsForEmptyParameters)
{
	const LinkCapacityParameters parameters =
		LinkCapacityCore::ParseParameters({});
	EXPECT_EQ(parameters.window_size, 5);
	EXPECT_DOUBLE_EQ(parameters.utilization, 0.9);
	EXPECT_DOUBLE_EQ(parameters.smoothing, 0.3);
}

TEST(LinkCapacityParametersTest, RejectsInvalidParameters)
{
	EXPECT_THROW(LinkCapacityCore::ParseParameters({{"unknown", 1.0}}),
				 std::invalid_argument);
	EXPECT_THROW(LinkCapacityCore::ParseParameters({{"window_size", 2.5}}),
				 std::invalid_argument);
	EXPECT_THROW(LinkCapacityCore::ParseParameters({{"window_size", 0.0}}),
				 std::out_of_range);
	EXPECT_THROW(LinkCapacityCore::ParseParameters({{"window_size", 32.0}}),
				 std::out_of_range);
	EXPECT_THROW(LinkCapacityCore::ParseParameters({{"utilization", 0.0}}),
				 std::out_of_range);
}

TEST(LinkCapacityCoreTest, IgnoresMissingOrZeroCapacity)
{
	LinkCapacityCore core = MakeUnsmoothedCore(3);
	IntervalSample without_capacity = MakeSample(1e6);
	without_capacity.link_capacity_bps.reset();
	core.Update(without_capacity);
	core.Update(MakeSample(0.0));
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(LinkCapacityCoreTest, SuppressesOutliersWithMedian)
{
	LinkCapacityCore core = MakeUnsmoothedCore(3);
	core.Update(MakeSample(10e6));
	core.Update(MakeSample(100e6));
	core.Update(MakeSample(10e6));
	EXPECT_TRUE(core.GetEstimate().valid);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 10e6);
}

TEST(LinkCapacityCoreTest, AppliesUtilizationAndLoss)
{
	LinkCapacityParameters parameters;
	parameters.smoothing = 1.0;
	LinkCapacityCore core(parameters);
	IntervalSample sample = MakeSample(10e6);
	sample.loss_rate = 0.1;
	core.Update(sample);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 8.1e6);
}

TEST(LinkCapacityCoreTest, RespectsConfiguredMaximum)
{
	LinkCapacityCore core = MakeUnsmoothedCore(1);
	IntervalSample sample = MakeSample(10e6);
	sample.max_bandwidth_bps = 5e6;
	core.Update(sample);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 5e6);
}

TEST(LinkCapacityCoreTest, ResetDiscardsEstimate)
{
	LinkCapacityCore core = MakeUnsmoothedCore(3);
	core.Update(MakeSample(10e6));
	core.Reset();
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(LinkCapacityRegistrationTest, IsAvailableOnBothSides)
{
	EstimatorFactory& factory = EstimatorFactory::Instance();
	EXPECT_EQ(factory.CreateSender("link_capacity")->Name(), "link_capacity");
	EXPECT_EQ(factory.CreateReceiver("link_capacity")->Name(), "link_capacity");
}

}  // namespace
}  // namespace internal
}  // namespace bwe
