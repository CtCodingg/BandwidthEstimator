#include "RttTrendAlgorithm.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

namespace
{

bwe::Input MakeInput(double rtt_ms, double drop_rate_percent, double receive_rate_bps)
{
	bwe::Input input;
	input.rtt_ms = rtt_ms;
	input.drop_rate_percent = drop_rate_percent;
	input.receive_rate_bps = receive_rate_bps;
	return input;
}

}

TEST(RttTrendAlgorithmTest, FirstCallSeedsFromReceiveRateThenIncreases)
{
	bwe::RttTrendAlgorithm algorithm(1316);
	// First call establishes the RTT baseline too, so queue delay is 0: no overuse.
	EXPECT_DOUBLE_EQ(algorithm.Estimate(MakeInput(40.0, 0.0, 10e6)), 10e6 + 1316.0 * 8.0);
}

TEST(RttTrendAlgorithmTest, StableRttAndNoLossIncreaseAdditivelyEachCall)
{
	bwe::RttTrendAlgorithm algorithm(1316);
	const double first = algorithm.Estimate(MakeInput(40.0, 0.0, 10e6));
	const double second = algorithm.Estimate(MakeInput(40.0, 0.0, 10e6));
	EXPECT_DOUBLE_EQ(second, first + 1316.0 * 8.0);
}

TEST(RttTrendAlgorithmTest, ModerateBackgroundLossDoesNotTriggerADecrease)
{
	// The key difference from AimdAlgorithm/TfrcAlgorithm: on its own, ordinary loss (e.g.
	// corruption from radio interference) is not treated as congestion.
	bwe::RttTrendAlgorithm algorithm(1316);
	const double before = algorithm.Estimate(MakeInput(40.0, 0.0, 10e6));
	const double after = algorithm.Estimate(MakeInput(40.0, 8.0, 10e6)); // below the loss backstop
	EXPECT_DOUBLE_EQ(after, before + 1316.0 * 8.0);
}

TEST(RttTrendAlgorithmTest, RisingRttAboveBaselineTriggersADecrease)
{
	bwe::RttTrendAlgorithm algorithm(1316);
	const double baseline = algorithm.Estimate(MakeInput(40.0, 0.0, 10e6));
	// 31 ms above the 40 ms baseline established above: over the 30 ms queueing threshold.
	const double overuse = algorithm.Estimate(MakeInput(71.0, 0.0, 10e6));
	EXPECT_DOUBLE_EQ(overuse, baseline * 0.85);
}

TEST(RttTrendAlgorithmTest, SevereLossTriggersADecreaseEvenWithoutRttRise)
{
	// Backstop for a link with too little buffering to show queueing delay before it drops.
	bwe::RttTrendAlgorithm algorithm(1316);
	const double baseline = algorithm.Estimate(MakeInput(40.0, 0.0, 10e6));
	const double severe_loss = algorithm.Estimate(MakeInput(40.0, 15.0, 10e6)); // above the backstop
	EXPECT_DOUBLE_EQ(severe_loss, baseline * 0.85);
}

TEST(RttTrendAlgorithmTest, BaselineTracksTheLowestRttSeen)
{
	bwe::RttTrendAlgorithm algorithm(1316);
	algorithm.Estimate(MakeInput(50.0, 0.0, 10e6));
	const double lower_baseline = algorithm.Estimate(MakeInput(35.0, 0.0, 10e6)); // new, lower baseline
	// 40 ms above the new 35 ms baseline (not the original 50 ms) is over the threshold.
	const double overuse = algorithm.Estimate(MakeInput(75.0, 0.0, 10e6));
	EXPECT_DOUBLE_EQ(overuse, lower_baseline * 0.85);
}

TEST(RttTrendAlgorithmTest, NeverBelowOnePacketPer64Seconds)
{
	bwe::RttTrendAlgorithm algorithm(1316);
	// Seeded at 0, then severe loss halves-ish 0 to 0; the floor must still hold.
	EXPECT_DOUBLE_EQ(algorithm.Estimate(MakeInput(40.0, 50.0, 0.0)), 1316.0 * 8.0 / 64.0);
}

TEST(RttTrendAlgorithmTest, IncreaseStepScalesWithPacketSize)
{
	bwe::RttTrendAlgorithm small(658);
	bwe::RttTrendAlgorithm large(1316);
	// Both seeded at 0 receive rate, so the result is exactly one increase step.
	EXPECT_DOUBLE_EQ(large.Estimate(MakeInput(40.0, 0.0, 0.0)), 2.0 * small.Estimate(MakeInput(40.0, 0.0, 0.0)));
}

TEST(RttTrendAlgorithmTest, RealisticRadioLinkSession)
{
	// Five consecutive 100 ms updates on a radio link (packet size 1316 bytes) starting at a
	// 5 Mbit/s receive rate: ordinary background loss from interference (2-4%) throughout, which
	// never triggers a decrease on its own; a real congestion event (another sender saturates the
	// channel, RTT climbs well above baseline) does trigger one; then it recovers.
	bwe::RttTrendAlgorithm algorithm(1316);
	constexpr double kIncreaseStepBps = 1316.0 * 8.0;
	constexpr double kDecreaseFactor = 0.85;

	const double step1 = algorithm.Estimate(MakeInput(/*rtt_ms=*/40.0, /*drop_rate_percent=*/2.0, 5'000'000.0));
	EXPECT_DOUBLE_EQ(step1, 5'000'000.0 + kIncreaseStepBps);

	const double step2 = algorithm.Estimate(MakeInput(42.0, 3.0, step1)); // jitter, still near baseline
	EXPECT_DOUBLE_EQ(step2, step1 + kIncreaseStepBps);

	const double step3 = algorithm.Estimate(MakeInput(38.0, 2.0, step2)); // new, lower baseline
	EXPECT_DOUBLE_EQ(step3, step2 + kIncreaseStepBps);

	// Congestion: another sender floods the channel. RTT jumps 37 ms above the 38 ms baseline;
	// loss (4%) alone would not have been enough to trigger this.
	const double step4 = algorithm.Estimate(MakeInput(75.0, 4.0, step3));
	EXPECT_DOUBLE_EQ(step4, step3 * kDecreaseFactor);

	const double step5 = algorithm.Estimate(MakeInput(40.0, 2.0, step4)); // recovered, near baseline again
	EXPECT_DOUBLE_EQ(step5, step4 + kIncreaseStepBps);
}

TEST(RttTrendAlgorithmTest, RejectsInvalidPacketSize)
{
	EXPECT_THROW(bwe::RttTrendAlgorithm(0).Estimate(MakeInput(40.0, 1.0, 1e6)), std::invalid_argument);
}

TEST(RttTrendAlgorithmTest, RejectsInvalidInput)
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	bwe::RttTrendAlgorithm algorithm(1316);
	EXPECT_THROW(algorithm.Estimate(MakeInput(0.0, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(-1.0, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(nan, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(40.0, -0.1, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(40.0, 100.1, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(40.0, nan, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(40.0, 1.0, -1.0)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(40.0, 1.0, nan)), std::invalid_argument);
}
