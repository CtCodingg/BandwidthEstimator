#include "algorithms/hybrid_estimator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace bwe
{
namespace internal
{
namespace
{

using std::chrono::milliseconds;

IntervalSample MakeSenderSample(milliseconds send_buffer_delay)
{
	IntervalSample sample;
	sample.side = Side::kSender;
	sample.timestamp = std::chrono::seconds(1);
	sample.duration = std::chrono::seconds(1);
	sample.rtt = milliseconds(50);
	sample.loss_rate = 0.0;
	sample.throughput_bps = 1e6;
	sample.link_capacity_bps = 5e6;
	sample.buffer_delay = send_buffer_delay;
	return sample;
}

TEST(HybridParametersTest, RoutesPrefixedParameters)
{
	const HybridParameters parameters = HybridCore::ParseParameters(
		{{"aimd.headroom", 2.0},
		 {"delay.smoothing", 1.0},
		 {"link_capacity.window_size", 3.0},
		 {"send_buffer.backoff", 0.8},
		 {"tsbpd_reserve.min_reserve", 0.4}});
	EXPECT_DOUBLE_EQ(parameters.aimd.headroom, 2.0);
	EXPECT_DOUBLE_EQ(parameters.delay.smoothing, 1.0);
	EXPECT_EQ(parameters.link_capacity.window_size, 3);
	EXPECT_DOUBLE_EQ(parameters.send_buffer.backoff, 0.8);
	EXPECT_DOUBLE_EQ(parameters.tsbpd_reserve.min_reserve, 0.4);
}

TEST(HybridParametersTest, RejectsUnknownPrefixes)
{
	EXPECT_THROW(HybridCore::ParseParameters({{"headroom", 1.0}}),
				 std::invalid_argument);
	EXPECT_THROW(HybridCore::ParseParameters({{"mathis.constant", 1.0}}),
				 std::invalid_argument);
	EXPECT_THROW(HybridCore::ParseParameters({{"aimd.", 1.0}}),
				 std::invalid_argument);
}

TEST(HybridParametersTest, PropagatesComponentErrors)
{
	EXPECT_THROW(HybridCore::ParseParameters({{"aimd.typo", 1.0}}),
				 std::invalid_argument);
	EXPECT_THROW(HybridCore::ParseParameters({{"aimd.headroom", 0.5}}),
				 std::out_of_range);
}

TEST(HybridCoreTest, IsInvalidWithoutSamples)
{
	HybridCore core(HybridParameters{});
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(HybridCoreTest, ReturnsLowestComponentEstimate)
{
	const HybridParameters parameters;
	HybridCore hybrid(parameters);
	AimdCore aimd(parameters.aimd);
	DelayCore delay(parameters.delay);
	LinkCapacityCore link_capacity(parameters.link_capacity);
	SendBufferCore send_buffer(parameters.send_buffer);

	const IntervalSample sample = MakeSenderSample(milliseconds(10));
	hybrid.Update(sample);
	aimd.Update(sample);
	delay.Update(sample);
	link_capacity.Update(sample);
	send_buffer.Update(sample);

	const double expected = std::min(
		{aimd.GetEstimate().bits_per_second, delay.GetEstimate().bits_per_second,
		 link_capacity.GetEstimate().bits_per_second,
		 send_buffer.GetEstimate().bits_per_second});
	EXPECT_TRUE(hybrid.GetEstimate().valid);
	EXPECT_DOUBLE_EQ(hybrid.GetEstimate().bits_per_second, expected);
	EXPECT_DOUBLE_EQ(expected, 1.25e6);
}

TEST(HybridCoreTest, FollowsCongestedComponent)
{
	HybridCore core(HybridParameters{});
	core.Update(MakeSenderSample(milliseconds(200)));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 0.9e6);
}

TEST(HybridCoreTest, ResetDiscardsAllEstimates)
{
	HybridCore core(HybridParameters{});
	core.Update(MakeSenderSample(milliseconds(10)));
	core.Reset();
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(HybridRegistrationTest, IsAvailableOnBothSides)
{
	EstimatorFactory& factory = EstimatorFactory::Instance();
	EXPECT_EQ(factory.CreateSender("hybrid")->Name(), "hybrid");
	EXPECT_EQ(factory.CreateReceiver("hybrid")->Name(), "hybrid");
	EXPECT_THROW(factory.CreateSender("hybrid", {{"delay.headroom", 0.0}}),
				 std::out_of_range);
}

}  // namespace
}  // namespace internal
}  // namespace bwe
