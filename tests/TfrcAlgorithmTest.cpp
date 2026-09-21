#include "bwe/TfrcAlgorithm.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

namespace
{

bwe::Input makeInput(double rttMs, double dropRatePercent, double receiveRateBps)
{
	bwe::Input input;
	input.rttMs = rttMs;
	input.dropRatePercent = dropRatePercent;
	input.receiveRateBps = receiveRateBps;
	return input;
}

}

TEST(TfrcAlgorithmTest, MatchesThroughputEquation)
{
	bwe::TfrcAlgorithm algorithm(1316);
	EXPECT_NEAR(algorithm.estimate(makeInput(100.0, 1.0, 10e6)), 1182634.0, 10.0);
}

TEST(TfrcAlgorithmTest, NoDropsAllowTwiceTheReceiveRate)
{
	bwe::TfrcAlgorithm algorithm(1316);
	EXPECT_DOUBLE_EQ(algorithm.estimate(makeInput(100.0, 0.0, 10e6)), 20e6);
}

TEST(TfrcAlgorithmTest, LimitedToTwiceTheReceiveRate)
{
	bwe::TfrcAlgorithm algorithm(1316);
	EXPECT_DOUBLE_EQ(algorithm.estimate(makeInput(100.0, 0.01, 1e6)), 2e6);
}

TEST(TfrcAlgorithmTest, NeverBelowOnePacketPer64Seconds)
{
	bwe::TfrcAlgorithm algorithm(1316);
	EXPECT_DOUBLE_EQ(algorithm.estimate(makeInput(100.0, 50.0, 0.0)), 1316.0 * 8.0 / 64.0);
}

TEST(TfrcAlgorithmTest, HigherDropRateGivesLowerRate)
{
	bwe::TfrcAlgorithm algorithm(1316);
	const double lowDrops = algorithm.estimate(makeInput(100.0, 1.0, 100e6));
	const double highDrops = algorithm.estimate(makeInput(100.0, 5.0, 100e6));
	EXPECT_LT(highDrops, lowDrops);
}

TEST(TfrcAlgorithmTest, RateScalesWithPacketSize)
{
	bwe::TfrcAlgorithm small(658);
	bwe::TfrcAlgorithm large(1316);
	const bwe::Input input = makeInput(100.0, 1.0, 100e6);
	EXPECT_DOUBLE_EQ(large.estimate(input), 2.0 * small.estimate(input));
}

TEST(TfrcAlgorithmTest, RejectsInvalidPacketSize)
{
	EXPECT_THROW(bwe::TfrcAlgorithm(0).estimate(makeInput(100.0, 1.0, 1e6)), std::invalid_argument);
}

TEST(TfrcAlgorithmTest, RejectsInvalidInput)
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	bwe::TfrcAlgorithm algorithm(1316);
	EXPECT_THROW(algorithm.estimate(makeInput(0.0, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.estimate(makeInput(-1.0, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.estimate(makeInput(nan, 1.0, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.estimate(makeInput(100.0, -0.1, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.estimate(makeInput(100.0, 100.1, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.estimate(makeInput(100.0, nan, 1e6)), std::invalid_argument);
	EXPECT_THROW(algorithm.estimate(makeInput(100.0, 1.0, -1.0)), std::invalid_argument);
	EXPECT_THROW(algorithm.estimate(makeInput(100.0, 1.0, nan)), std::invalid_argument);
}
