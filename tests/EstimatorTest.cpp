#include "bwe/Estimator.hpp"
#include "bwe/TfrcAlgorithm.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

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

bwe::Input makeInput(bwe::StreamId streamId)
{
	bwe::Input input;
	input.streamId = streamId;
	input.rttMs = 50.0;
	input.dropRatePercent = 1.0;
	input.receiveRateBps = 5e6;
	return input;
}

}

TEST(EstimatorTest, DefaultConfigUsesTfrc)
{
	const bwe::Config config;
	bwe::Estimator estimator(config);
	bwe::TfrcAlgorithm tfrc(config.packetSizeBytes);
	const bwe::Input input = makeInput(1);
	EXPECT_DOUBLE_EQ(estimator.update(input).rateBps, tfrc.estimate(input));
}

TEST(EstimatorTest, UsesCustomAlgorithm)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1234.0));
	const bwe::Output output = estimator.update(makeInput(7));
	EXPECT_EQ(output.streamId, 7u);
	EXPECT_DOUBLE_EQ(output.rateBps, 1234.0);
}

TEST(EstimatorTest, NotifiesOnlyTheSenderOfTheStream)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1000.0));
	int calls1 = 0;
	int calls2 = 0;
	estimator.subscribe(1, [&calls1](const bwe::Output& output)
		{
			EXPECT_EQ(output.streamId, 1u);
			++calls1;
		});
	estimator.subscribe(2, [&calls2](const bwe::Output&)
		{
			++calls2;
		});

	estimator.update(makeInput(1));
	estimator.update(makeInput(1));
	estimator.update(makeInput(3));

	EXPECT_EQ(calls1, 2);
	EXPECT_EQ(calls2, 0);
}

TEST(EstimatorTest, UnsubscribeStopsNotifications)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1000.0));
	int calls = 0;
	estimator.subscribe(1, [&calls](const bwe::Output&)
		{
			++calls;
		});
	estimator.update(makeInput(1));
	estimator.unsubscribe(1);
	estimator.update(makeInput(1));
	EXPECT_EQ(calls, 1);
}

TEST(EstimatorTest, ObserverSeesInputAndOutput)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(500.0));
	bwe::Input seenInput;
	bwe::Output seenOutput;
	estimator.setObserver([&seenInput, &seenOutput](const bwe::Input& input, const bwe::Output& output)
		{
			seenInput = input;
			seenOutput = output;
		});
	estimator.update(makeInput(4));
	EXPECT_EQ(seenInput.streamId, 4u);
	EXPECT_DOUBLE_EQ(seenInput.rttMs, 50.0);
	EXPECT_EQ(seenOutput.streamId, 4u);
	EXPECT_DOUBLE_EQ(seenOutput.rateBps, 500.0);
}

TEST(EstimatorTest, RejectsInvalidArguments)
{
	std::unique_ptr<bwe::IAlgorithm> none;
	EXPECT_THROW(bwe::Estimator estimator(std::move(none)), std::invalid_argument);

	bwe::Config config;
	config.packetSizeBytes = 0;
	EXPECT_THROW(bwe::Estimator estimator(config), std::invalid_argument);

	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1.0));
	EXPECT_THROW(estimator.subscribe(1, bwe::Estimator::Callback()), std::invalid_argument);
}

TEST(EstimatorTest, IsThreadSafe)
{
	constexpr int ThreadCount = 4;
	constexpr int UpdatesPerThread = 1000;

	const bwe::Config config;
	bwe::Estimator estimator(config);
	std::atomic<int> observed(0);
	std::atomic<int> notified(0);
	estimator.setObserver([&observed](const bwe::Input&, const bwe::Output&)
		{
			++observed;
		});
	for (int i = 0; i < ThreadCount; ++i)
	{
		estimator.subscribe(static_cast<bwe::StreamId>(i), [&notified](const bwe::Output&)
			{
				++notified;
			});
	}

	std::vector<std::thread> threads;
	for (int i = 0; i < ThreadCount; ++i)
	{
		threads.emplace_back([&estimator, i, UpdatesPerThread]()
			{
				for (int n = 0; n < UpdatesPerThread; ++n)
				{
					estimator.update(makeInput(static_cast<bwe::StreamId>(i)));
				}
			});
	}
	for (std::thread& thread : threads)
	{
		thread.join();
	}

	EXPECT_EQ(observed.load(), ThreadCount * UpdatesPerThread);
	EXPECT_EQ(notified.load(), ThreadCount * UpdatesPerThread);
}
