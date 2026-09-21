#include "bwe/Estimator.hpp"
#include "bwe/Player.hpp"
#include "bwe/Recorder.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

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

}

TEST(RecordReplayTest, PlayerParsesRecordedEvents)
{
	std::stringstream file;
	{
		bwe::Recorder recorder(file);
		recorder.RecordChannel(80.0, 2.5);
		recorder.RecordStream(bwe::StreamInput{ 1, 3e6, 1.0 });
		recorder.RecordStream(bwe::StreamInput{ 2, 1e6, 2.0 });
		recorder.RecordRemove(1);
	}

	const bwe::Player player(file);
	ASSERT_EQ(player.Events().size(), 4u);

	EXPECT_EQ(player.Events()[0].step, 1u);
	EXPECT_EQ(player.Events()[0].kind, bwe::EventKind::kChannel);
	EXPECT_DOUBLE_EQ(player.Events()[0].rtt_ms, 80.0);
	EXPECT_DOUBLE_EQ(player.Events()[0].drop_rate_percent, 2.5);

	EXPECT_EQ(player.Events()[1].kind, bwe::EventKind::kStream);
	EXPECT_EQ(player.Events()[1].stream.stream_id, 1u);
	EXPECT_DOUBLE_EQ(player.Events()[1].stream.receive_rate_bps, 3e6);
	EXPECT_DOUBLE_EQ(player.Events()[1].stream.weight, 1.0);

	EXPECT_EQ(player.Events()[2].kind, bwe::EventKind::kStream);
	EXPECT_EQ(player.Events()[2].stream.stream_id, 2u);

	EXPECT_EQ(player.Events()[3].kind, bwe::EventKind::kRemove);
	EXPECT_EQ(player.Events()[3].stream.stream_id, 1u);
}

TEST(RecordReplayTest, ReplayFeedsEventsIntoEstimator)
{
	std::stringstream file;
	{
		bwe::Recorder recorder(file);
		recorder.RecordChannel(50.0, 1.0);
		recorder.RecordStream(bwe::StreamInput{ 1, 1e6, 1.0 });
		recorder.RecordStream(bwe::StreamInput{ 2, 1e6, 2.0 });
	}

	const bwe::Player player(file);
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(900.0), kTestIntervalMs);
	player.Replay(estimator);

	const std::vector<bwe::Output> outputs = WaitForOutputs(estimator, 2);
	ASSERT_EQ(outputs.size(), 2u);
	for (const bwe::Output& output : outputs)
	{
		EXPECT_DOUBLE_EQ(output.rate_bps, output.stream_id == 1u ? 300.0 : 600.0);
	}
}

TEST(RecordReplayTest, AcceptsWindowsLineEndingsAndEmptyLines)
{
	std::istringstream file(std::string(bwe::Recorder::kHeader) + "\r\n1,stream,2,0,0,1000000,1.5,inf\r\n\r\n");
	const bwe::Player player(file);
	ASSERT_EQ(player.Events().size(), 1u);
	EXPECT_EQ(player.Events()[0].kind, bwe::EventKind::kStream);
	EXPECT_EQ(player.Events()[0].stream.stream_id, 2u);
	EXPECT_DOUBLE_EQ(player.Events()[0].stream.receive_rate_bps, 1000000.0);
	EXPECT_DOUBLE_EQ(player.Events()[0].stream.weight, 1.5);
	EXPECT_TRUE(std::isinf(player.Events()[0].stream.max_rate_bps));
}

TEST(RecordReplayTest, RejectsInvalidFiles)
{
	const std::string header = std::string(bwe::Recorder::kHeader) + "\n";

	std::istringstream empty("");
	EXPECT_THROW(bwe::Player player(empty), std::runtime_error);

	std::istringstream wrong_header("foo\n");
	EXPECT_THROW(bwe::Player player(wrong_header), std::runtime_error);

	std::istringstream wrong_count(header + "1,channel,0,50,1\n");
	EXPECT_THROW(bwe::Player player(wrong_count), std::runtime_error);

	std::istringstream wrong_value(header + "1,channel,0,abc,1,0,0,0\n");
	EXPECT_THROW(bwe::Player player(wrong_value), std::runtime_error);

	std::istringstream unknown_kind(header + "1,bogus,0,50,1,0,0,0\n");
	EXPECT_THROW(bwe::Player player(unknown_kind), std::runtime_error);
}
