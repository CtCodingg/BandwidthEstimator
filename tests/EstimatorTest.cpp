#include "bwe/Estimator.hpp"
#include "bwe/TfrcAlgorithm.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

// Fast on purpose: keeps the background-thread tests below quick without being flaky.
constexpr std::uint32_t TestIntervalMs = 5;

class FixedAlgorithm : public bwe::IAlgorithm
{
public:
	explicit FixedAlgorithm(double rateBps)
		: m_rateBps(rateBps)
	{
	}

	double estimate(const bwe::Input&) override
	{
		return m_rateBps;
	}

private:
	double m_rateBps;
};

class ThrowingAlgorithm : public bwe::IAlgorithm
{
public:
	double estimate(const bwe::Input&) override
	{
		throw std::runtime_error("ThrowingAlgorithm always fails");
	}
};

/// Polls estimator.outputs() until it has exactly @p expectedCount entries or @p timeout elapses.
std::vector<bwe::Output> waitForOutputs(const bwe::Estimator& estimator, std::size_t expectedCount,
	std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	std::vector<bwe::Output> outputs;
	do
	{
		outputs = estimator.outputs();
		if (outputs.size() == expectedCount)
		{
			return outputs;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	} while (std::chrono::steady_clock::now() < deadline);
	return outputs;
}

bwe::StreamInput makeStream(bwe::StreamId streamId, double weight = 1.0)
{
	bwe::StreamInput stream;
	stream.streamId = streamId;
	stream.receiveRateBps = 5e6;
	stream.weight = weight;
	return stream;
}

}

TEST(EstimatorTest, DefaultConfigUsesTfrc)
{
	bwe::Config config;
	config.updateIntervalMs = TestIntervalMs;
	bwe::Estimator estimator(config);
	bwe::TfrcAlgorithm tfrc(config.packetSizeBytes);

	estimator.updateChannel(50.0, 1.0);
	estimator.updateStream(makeStream(1));

	bwe::Input algorithmInput;
	algorithmInput.rttMs = 50.0;
	algorithmInput.dropRatePercent = 1.0;
	algorithmInput.receiveRateBps = 5e6;

	const std::vector<bwe::Output> outputs = waitForOutputs(estimator, 1);
	ASSERT_EQ(outputs.size(), 1u);
	EXPECT_DOUBLE_EQ(outputs[0].rateBps, tfrc.estimate(algorithmInput));
}

TEST(EstimatorTest, UsesCustomAlgorithm)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1234.0), TestIntervalMs);
	estimator.updateChannel(50.0, 1.0);
	estimator.updateStream(makeStream(7));

	const std::vector<bwe::Output> outputs = waitForOutputs(estimator, 1);
	ASSERT_EQ(outputs.size(), 1u);
	EXPECT_EQ(outputs[0].streamId, 7u);
	EXPECT_DOUBLE_EQ(outputs[0].rateBps, 1234.0);
}

TEST(EstimatorTest, SplitsTheChannelByWeight)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1200.0), TestIntervalMs);
	estimator.updateChannel(50.0, 1.0);
	estimator.updateStream(bwe::StreamInput{ 1, 1e6, 1.0 });
	estimator.updateStream(bwe::StreamInput{ 2, 1e6, 2.0 });

	const std::vector<bwe::Output> outputs = waitForOutputs(estimator, 2);
	ASSERT_EQ(outputs.size(), 2u);
	for (const bwe::Output& output : outputs)
	{
		EXPECT_DOUBLE_EQ(output.rateBps, output.streamId == 1u ? 400.0 : 800.0);
	}
}

TEST(EstimatorTest, RemoveStreamFreesItsShare)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(900.0), TestIntervalMs);
	estimator.updateChannel(50.0, 1.0);
	estimator.updateStream(bwe::StreamInput{ 1, 1e6, 1.0 });
	estimator.updateStream(bwe::StreamInput{ 2, 1e6, 2.0 });
	ASSERT_EQ(waitForOutputs(estimator, 2).size(), 2u);

	estimator.removeStream(2);
	const std::vector<bwe::Output> outputs = waitForOutputs(estimator, 1);
	ASSERT_EQ(outputs.size(), 1u);
	EXPECT_EQ(outputs[0].streamId, 1u);
	EXPECT_DOUBLE_EQ(outputs[0].rateBps, 900.0);
}

TEST(EstimatorTest, OutputsAreEmptyUntilChannelAndStreamAreSet)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1.0), TestIntervalMs);
	std::this_thread::sleep_for(std::chrono::milliseconds(5 * TestIntervalMs));
	EXPECT_TRUE(estimator.outputs().empty());

	estimator.updateStream(makeStream(1));
	std::this_thread::sleep_for(std::chrono::milliseconds(5 * TestIntervalMs));
	EXPECT_TRUE(estimator.outputs().empty()); // channel condition still unknown
}

TEST(EstimatorTest, AlgorithmExceptionDoesNotCrashTheBackgroundThread)
{
	bwe::Estimator estimator(std::make_unique<ThrowingAlgorithm>(), TestIntervalMs);
	estimator.updateChannel(50.0, 1.0);
	estimator.updateStream(makeStream(1));
	std::this_thread::sleep_for(std::chrono::milliseconds(20 * TestIntervalMs));
	EXPECT_TRUE(estimator.outputs().empty());
}

TEST(EstimatorTest, RejectsInvalidArguments)
{
	std::unique_ptr<bwe::IAlgorithm> none;
	EXPECT_THROW(bwe::Estimator estimator(std::move(none)), std::invalid_argument);
	EXPECT_THROW(bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1.0), 0), std::invalid_argument);

	bwe::Config config;
	config.packetSizeBytes = 0;
	EXPECT_THROW(bwe::Estimator estimator(config), std::invalid_argument);

	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1.0), TestIntervalMs);
	EXPECT_THROW(estimator.updateChannel(0.0, 1.0), std::invalid_argument);
	EXPECT_THROW(estimator.updateChannel(50.0, -1.0), std::invalid_argument);
	EXPECT_THROW(estimator.updateChannel(50.0, 100.1), std::invalid_argument);
	EXPECT_THROW(estimator.updateStream(bwe::StreamInput{ 1, -1.0, 1.0 }), std::invalid_argument);
	EXPECT_THROW(estimator.updateStream(bwe::StreamInput{ 1, 1e6, 0.0 }), std::invalid_argument);
}

TEST(EstimatorTest, IsThreadSafe)
{
	constexpr int ThreadCount = 4;
	constexpr int UpdatesPerThread = 200;

	bwe::Config config;
	config.updateIntervalMs = TestIntervalMs;
	bwe::Estimator estimator(config);
	estimator.updateChannel(50.0, 1.0);

	std::vector<std::thread> threads;
	for (int i = 0; i < ThreadCount; ++i)
	{
		threads.emplace_back([&estimator, i, UpdatesPerThread]()
			{
				for (int n = 0; n < UpdatesPerThread; ++n)
				{
					estimator.updateStream(makeStream(static_cast<bwe::StreamId>(i)));
					(void)estimator.outputs();
				}
			});
	}
	for (std::thread& thread : threads)
	{
		thread.join();
	}

	const std::vector<bwe::Output> outputs = waitForOutputs(estimator, ThreadCount);
	EXPECT_EQ(outputs.size(), static_cast<std::size_t>(ThreadCount));
}
