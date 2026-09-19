#include "algorithms/send_buffer_estimator.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>

namespace bwe
{
namespace internal
{
namespace
{

using std::chrono::milliseconds;
using std::chrono::seconds;

IntervalSample MakeSample(milliseconds buffer_delay, double throughput_bps)
{
	IntervalSample sample;
	sample.side = Side::kSender;
	sample.timestamp = seconds(1);
	sample.duration = seconds(1);
	sample.buffer_delay = buffer_delay;
	sample.throughput_bps = throughput_bps;
	return sample;
}

SendBufferCore MakeUnsmoothedCore()
{
	SendBufferParameters parameters;
	parameters.smoothing = 1.0;
	parameters.capacity_window = 1;
	return SendBufferCore(parameters);
}

TEST(SendBufferParametersTest, UsesDefaultsForEmptyParameters)
{
	const SendBufferParameters parameters =
		SendBufferCore::ParseParameters({});
	EXPECT_DOUBLE_EQ(parameters.buffer_threshold_ms, 100.0);
	EXPECT_DOUBLE_EQ(parameters.max_growth_rate, 0.05);
	EXPECT_DOUBLE_EQ(parameters.backoff, 0.9);
	EXPECT_DOUBLE_EQ(parameters.headroom, 1.2);
	EXPECT_EQ(parameters.capacity_window, 5);
	EXPECT_DOUBLE_EQ(parameters.smoothing, 0.3);
}

TEST(SendBufferParametersTest, RejectsInvalidParameters)
{
	EXPECT_THROW(SendBufferCore::ParseParameters({{"unknown", 1.0}}),
				 std::invalid_argument);
	EXPECT_THROW(SendBufferCore::ParseParameters({{"buffer_threshold_ms", 0.0}}),
				 std::out_of_range);
	EXPECT_THROW(SendBufferCore::ParseParameters({{"backoff", 1.5}}),
				 std::out_of_range);
}

TEST(SendBufferCoreTest, IgnoresIncompleteAndReceiverSamples)
{
	SendBufferCore core = MakeUnsmoothedCore();
	IntervalSample without_delay = MakeSample(milliseconds(10), 1e6);
	without_delay.buffer_delay.reset();
	IntervalSample without_throughput = MakeSample(milliseconds(10), 1e6);
	without_throughput.throughput_bps.reset();
	IntervalSample receiver = MakeSample(milliseconds(10), 1e6);
	receiver.side = Side::kReceiver;

	core.Update(without_delay);
	core.Update(without_throughput);
	core.Update(receiver);
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(SendBufferCoreTest, UsesHeadroomWithoutCongestion)
{
	SendBufferCore core = MakeUnsmoothedCore();
	core.Update(MakeSample(milliseconds(10), 1e6));
	EXPECT_TRUE(core.GetEstimate().valid);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1.2e6);
}

TEST(SendBufferCoreTest, UsesLinkCapacityWithoutCongestion)
{
	SendBufferCore core = MakeUnsmoothedCore();
	IntervalSample sample = MakeSample(milliseconds(10), 1e6);
	sample.link_capacity_bps = 5e6;
	core.Update(sample);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 5e6);

	sample.link_capacity_bps = 0.5e6;
	core.Update(sample);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1e6);
}

TEST(SendBufferCoreTest, BacksOffWhenBufferExceedsThreshold)
{
	SendBufferCore core = MakeUnsmoothedCore();
	IntervalSample sample = MakeSample(milliseconds(200), 1e6);
	sample.link_capacity_bps = 5e6;
	core.Update(sample);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 0.9e6);
}

TEST(SendBufferCoreTest, BacksOffWhenBufferGrowsFast)
{
	SendBufferCore core = MakeUnsmoothedCore();
	core.Update(MakeSample(milliseconds(10), 1e6));
	core.Update(MakeSample(milliseconds(80), 1e6));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 0.9e6);
}

TEST(SendBufferCoreTest, RespectsConfiguredMaximum)
{
	SendBufferCore core = MakeUnsmoothedCore();
	IntervalSample sample = MakeSample(milliseconds(10), 1e6);
	sample.max_bandwidth_bps = 1.1e6;
	core.Update(sample);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1.1e6);
}

TEST(SendBufferCoreTest, ResetDiscardsEstimate)
{
	SendBufferCore core = MakeUnsmoothedCore();
	core.Update(MakeSample(milliseconds(10), 1e6));
	core.Reset();
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(SendBufferRegistrationTest, IsAvailableOnSenderSideOnly)
{
	EstimatorFactory& factory = EstimatorFactory::Instance();
	EXPECT_EQ(factory.CreateSender("send_buffer")->Name(), "send_buffer");
	EXPECT_FALSE(factory.HasReceiverAlgorithm("send_buffer"));
}

TEST(SendBufferCoreTest, SuppressesCapacityOutliers)
{
	SendBufferParameters parameters;
	parameters.smoothing = 1.0;
	parameters.capacity_window = 3;
	SendBufferCore core(parameters);
	IntervalSample sample = MakeSample(milliseconds(10), 1e6);
	for (double capacity : {5e6, 20e6, 5e6})
	{
		sample.link_capacity_bps = capacity;
		core.Update(sample);
	}
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 5e6);
}

TEST(SendBufferParametersTest, RejectsNonIntegralCapacityWindow)
{
	EXPECT_THROW(SendBufferCore::ParseParameters({{"capacity_window", 2.5}}),
				 std::invalid_argument);
	EXPECT_THROW(SendBufferCore::ParseParameters({{"capacity_window", 0.0}}),
				 std::out_of_range);
}

}  // namespace
}  // namespace internal
}  // namespace bwe
