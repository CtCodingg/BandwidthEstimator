#include "algorithms/mathis_estimator.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace bwe
{
namespace internal
{
namespace
{

using std::chrono::milliseconds;
using std::chrono::seconds;

// MSS 1500 bytes and RTT 100 ms give MSS * 8 / RTT = 120'000 bit/s.
constexpr double kBaseRateBps = 120'000.0;

IntervalSample MakeSample(double loss_rate)
{
	IntervalSample sample;
	sample.timestamp = seconds(1);
	sample.duration = seconds(1);
	sample.rtt = milliseconds(100);
	sample.mss_bytes = 1500;
	sample.loss_rate = loss_rate;
	return sample;
}

TEST(MathisParametersTest, UsesDefaultsForEmptyParameters)
{
	const MathisParameters parameters = MathisCore::ParseParameters({});
	EXPECT_DOUBLE_EQ(parameters.constant, 1.22);
	EXPECT_DOUBLE_EQ(parameters.min_loss_rate, 1e-4);
	EXPECT_DOUBLE_EQ(parameters.smoothing, 0.3);
}

TEST(MathisParametersTest, AcceptsValidValues)
{
	const MathisParameters parameters = MathisCore::ParseParameters(
		{{"constant", 1.0}, {"min_loss_rate", 0.01}, {"smoothing", 1.0}});
	EXPECT_DOUBLE_EQ(parameters.constant, 1.0);
	EXPECT_DOUBLE_EQ(parameters.min_loss_rate, 0.01);
	EXPECT_DOUBLE_EQ(parameters.smoothing, 1.0);
}

TEST(MathisParametersTest, RejectsUnknownKey)
{
	EXPECT_THROW(MathisCore::ParseParameters({{"konstant", 1.0}}),
				 std::invalid_argument);
}

TEST(MathisParametersTest, RejectsValuesOutOfRange)
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	EXPECT_THROW(MathisCore::ParseParameters({{"constant", 0.0}}),
				 std::out_of_range);
	EXPECT_THROW(MathisCore::ParseParameters({{"constant", 10.5}}),
				 std::out_of_range);
	EXPECT_THROW(MathisCore::ParseParameters({{"constant", nan}}),
				 std::out_of_range);
	EXPECT_THROW(MathisCore::ParseParameters({{"min_loss_rate", 0.0}}),
				 std::out_of_range);
	EXPECT_THROW(MathisCore::ParseParameters({{"min_loss_rate", 1.0}}),
				 std::out_of_range);
	EXPECT_THROW(MathisCore::ParseParameters({{"smoothing", 0.0}}),
				 std::out_of_range);
	EXPECT_THROW(MathisCore::ParseParameters({{"smoothing", 1.5}}),
				 std::out_of_range);
}

TEST(MathisCoreTest, IsInvalidWithoutSamples)
{
	MathisCore core(MathisParameters{});
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(MathisCoreTest, AppliesFormula)
{
	MathisCore core(MathisParameters{});
	core.Update(MakeSample(0.01));

	const BandwidthEstimate estimate = core.GetEstimate();
	EXPECT_TRUE(estimate.valid);
	EXPECT_EQ(estimate.timestamp, seconds(1));
	EXPECT_NEAR(estimate.bits_per_second, kBaseRateBps * 1.22 / 0.1, 1e-6);
}

TEST(MathisCoreTest, UsesMinimumLossRateWithoutLoss)
{
	MathisCore core(MathisParameters{});
	core.Update(MakeSample(0.0));
	EXPECT_NEAR(core.GetEstimate().bits_per_second,
				kBaseRateBps * 1.22 / std::sqrt(1e-4), 1e-6);
}

TEST(MathisCoreTest, UsesConfiguredConstant)
{
	MathisParameters parameters;
	parameters.constant = 1.0;
	MathisCore core(parameters);
	core.Update(MakeSample(0.04));
	EXPECT_NEAR(core.GetEstimate().bits_per_second, kBaseRateBps / 0.2, 1e-6);
}

TEST(MathisCoreTest, SmoothsEstimates)
{
	MathisParameters parameters;
	parameters.smoothing = 0.5;
	MathisCore core(parameters);
	core.Update(MakeSample(0.01));
	core.Update(MakeSample(0.04));

	const double first = kBaseRateBps * 1.22 / 0.1;
	const double second = kBaseRateBps * 1.22 / 0.2;
	EXPECT_NEAR(core.GetEstimate().bits_per_second, (first + second) / 2,
				1e-6);
}

TEST(MathisCoreTest, IgnoresIncompleteSamples)
{
	MathisCore core(MathisParameters{});

	IntervalSample without_rtt = MakeSample(0.01);
	without_rtt.rtt.reset();
	IntervalSample without_loss = MakeSample(0.01);
	without_loss.loss_rate.reset();
	IntervalSample without_mss = MakeSample(0.01);
	without_mss.mss_bytes.reset();
	IntervalSample zero_rtt = MakeSample(0.01);
	zero_rtt.rtt = milliseconds(0);

	core.Update(without_rtt);
	core.Update(without_loss);
	core.Update(without_mss);
	core.Update(zero_rtt);
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(MathisCoreTest, ConfidenceRisesWithSamples)
{
	MathisCore core(MathisParameters{});
	core.Update(MakeSample(0.01));
	const double first = core.GetEstimate().confidence;
	for (int i = 0; i < 10; ++i)
	{
		core.Update(MakeSample(0.01));
	}
	EXPECT_GT(first, 0.0);
	EXPECT_LT(first, 1.0);
	EXPECT_DOUBLE_EQ(core.GetEstimate().confidence, 1.0);
}

TEST(MathisCoreTest, ResetDiscardsEstimate)
{
	MathisCore core(MathisParameters{});
	core.Update(MakeSample(0.01));
	core.Reset();
	EXPECT_FALSE(core.GetEstimate().valid);
	EXPECT_DOUBLE_EQ(core.GetEstimate().confidence, 0.0);
}

TEST(MathisSenderEstimatorTest, EstimatesFromMeasurements)
{
	MathisSenderEstimator estimator(MathisParameters{});

	SenderMeasurement first;
	first.common.timestamp = seconds(0);
	first.packets_sent = 0;
	first.packets_lost = 0;
	SenderMeasurement second;
	second.common.timestamp = seconds(1);
	second.common.rtt = milliseconds(100);
	second.common.mss_bytes = 1500;
	second.packets_sent = 1000;
	second.packets_lost = 10;

	estimator.Update(first);
	EXPECT_FALSE(estimator.GetEstimate().valid);
	estimator.Update(second);

	EXPECT_TRUE(estimator.GetEstimate().valid);
	EXPECT_NEAR(estimator.GetEstimate().bits_per_second,
				kBaseRateBps * 1.22 / 0.1, 1e-6);
	EXPECT_EQ(estimator.Name(), "mathis");

	estimator.Reset();
	EXPECT_FALSE(estimator.GetEstimate().valid);
}

TEST(MathisReceiverEstimatorTest, EstimatesFromMeasurements)
{
	MathisReceiverEstimator estimator(MathisParameters{});

	ReceiverMeasurement first;
	first.common.timestamp = seconds(0);
	first.packets_received = 0;
	first.packets_lost = 0;
	ReceiverMeasurement second;
	second.common.timestamp = seconds(1);
	second.common.rtt = milliseconds(100);
	second.common.mss_bytes = 1500;
	second.packets_received = 990;
	second.packets_lost = 10;

	estimator.Update(first);
	estimator.Update(second);

	EXPECT_TRUE(estimator.GetEstimate().valid);
	EXPECT_NEAR(estimator.GetEstimate().bits_per_second,
				kBaseRateBps * 1.22 / 0.1, 1e-6);
	EXPECT_EQ(estimator.Name(), "mathis");
}

TEST(MathisRegistrationTest, IsAvailableInGlobalFactory)
{
	EstimatorFactory& factory = EstimatorFactory::Instance();
	ASSERT_TRUE(factory.HasSenderAlgorithm("mathis"));
	ASSERT_TRUE(factory.HasReceiverAlgorithm("mathis"));
	EXPECT_EQ(factory.CreateSender("mathis")->Name(), "mathis");
	EXPECT_EQ(factory.CreateReceiver("mathis")->Name(), "mathis");
	EXPECT_THROW(factory.CreateSender("mathis", {{"smoothing", 0.0}}),
				 std::out_of_range);
}

}  // namespace
}  // namespace internal
}  // namespace bwe
