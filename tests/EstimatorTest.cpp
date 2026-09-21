#include "bwe/Estimator.hpp"
#include "TfrcAlgorithm.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

// Fast on purpose: keeps the background-thread tests below quick without being flaky.
constexpr uint32_t kTestIntervalMs = 5;

class FixedAlgorithm : public bwe::IAlgorithm
{
public:
	explicit FixedAlgorithm(double rate_bps)
		: rate_bps_(rate_bps)
	{
	}

	double Estimate(const bwe::Input&) override
	{
		return rate_bps_;
	}

private:
	double rate_bps_;
};

class ThrowingAlgorithm : public bwe::IAlgorithm
{
public:
	double Estimate(const bwe::Input&) override
	{
		throw std::runtime_error("ThrowingAlgorithm always fails");
	}
};

/// Polls estimator.Outputs() until it has exactly @p expected_count entries or @p timeout elapses.
std::vector<bwe::Output> WaitForOutputs(const bwe::Estimator& estimator, size_t expected_count,
	std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	std::vector<bwe::Output> outputs;
	do
	{
		outputs = estimator.Outputs();
		if (outputs.size() == expected_count)
		{
			return outputs;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	} while (std::chrono::steady_clock::now() < deadline);
	return outputs;
}

bwe::StreamInput MakeStream(bwe::StreamId stream_id, double weight = 1.0)
{
	bwe::StreamInput stream;
	stream.stream_id = stream_id;
	stream.receive_rate_bps = 5e6;
	stream.weight = weight;
	return stream;
}

}

TEST(EstimatorTest, DefaultConfigUsesTfrc)
{
	bwe::Config config;
	config.update_interval_ms = kTestIntervalMs;
	bwe::Estimator estimator(config);
	bwe::TfrcAlgorithm tfrc(config.packet_size_bytes);

	estimator.UpdateChannel(50.0, 1.0);
	estimator.UpdateStream(MakeStream(1));

	bwe::Input algorithm_input;
	algorithm_input.rtt_ms = 50.0;
	algorithm_input.drop_rate_percent = 1.0;
	algorithm_input.receive_rate_bps = 5e6;

	const std::vector<bwe::Output> outputs = WaitForOutputs(estimator, 1);
	ASSERT_EQ(outputs.size(), 1u);
	EXPECT_DOUBLE_EQ(outputs[0].rate_bps, tfrc.Estimate(algorithm_input));
}

TEST(EstimatorTest, UsesCustomAlgorithm)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1234.0), kTestIntervalMs);
	estimator.UpdateChannel(50.0, 1.0);
	estimator.UpdateStream(MakeStream(7));

	const std::vector<bwe::Output> outputs = WaitForOutputs(estimator, 1);
	ASSERT_EQ(outputs.size(), 1u);
	EXPECT_EQ(outputs[0].stream_id, 7u);
	EXPECT_DOUBLE_EQ(outputs[0].rate_bps, 1234.0);
}

TEST(EstimatorTest, SplitsTheChannelByWeight)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1200.0), kTestIntervalMs);
	estimator.UpdateChannel(50.0, 1.0);
	estimator.UpdateStream(bwe::StreamInput{ 1, 1e6, 1.0 });
	estimator.UpdateStream(bwe::StreamInput{ 2, 1e6, 2.0 });

	const std::vector<bwe::Output> outputs = WaitForOutputs(estimator, 2);
	ASSERT_EQ(outputs.size(), 2u);
	for (const bwe::Output& output : outputs)
	{
		EXPECT_DOUBLE_EQ(output.rate_bps, output.stream_id == 1u ? 400.0 : 800.0);
	}
}

TEST(EstimatorTest, ClampsToMaxRate)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1200.0), kTestIntervalMs);
	estimator.UpdateChannel(50.0, 1.0);
	bwe::StreamInput uncapped{ 1, 1e6, 1.0 };
	bwe::StreamInput capped{ 2, 1e6, 1.0 };
	capped.max_rate_bps = 300.0; // below its 600.0 fair share
	estimator.UpdateStream(uncapped);
	estimator.UpdateStream(capped);

	const std::vector<bwe::Output> outputs = WaitForOutputs(estimator, 2);
	ASSERT_EQ(outputs.size(), 2u);
	for (const bwe::Output& output : outputs)
	{
		EXPECT_DOUBLE_EQ(output.rate_bps, output.stream_id == 1u ? 600.0 : 300.0);
	}
}

TEST(EstimatorTest, RemoveStreamFreesItsShare)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(900.0), kTestIntervalMs);
	estimator.UpdateChannel(50.0, 1.0);
	estimator.UpdateStream(bwe::StreamInput{ 1, 1e6, 1.0 });
	estimator.UpdateStream(bwe::StreamInput{ 2, 1e6, 2.0 });
	ASSERT_EQ(WaitForOutputs(estimator, 2).size(), 2u);

	estimator.RemoveStream(2);
	const std::vector<bwe::Output> outputs = WaitForOutputs(estimator, 1);
	ASSERT_EQ(outputs.size(), 1u);
	EXPECT_EQ(outputs[0].stream_id, 1u);
	EXPECT_DOUBLE_EQ(outputs[0].rate_bps, 900.0);
}

TEST(EstimatorTest, RemovingLastStreamEmptiesOutputs)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(900.0), kTestIntervalMs);
	estimator.UpdateChannel(50.0, 1.0);
	estimator.UpdateStream(MakeStream(1));
	ASSERT_EQ(WaitForOutputs(estimator, 1).size(), 1u);

	estimator.RemoveStream(1);
	EXPECT_TRUE(WaitForOutputs(estimator, 0).empty());
}

TEST(EstimatorTest, StreamNeverExpiresWhenTimeoutIsZero)
{
	// stream_timeout_ms defaults to 0, i.e. disabled.
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(900.0), kTestIntervalMs);
	estimator.UpdateChannel(50.0, 1.0);
	estimator.UpdateStream(MakeStream(1));
	ASSERT_EQ(WaitForOutputs(estimator, 1).size(), 1u);

	std::this_thread::sleep_for(std::chrono::milliseconds(20 * kTestIntervalMs));
	EXPECT_EQ(estimator.Outputs().size(), 1u);
}

TEST(EstimatorTest, StreamTimesOutWhenNotUpdated)
{
	// A generous multiple of kTestIntervalMs so the initial "is present" check isn't flaky under
	// a slow (e.g. debug) build or scheduling jitter on the freshly started background thread.
	constexpr uint32_t kTimeoutMs = 20 * kTestIntervalMs;
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(900.0), kTestIntervalMs, kTimeoutMs);
	estimator.UpdateChannel(50.0, 1.0);
	estimator.UpdateStream(MakeStream(1));
	ASSERT_EQ(WaitForOutputs(estimator, 1).size(), 1u);

	// No further UpdateStream() calls: the stream must be dropped once it goes stale, as if
	// RemoveStream() had been called.
	EXPECT_TRUE(WaitForOutputs(estimator, 0).empty());
}

TEST(EstimatorTest, UpdateStreamResetsTheTimeout)
{
	constexpr uint32_t kTimeoutMs = 20 * kTestIntervalMs;
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(900.0), kTestIntervalMs, kTimeoutMs);
	estimator.UpdateChannel(50.0, 1.0);
	estimator.UpdateStream(MakeStream(1));
	ASSERT_EQ(WaitForOutputs(estimator, 1).size(), 1u);

	// Keep refreshing well within the timeout; the stream must never be evicted.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3 * kTimeoutMs);
	do
	{
		estimator.UpdateStream(MakeStream(1));
		std::this_thread::sleep_for(std::chrono::milliseconds(kTimeoutMs / 3));
		ASSERT_EQ(estimator.Outputs().size(), 1u);
	} while (std::chrono::steady_clock::now() < deadline);
}

TEST(EstimatorTest, OutputsAreEmptyUntilChannelAndStreamAreSet)
{
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1.0), kTestIntervalMs);
	std::this_thread::sleep_for(std::chrono::milliseconds(5 * kTestIntervalMs));
	EXPECT_TRUE(estimator.Outputs().empty());

	estimator.UpdateStream(MakeStream(1));
	std::this_thread::sleep_for(std::chrono::milliseconds(5 * kTestIntervalMs));
	EXPECT_TRUE(estimator.Outputs().empty()); // channel condition still unknown
}

TEST(EstimatorTest, AlgorithmExceptionDoesNotCrashTheBackgroundThread)
{
	bwe::Estimator estimator(std::make_unique<ThrowingAlgorithm>(), kTestIntervalMs);
	estimator.UpdateChannel(50.0, 1.0);
	estimator.UpdateStream(MakeStream(1));
	std::this_thread::sleep_for(std::chrono::milliseconds(20 * kTestIntervalMs));
	EXPECT_TRUE(estimator.Outputs().empty());
}

TEST(EstimatorTest, RejectsInvalidArguments)
{
	std::unique_ptr<bwe::IAlgorithm> none;
	EXPECT_THROW(bwe::Estimator estimator(std::move(none)), std::invalid_argument);
	EXPECT_THROW(bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1.0), 0), std::invalid_argument);

	bwe::Config config;
	config.packet_size_bytes = 0;
	EXPECT_THROW(bwe::Estimator estimator(config), std::invalid_argument);

	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(1.0), kTestIntervalMs);
	EXPECT_THROW(estimator.UpdateChannel(0.0, 1.0), std::invalid_argument);
	EXPECT_THROW(estimator.UpdateChannel(50.0, -1.0), std::invalid_argument);
	EXPECT_THROW(estimator.UpdateChannel(50.0, 100.1), std::invalid_argument);
	EXPECT_THROW(estimator.UpdateStream(bwe::StreamInput{ 1, -1.0, 1.0 }), std::invalid_argument);
	EXPECT_THROW(estimator.UpdateStream(bwe::StreamInput{ 1, 1e6, 0.0 }), std::invalid_argument);
	bwe::StreamInput zero_cap{ 1, 1e6, 1.0 };
	zero_cap.max_rate_bps = 0.0;
	EXPECT_THROW(estimator.UpdateStream(zero_cap), std::invalid_argument);
}

TEST(EstimatorTest, IsThreadSafe)
{
	constexpr int kThreadCount = 4;
	constexpr int kUpdatesPerThread = 200;

	bwe::Config config;
	config.update_interval_ms = kTestIntervalMs;
	bwe::Estimator estimator(config);
	estimator.UpdateChannel(50.0, 1.0);

	std::vector<std::thread> threads;
	for (int i = 0; i < kThreadCount; ++i)
	{
		threads.emplace_back([&estimator, i, kUpdatesPerThread]()
			{
				for (int n = 0; n < kUpdatesPerThread; ++n)
				{
					estimator.UpdateStream(MakeStream(static_cast<bwe::StreamId>(i)));
					(void)estimator.Outputs();
				}
			});
	}
	for (std::thread& thread : threads)
	{
		thread.join();
	}

	const std::vector<bwe::Output> outputs = WaitForOutputs(estimator, kThreadCount);
	EXPECT_EQ(outputs.size(), static_cast<size_t>(kThreadCount));
}
