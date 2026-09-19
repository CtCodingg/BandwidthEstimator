#include "algorithms/aimd_estimator.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <stdexcept>

namespace bwe
{
namespace internal
{
namespace
{

using std::chrono::seconds;

IntervalSample MakeSample(double loss_rate,
						  std::optional<double> throughput_bps)
{
	IntervalSample sample;
	sample.timestamp = seconds(1);
	sample.duration = seconds(1);
	sample.loss_rate = loss_rate;
	sample.throughput_bps = throughput_bps;
	return sample;
}

TEST(AimdParametersTest, UsesDefaultsForEmptyParameters)
{
	const AimdParameters parameters = AimdCore::ParseParameters({});
	EXPECT_DOUBLE_EQ(parameters.loss_threshold, 0.02);
	EXPECT_DOUBLE_EQ(parameters.decrease_factor, 0.85);
	EXPECT_DOUBLE_EQ(parameters.increase_bps, 250e3);
	EXPECT_DOUBLE_EQ(parameters.headroom, 1.5);
	EXPECT_DOUBLE_EQ(parameters.min_bps, 100e3);
}

TEST(AimdParametersTest, RejectsInvalidParameters)
{
	EXPECT_THROW(AimdCore::ParseParameters({{"unknown", 1.0}}),
				 std::invalid_argument);
	EXPECT_THROW(AimdCore::ParseParameters({{"decrease_factor", 1.0}}),
				 std::out_of_range);
	EXPECT_THROW(AimdCore::ParseParameters({{"headroom", 0.5}}),
				 std::out_of_range);
	EXPECT_THROW(AimdCore::ParseParameters({{"min_bps", -1.0}}),
				 std::out_of_range);
}

TEST(AimdCoreTest, IgnoresSamplesWithoutLossRate)
{
	AimdCore core(AimdParameters{});
	IntervalSample sample = MakeSample(0.0, 1e6);
	sample.loss_rate.reset();
	core.Update(sample);
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(AimdCoreTest, StartsFromThroughputAndIncreases)
{
	AimdCore core(AimdParameters{});
	core.Update(MakeSample(0.0, 1e6));
	EXPECT_TRUE(core.GetEstimate().valid);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1.25e6);

	core.Update(MakeSample(0.0, 2e6));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1.5e6);
}

TEST(AimdCoreTest, IsLimitedByHeadroom)
{
	AimdCore core(AimdParameters{});
	for (int i = 0; i < 5; ++i)
	{
		core.Update(MakeSample(0.0, 1e6));
	}
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1.5e6);
}

TEST(AimdCoreTest, DecreasesOnLoss)
{
	AimdCore core(AimdParameters{});
	core.Update(MakeSample(0.0, 1e6));
	core.Update(MakeSample(0.05, 1e6));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 0.85e6);
}

TEST(AimdCoreTest, DecreasesFromEstimateWithoutThroughput)
{
	AimdCore core(AimdParameters{});
	core.Update(MakeSample(0.0, 1e6));
	core.Update(MakeSample(0.05, std::nullopt));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 0.85 * 1.25e6);
}

TEST(AimdCoreTest, RespectsMinimum)
{
	AimdCore core(AimdParameters{});
	core.Update(MakeSample(0.5, 10e3));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 100e3);
}

TEST(AimdCoreTest, RespectsConfiguredMaximum)
{
	AimdCore core(AimdParameters{});
	IntervalSample sample = MakeSample(0.0, 1e6);
	sample.max_bandwidth_bps = 1.1e6;
	core.Update(sample);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1.1e6);
}

TEST(AimdCoreTest, ResetDiscardsEstimate)
{
	AimdCore core(AimdParameters{});
	core.Update(MakeSample(0.0, 1e6));
	core.Reset();
	EXPECT_FALSE(core.GetEstimate().valid);
	EXPECT_DOUBLE_EQ(core.GetEstimate().confidence, 0.0);
}

TEST(AimdRegistrationTest, IsAvailableOnBothSides)
{
	EstimatorFactory& factory = EstimatorFactory::Instance();
	EXPECT_EQ(factory.CreateSender("aimd")->Name(), "aimd");
	EXPECT_EQ(factory.CreateReceiver("aimd")->Name(), "aimd");
}

}  // namespace
}  // namespace internal
}  // namespace bwe
