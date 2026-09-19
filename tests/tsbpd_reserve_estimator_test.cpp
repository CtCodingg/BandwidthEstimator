#include "algorithms/tsbpd_reserve_estimator.hpp"

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

IntervalSample MakeSample(milliseconds buffer_delay, milliseconds latency,
						  double throughput_bps)
{
	IntervalSample sample;
	sample.side = Side::kReceiver;
	sample.timestamp = std::chrono::seconds(1);
	sample.duration = std::chrono::seconds(1);
	sample.buffer_delay = buffer_delay;
	sample.buffer_target = latency;
	sample.throughput_bps = throughput_bps;
	return sample;
}

TsbpdReserveCore MakeUnsmoothedCore()
{
	TsbpdReserveParameters parameters;
	parameters.smoothing = 1.0;
	parameters.capacity_window = 1;
	return TsbpdReserveCore(parameters);
}

TEST(TsbpdReserveParametersTest, UsesDefaultsForEmptyParameters)
{
	const TsbpdReserveParameters parameters =
		TsbpdReserveCore::ParseParameters({});
	EXPECT_DOUBLE_EQ(parameters.min_reserve, 0.5);
	EXPECT_DOUBLE_EQ(parameters.late_threshold, 0.01);
	EXPECT_DOUBLE_EQ(parameters.backoff, 0.9);
	EXPECT_DOUBLE_EQ(parameters.headroom, 1.2);
	EXPECT_EQ(parameters.capacity_window, 5);
	EXPECT_DOUBLE_EQ(parameters.smoothing, 0.3);
}

TEST(TsbpdReserveParametersTest, RejectsInvalidParameters)
{
	EXPECT_THROW(TsbpdReserveCore::ParseParameters({{"unknown", 1.0}}),
				 std::invalid_argument);
	EXPECT_THROW(TsbpdReserveCore::ParseParameters({{"min_reserve", 0.0}}),
				 std::out_of_range);
	EXPECT_THROW(TsbpdReserveCore::ParseParameters({{"late_threshold", 1.0}}),
				 std::out_of_range);
}

TEST(TsbpdReserveCoreTest, IgnoresIncompleteAndSenderSamples)
{
	TsbpdReserveCore core = MakeUnsmoothedCore();
	IntervalSample sender = MakeSample(milliseconds(120), milliseconds(120), 1e6);
	sender.side = Side::kSender;
	IntervalSample zero_latency =
		MakeSample(milliseconds(120), milliseconds(0), 1e6);
	IntervalSample without_throughput =
		MakeSample(milliseconds(120), milliseconds(120), 1e6);
	without_throughput.throughput_bps.reset();

	core.Update(sender);
	core.Update(zero_latency);
	core.Update(without_throughput);
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(TsbpdReserveCoreTest, UsesHeadroomWithFullReserve)
{
	TsbpdReserveCore core = MakeUnsmoothedCore();
	core.Update(MakeSample(milliseconds(120), milliseconds(120), 1e6));
	EXPECT_TRUE(core.GetEstimate().valid);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 1.2e6);
}

TEST(TsbpdReserveCoreTest, UsesLinkCapacityWithFullReserve)
{
	TsbpdReserveCore core = MakeUnsmoothedCore();
	IntervalSample sample =
		MakeSample(milliseconds(120), milliseconds(120), 1e6);
	sample.link_capacity_bps = 5e6;
	core.Update(sample);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 5e6);
}

TEST(TsbpdReserveCoreTest, BacksOffWithLowReserve)
{
	TsbpdReserveCore core = MakeUnsmoothedCore();
	core.Update(MakeSample(milliseconds(40), milliseconds(120), 1e6));
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 0.9e6);
}

TEST(TsbpdReserveCoreTest, BacksOffWithLatePackets)
{
	TsbpdReserveCore core = MakeUnsmoothedCore();
	IntervalSample sample =
		MakeSample(milliseconds(120), milliseconds(120), 1e6);
	sample.late_rate = 0.015;
	core.Update(sample);
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 0.9e6);
}

TEST(TsbpdReserveCoreTest, ResetDiscardsEstimate)
{
	TsbpdReserveCore core = MakeUnsmoothedCore();
	core.Update(MakeSample(milliseconds(120), milliseconds(120), 1e6));
	core.Reset();
	EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(TsbpdReserveRegistrationTest, IsAvailableOnReceiverSideOnly)
{
	EstimatorFactory& factory = EstimatorFactory::Instance();
	EXPECT_EQ(factory.CreateReceiver("tsbpd_reserve")->Name(),
			  "tsbpd_reserve");
	EXPECT_FALSE(factory.HasSenderAlgorithm("tsbpd_reserve"));
}

TEST(TsbpdReserveCoreTest, SuppressesCapacityOutliers)
{
	TsbpdReserveParameters parameters;
	parameters.smoothing = 1.0;
	parameters.capacity_window = 3;
	TsbpdReserveCore core(parameters);
	IntervalSample sample = MakeSample(milliseconds(120), milliseconds(120), 1e6);
	for (double capacity : {5e6, 20e6, 5e6})
	{
		sample.link_capacity_bps = capacity;
		core.Update(sample);
	}
	EXPECT_DOUBLE_EQ(core.GetEstimate().bits_per_second, 5e6);
}

TEST(TsbpdReserveParametersTest, RejectsNonIntegralCapacityWindow)
{
	EXPECT_THROW(TsbpdReserveCore::ParseParameters({{"capacity_window", 2.5}}),
				 std::invalid_argument);
	EXPECT_THROW(TsbpdReserveCore::ParseParameters({{"capacity_window", 0.0}}),
				 std::out_of_range);
}

}  // namespace
}  // namespace internal
}  // namespace bwe
