#include "bwe/AimdAlgorithm.hpp"

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

TEST(AimdAlgorithmTest, FirstCallSeedsFromReceiveRateThenIncreases)
{
	bwe::AimdAlgorithm algorithm(1316);
	// Seeded at 10e6, then one additive-increase step of one packet.
	EXPECT_DOUBLE_EQ(algorithm.Estimate(MakeInput(100.0, 0.0, 10e6)), 10e6 + 1316.0 * 8.0);
}

TEST(AimdAlgorithmTest, NoDropsIncreaseAdditivelyEachCall)
{
	bwe::AimdAlgorithm algorithm(1316);
	const double first = algorithm.Estimate(MakeInput(100.0, 0.0, 10e6));
	const double second = algorithm.Estimate(MakeInput(100.0, 0.0, 10e6));
	EXPECT_DOUBLE_EQ(second, first + 1316.0 * 8.0);
}

TEST(AimdAlgorithmTest, AnyDropHalvesTheRate)
{
	bwe::AimdAlgorithm algorithm(1316);
	const double before_drop = algorithm.Estimate(MakeInput(100.0, 0.0, 10e6));
	const double after_drop = algorithm.Estimate(MakeInput(100.0, 1.0, 10e6));
	EXPECT_DOUBLE_EQ(after_drop, before_drop * 0.5);
}

TEST(AimdAlgorithmTest, DropMagnitudeDoesNotChangeTheDecrease)
{
	// Unlike TFRC, AIMD only cares whether there was a drop at all, not how much: a barely-lossy
	// and a heavily-lossy interval both just halve the rate.
	bwe::AimdAlgorithm light_loss(1316);
	bwe::AimdAlgorithm heavy_loss(1316);
	light_loss.Estimate(MakeInput(100.0, 0.0, 10e6));
	heavy_loss.Estimate(MakeInput(100.0, 0.0, 10e6));

	EXPECT_DOUBLE_EQ(light_loss.Estimate(MakeInput(100.0, 0.1, 10e6)), heavy_loss.Estimate(MakeInput(100.0, 50.0, 10e6)));
}

TEST(AimdAlgorithmTest, NeverBelowOnePacketPer64Seconds)
{
	bwe::AimdAlgorithm algorithm(1316);
	// Seeded at 0, then a drop halves 0 to 0; the floor must still hold.
	EXPECT_DOUBLE_EQ(algorithm.Estimate(MakeInput(100.0, 50.0, 0.0)), 1316.0 * 8.0 / 64.0);
}

TEST(AimdAlgorithmTest, IncreaseStepScalesWithPacketSize)
{
	bwe::AimdAlgorithm small(658);
	bwe::AimdAlgorithm large(1316);
	// Both seeded at 0 receive rate, so the result is exactly one increase step.
	EXPECT_DOUBLE_EQ(large.Estimate(MakeInput(100.0, 0.0, 0.0)), 2.0 * small.Estimate(MakeInput(100.0, 0.0, 0.0)));
}

TEST(AimdAlgorithmTest, RejectsInvalidPacketSize)
{
	EXPECT_THROW(bwe::AimdAlgorithm(0).Estimate(MakeInput(100.0, 1.0, 1e6)), std::invalid_argument);
}

TEST(AimdAlgorithmTest, RejectsInvalidInput)
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	bwe::AimdAlgorithm algorithm(1316);
	EXPECT_THROW(algorithm.Estimate(MakeInput(0.0, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(-1.0, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(nan, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(100.0, -0.1, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(100.0, 100.1, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(100.0, nan, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(100.0, 1.0, -1.0)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(100.0, 1.0, nan)), std::invalid_argument);
}
