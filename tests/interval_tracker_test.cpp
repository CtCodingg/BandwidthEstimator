#include "interval_tracker.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace bwe
{
namespace internal
{
namespace
{

using std::chrono::milliseconds;
using std::chrono::seconds;

SenderMeasurement SenderAt(Duration timestamp)
{
	SenderMeasurement measurement;
	measurement.common.timestamp = timestamp;
	return measurement;
}

ReceiverMeasurement ReceiverAt(Duration timestamp)
{
	ReceiverMeasurement measurement;
	measurement.common.timestamp = timestamp;
	return measurement;
}

TEST(SenderIntervalTrackerTest, FirstMeasurementYieldsNoSample)
{
	SenderIntervalTracker tracker;
	EXPECT_FALSE(tracker.Update(SenderAt(seconds(1))).has_value());
}

TEST(SenderIntervalTrackerTest, ComputesTimestampAndDuration)
{
	SenderIntervalTracker tracker;
	tracker.Update(SenderAt(seconds(1)));
	const auto sample = tracker.Update(SenderAt(milliseconds(1500)));

	ASSERT_TRUE(sample.has_value());
	EXPECT_EQ(sample->side, Side::kSender);
	EXPECT_EQ(sample->timestamp, milliseconds(1500));
	EXPECT_EQ(sample->duration, milliseconds(500));
}

TEST(SenderIntervalTrackerTest, ComputesThroughputFromUniqueBytes)
{
	SenderIntervalTracker tracker;
	auto first = SenderAt(seconds(0));
	first.bytes_sent_unique = 0;
	first.bytes_sent = 0;
	auto second = SenderAt(seconds(1));
	second.bytes_sent_unique = 125'000;
	second.bytes_sent = 250'000;

	tracker.Update(first);
	const auto sample = tracker.Update(second);

	ASSERT_TRUE(sample->throughput_bps.has_value());
	EXPECT_DOUBLE_EQ(*sample->throughput_bps, 1e6);
}

TEST(SenderIntervalTrackerTest, FallsBackToTotalBytes)
{
	SenderIntervalTracker tracker;
	auto first = SenderAt(seconds(0));
	first.bytes_sent = 0;
	auto second = SenderAt(seconds(2));
	second.bytes_sent = 250'000;

	tracker.Update(first);
	const auto sample = tracker.Update(second);

	ASSERT_TRUE(sample->throughput_bps.has_value());
	EXPECT_DOUBLE_EQ(*sample->throughput_bps, 1e6);
}

TEST(SenderIntervalTrackerTest, ThroughputExcludesLostShare)
{
	SenderIntervalTracker tracker;
	auto first = SenderAt(seconds(0));
	first.bytes_sent_unique = 0;
	first.packets_sent_unique = 0;
	first.packets_lost = 0;
	auto second = SenderAt(seconds(1));
	second.bytes_sent_unique = 125'000;
	second.packets_sent_unique = 100;
	second.packets_lost = 25;

	tracker.Update(first);
	EXPECT_DOUBLE_EQ(*tracker.Update(second)->throughput_bps, 0.75e6);
}

TEST(SenderIntervalTrackerTest, ComputesLossAndDropRate)
{
	SenderIntervalTracker tracker;
	auto first = SenderAt(seconds(0));
	first.packets_sent_unique = 1000;
	first.packets_lost = 10;
	first.packets_dropped = 0;
	auto second = SenderAt(seconds(1));
	second.packets_sent_unique = 1090;
	second.packets_lost = 19;
	second.packets_dropped = 10;

	tracker.Update(first);
	const auto sample = tracker.Update(second);

	EXPECT_DOUBLE_EQ(*sample->loss_rate, 0.1);
	EXPECT_DOUBLE_EQ(*sample->drop_rate, 0.1);
}

TEST(SenderIntervalTrackerTest, ClampsLossRateToOne)
{
	SenderIntervalTracker tracker;
	auto first = SenderAt(seconds(0));
	first.packets_sent = 0;
	first.packets_lost = 0;
	auto second = SenderAt(seconds(1));
	second.packets_sent = 10;
	second.packets_lost = 20;

	tracker.Update(first);
	EXPECT_DOUBLE_EQ(*tracker.Update(second)->loss_rate, 1.0);
}

TEST(SenderIntervalTrackerTest, LeavesRatesUnsetWithoutCounters)
{
	SenderIntervalTracker tracker;
	tracker.Update(SenderAt(seconds(0)));
	const auto sample = tracker.Update(SenderAt(seconds(1)));

	EXPECT_FALSE(sample->throughput_bps.has_value());
	EXPECT_FALSE(sample->loss_rate.has_value());
	EXPECT_FALSE(sample->drop_rate.has_value());
}

TEST(SenderIntervalTrackerTest, PassesThroughInstantaneousValues)
{
	SenderIntervalTracker tracker;
	tracker.Update(SenderAt(seconds(0)));
	auto second = SenderAt(seconds(1));
	second.common.rtt = milliseconds(40);
	second.common.link_capacity_bps = 20e6;
	second.common.mss_bytes = 1500;
	second.send_buffer_delay = milliseconds(120);
	second.max_bandwidth_bps = 10e6;

	const auto sample = tracker.Update(second);

	EXPECT_EQ(sample->rtt, milliseconds(40));
	EXPECT_EQ(sample->link_capacity_bps, 20e6);
	EXPECT_EQ(sample->mss_bytes, 1500u);
	EXPECT_EQ(sample->buffer_delay, milliseconds(120));
	EXPECT_EQ(sample->max_bandwidth_bps, 10e6);
	EXPECT_FALSE(sample->buffer_target.has_value());
	EXPECT_FALSE(sample->late_rate.has_value());
}

TEST(SenderIntervalTrackerTest, IgnoresNonIncreasingTimestamps)
{
	SenderIntervalTracker tracker;
	auto first = SenderAt(seconds(1));
	first.bytes_sent = 0;
	tracker.Update(first);

	auto older = SenderAt(milliseconds(500));
	older.bytes_sent = 999'999;
	EXPECT_FALSE(tracker.Update(older).has_value());
	EXPECT_FALSE(tracker.Update(SenderAt(seconds(1))).has_value());

	auto next = SenderAt(seconds(2));
	next.bytes_sent = 125'000;
	const auto sample = tracker.Update(next);

	ASSERT_TRUE(sample.has_value());
	EXPECT_EQ(sample->duration, seconds(1));
	EXPECT_DOUBLE_EQ(*sample->throughput_bps, 1e6);
}

TEST(SenderIntervalTrackerTest, DecreasingCounterStartsNewBaseline)
{
	SenderIntervalTracker tracker;
	auto first = SenderAt(seconds(0));
	first.bytes_sent = 1'000'000;
	auto reset = SenderAt(seconds(1));
	reset.bytes_sent = 1000;
	auto next = SenderAt(seconds(2));
	next.bytes_sent = 126'000;

	tracker.Update(first);
	const auto after_reset = tracker.Update(reset);
	ASSERT_TRUE(after_reset.has_value());
	EXPECT_FALSE(after_reset->throughput_bps.has_value());

	EXPECT_DOUBLE_EQ(*tracker.Update(next)->throughput_bps, 1e6);
}

TEST(SenderIntervalTrackerTest, ResetForgetsPreviousMeasurement)
{
	SenderIntervalTracker tracker;
	tracker.Update(SenderAt(seconds(0)));
	tracker.Reset();
	EXPECT_FALSE(tracker.Update(SenderAt(seconds(1))).has_value());
}

TEST(ReceiverIntervalTrackerTest, FirstMeasurementYieldsNoSample)
{
	ReceiverIntervalTracker tracker;
	EXPECT_FALSE(tracker.Update(ReceiverAt(seconds(1))).has_value());
}

TEST(ReceiverIntervalTrackerTest, ComputesRates)
{
	ReceiverIntervalTracker tracker;
	auto first = ReceiverAt(seconds(0));
	first.packets_received_unique = 0;
	first.packets_lost = 0;
	first.packets_dropped = 0;
	first.packets_belated = 0;
	first.bytes_received_unique = 0;
	auto second = ReceiverAt(seconds(1));
	second.packets_received_unique = 90;
	second.packets_lost = 10;
	second.packets_dropped = 5;
	second.packets_belated = 9;
	second.bytes_received_unique = 250'000;

	tracker.Update(first);
	const auto sample = tracker.Update(second);

	ASSERT_TRUE(sample.has_value());
	EXPECT_EQ(sample->side, Side::kReceiver);
	EXPECT_DOUBLE_EQ(*sample->throughput_bps, 2e6);
	EXPECT_DOUBLE_EQ(*sample->loss_rate, 0.1);
	EXPECT_DOUBLE_EQ(*sample->drop_rate, 0.05);
	EXPECT_DOUBLE_EQ(*sample->late_rate, 0.1);
}

TEST(ReceiverIntervalTrackerTest, PassesThroughBufferValues)
{
	ReceiverIntervalTracker tracker;
	tracker.Update(ReceiverAt(seconds(0)));
	auto second = ReceiverAt(seconds(1));
	second.receive_buffer_delay = milliseconds(80);
	second.tsbpd_delay = milliseconds(120);

	const auto sample = tracker.Update(second);

	EXPECT_EQ(sample->buffer_delay, milliseconds(80));
	EXPECT_EQ(sample->buffer_target, milliseconds(120));
	EXPECT_FALSE(sample->max_bandwidth_bps.has_value());
}

TEST(ReceiverIntervalTrackerTest, IgnoresNonIncreasingTimestamps)
{
	ReceiverIntervalTracker tracker;
	tracker.Update(ReceiverAt(seconds(1)));
	EXPECT_FALSE(tracker.Update(ReceiverAt(seconds(1))).has_value());
	EXPECT_FALSE(tracker.Update(ReceiverAt(seconds(0))).has_value());
	EXPECT_TRUE(tracker.Update(ReceiverAt(seconds(2))).has_value());
}

}  // namespace
}  // namespace internal
}  // namespace bwe
