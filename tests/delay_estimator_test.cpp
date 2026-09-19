#include "algorithms/delay_estimator.hpp"

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

IntervalSample MakeSample(milliseconds rtt, double throughput_bps,
						  seconds timestamp = seconds(1))
{
	IntervalSample sample;
	sample.timestamp = timestamp;
	sample.duration = seconds(1);
	sample.rtt = rtt;
	sample.throughput_bps = throughput_bps;
	return sample;
}

DelayCore MakeUnsmoothedCore()
{
	DelayParameters parameters;
	parameters.smoothing = 1.0;
	return DelayCore(parameters);
}

TEST(DelayParametersTest, UsesDefaultsForEmptyParameters)
{
	const DelayParameters parameters = DelayCore::ParseParameters({});
	EXPECT_DOUBLE_EQ(parameters.low_threshold_ms, 10.0);
	EXPECT_DOUBLE_EQ(parameters.high_threshold_ms, 50.0);
	EXPECT_DOUBLE_EQ(parameters.headroom, 1.25);
	EXPECT_DOUBLE_EQ(parameters.backoff, 0.9);
	EXPECT_DOUBLE_EQ(parameters.base_rtt_window_s, 30.0);
	EXPECT_DOUBLE_EQ(parameters.smoothing, 0.3);
}

TEST(DelayParametersTest, RejectsInvalidParameters)
{
	EXPECT_THROW(DelayCore::ParseParameters({{"unknown", 1.0}}),
				 std::invalid_argument);
	EXPECT_THROW(DelayCore::ParseParameters({{"headroom", 0.5}}),
				 std::out_of_range);
	EXPECT_THROW(DelayCore::ParseParameters({{"backoff", 0.0}}),
				 std::out_of_range);
	EXPECT_THROW(DelayCore::ParseParameters({{"base_rtt_window_s", 0.0}}),
				 std::out_of_range);
}

TEST(DelayParametersTest, RejectsContradictoryThresholds)
{
	EXPECT_THROW(DelayCore::ParseParameters(
					 {{"low_threshold_ms", 60.0}, {"high_threshold_ms", 50.0}}),
				 std::invalid_argument);
	EXPECT_THROW(DelayCore::ParseParameters(
					 {{"low_threshold_ms", 50.0}, {"high_threshold_ms", 50.0}}),
				 std::invalid_argument);
}

TEST(DelayCoreTest, IgnoresIncompleteSamples)
{
	DelayCore core = MakeUnsmoothedCore();
	IntervalSample without_rtt = MakeSample(milliseconds(50), 1e6);
	without_rtt.rtt.reset();
	IntervalSample without_throughput = MakeSample(milliseconds(50), 1e6);
	without_throughput.throughput_bps.reset();
	core.Update(without_rtt);
	core.Update(without_throughput);
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(DelayCoreTest, RisesWithoutQueueingDelay)
{
	DelayCore core = MakeUnsmoothedCore();
	core.Update(MakeSample(milliseconds(50), 1e6));
	EXPECT_TRUE(core.GetEstimate().valid);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1.25e6);
}

TEST(DelayCoreTest, HoldsBetweenThresholds)
{
	DelayCore core = MakeUnsmoothedCore();
	core.Update(MakeSample(milliseconds(50), 1e6));
	core.Update(MakeSample(milliseconds(80), 1e6));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1e6);
}

TEST(DelayCoreTest, FallsAboveHighThreshold)
{
	DelayCore core = MakeUnsmoothedCore();
	core.Update(MakeSample(milliseconds(50), 1e6));
	core.Update(MakeSample(milliseconds(150), 1e6));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 0.9e6);
}

TEST(DelayCoreTest, FollowsLowerBaseRtt)
{
	DelayCore core = MakeUnsmoothedCore();
	core.Update(MakeSample(milliseconds(50), 1e6));
	core.Update(MakeSample(milliseconds(40), 1e6));
	core.Update(MakeSample(milliseconds(90), 1e6));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 0.9e6);
}

TEST(DelayCoreTest, RemeasuresBaseRttAfterWindow)
{
	DelayParameters parameters;
	parameters.smoothing = 1.0;
	parameters.base_rtt_window_s = 10.0;
	DelayCore core(parameters);
	core.Update(MakeSample(milliseconds(50), 1e6, seconds(0)));
	// The first window still contains the 50 ms minimum.
	core.Update(MakeSample(milliseconds(150), 1e6, seconds(20)));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 0.9e6);
	// After a whole window at 150 ms it becomes the new base.
	core.Update(MakeSample(milliseconds(150), 1e6, seconds(40)));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1.25e6);
}

TEST(DelayCoreTest, RespectsConfiguredMaximum)
{
	DelayCore core = MakeUnsmoothedCore();
	IntervalSample sample = MakeSample(milliseconds(50), 1e6);
	sample.max_bandwidth_bps = 1.1e6;
	core.Update(sample);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1.1e6);
}

TEST(DelayCoreTest, ResetDiscardsEstimateAndBaseRtt)
{
	DelayCore core = MakeUnsmoothedCore();
	core.Update(MakeSample(milliseconds(50), 1e6));
	core.Reset();
	EXPECT_FALSE(core.GetEstimate().valid);
	core.Update(MakeSample(milliseconds(150), 1e6));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1.25e6);
}

TEST(DelayRegistrationTest, IsAvailableOnBothSides)
{
	EstimatorFactory& factory = EstimatorFactory::Instance();
	EXPECT_EQ(factory.CreateSender("delay")->Name(), "delay");
	EXPECT_EQ(factory.CreateReceiver("delay")->Name(), "delay");
}

}  // namespace
}  // namespace internal
}  // namespace bwe
