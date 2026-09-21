#include "bwe/TfrcAlgorithm.hpp"

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

TEST(TfrcAlgorithmTest, MatchesThroughputEquation)
{
	bwe::TfrcAlgorithm algorithm(1316);
	EXPECT_NEAR(algorithm.Estimate(MakeInput(100.0, 1.0, 10e6)), 1182634.0, 10.0);
}

TEST(TfrcAlgorithmTest, NoDropsAllowTwiceTheReceiveRate)
{
	bwe::TfrcAlgorithm algorithm(1316);
	EXPECT_DOUBLE_EQ(algorithm.Estimate(MakeInput(100.0, 0.0, 10e6)), 20e6);
}

TEST(TfrcAlgorithmTest, LimitedToTwiceTheReceiveRate)
{
	bwe::TfrcAlgorithm algorithm(1316);
	EXPECT_DOUBLE_EQ(algorithm.Estimate(MakeInput(100.0, 0.01, 1e6)), 2e6);
}

TEST(TfrcAlgorithmTest, NeverBelowOnePacketPer64Seconds)
{
	bwe::TfrcAlgorithm algorithm(1316);
	EXPECT_DOUBLE_EQ(algorithm.Estimate(MakeInput(100.0, 50.0, 0.0)), 1316.0 * 8.0 / 64.0);
}

TEST(TfrcAlgorithmTest, HigherDropRateGivesLowerRate)
{
	bwe::TfrcAlgorithm algorithm(1316);
	const double low_drops = algorithm.Estimate(MakeInput(100.0, 1.0, 100e6));
	const double high_drops = algorithm.Estimate(MakeInput(100.0, 5.0, 100e6));
	EXPECT_LT(high_drops, low_drops);
}

TEST(TfrcAlgorithmTest, RateScalesWithPacketSize)
{
	bwe::TfrcAlgorithm small(658);
	bwe::TfrcAlgorithm large(1316);
	const bwe::Input input = MakeInput(100.0, 1.0, 100e6);
	EXPECT_DOUBLE_EQ(large.Estimate(input), 2.0 * small.Estimate(input));
}

TEST(TfrcAlgorithmTest, RejectsInvalidPacketSize)
{
	EXPECT_THROW(bwe::TfrcAlgorithm(0).Estimate(MakeInput(100.0, 1.0, 1e6)), std::invalid_argument);
}

TEST(TfrcAlgorithmTest, RejectsInvalidInput)
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	bwe::TfrcAlgorithm algorithm(1316);
	EXPECT_THROW(algorithm.Estimate(MakeInput(0.0, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(-1.0, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(nan, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(100.0, -0.1, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(100.0, 100.1, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(100.0, nan, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(100.0, 1.0, -1.0)), std::invalid_argument);
	EXPECT_THROW(algorithm.Estimate(MakeInput(100.0, 1.0, nan)), std::invalid_argument);
}
