#include "bwe/Estimator.hpp"
#include "bwe/Player.hpp"
#include "bwe/Recorder.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

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

}

TEST(RecordReplayTest, PlayerParsesRecordedEvents)
{
	std::stringstream file;
	{
		bwe::Recorder recorder(file);
		recorder.recordChannel(80.0, 2.5);
		recorder.recordStream(bwe::StreamInput{ 1, 3e6, 1.0 });
		recorder.recordStream(bwe::StreamInput{ 2, 1e6, 2.0 });
		recorder.recordRemove(1);
	}

	const bwe::Player player(file);
	ASSERT_EQ(player.events().size(), 4u);

	EXPECT_EQ(player.events()[0].step, 1u);
	EXPECT_EQ(player.events()[0].kind, bwe::EventKind::Channel);
	EXPECT_DOUBLE_EQ(player.events()[0].rttMs, 80.0);
	EXPECT_DOUBLE_EQ(player.events()[0].dropRatePercent, 2.5);

	EXPECT_EQ(player.events()[1].kind, bwe::EventKind::Stream);
	EXPECT_EQ(player.events()[1].stream.streamId, 1u);
	EXPECT_DOUBLE_EQ(player.events()[1].stream.receiveRateBps, 3e6);
	EXPECT_DOUBLE_EQ(player.events()[1].stream.weight, 1.0);

	EXPECT_EQ(player.events()[2].kind, bwe::EventKind::Stream);
	EXPECT_EQ(player.events()[2].stream.streamId, 2u);

	EXPECT_EQ(player.events()[3].kind, bwe::EventKind::Remove);
	EXPECT_EQ(player.events()[3].stream.streamId, 1u);
}

TEST(RecordReplayTest, ReplayFeedsEventsIntoEstimator)
{
	std::stringstream file;
	{
		bwe::Recorder recorder(file);
		recorder.recordChannel(50.0, 1.0);
		recorder.recordStream(bwe::StreamInput{ 1, 1e6, 1.0 });
		recorder.recordStream(bwe::StreamInput{ 2, 1e6, 2.0 });
	}

	const bwe::Player player(file);
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(900.0), TestIntervalMs);
	player.replay(estimator);

	const std::vector<bwe::Output> outputs = waitForOutputs(estimator, 2);
	ASSERT_EQ(outputs.size(), 2u);
	for (const bwe::Output& output : outputs)
	{
		EXPECT_DOUBLE_EQ(output.rateBps, output.streamId == 1u ? 300.0 : 600.0);
	}
}

TEST(RecordReplayTest, AcceptsWindowsLineEndingsAndEmptyLines)
{
	std::istringstream file(std::string(bwe::Recorder::Header) + "\r\n1,stream,2,0,0,1000000,1.5\r\n\r\n");
	const bwe::Player player(file);
	ASSERT_EQ(player.events().size(), 1u);
	EXPECT_EQ(player.events()[0].kind, bwe::EventKind::Stream);
	EXPECT_EQ(player.events()[0].stream.streamId, 2u);
	EXPECT_DOUBLE_EQ(player.events()[0].stream.receiveRateBps, 1000000.0);
	EXPECT_DOUBLE_EQ(player.events()[0].stream.weight, 1.5);
}

TEST(RecordReplayTest, RejectsInvalidFiles)
{
	const std::string header = std::string(bwe::Recorder::Header) + "\n";

	std::istringstream empty("");
	EXPECT_THROW(bwe::Player player(empty), std::runtime_error);

	std::istringstream wrongHeader("foo\n");
	EXPECT_THROW(bwe::Player player(wrongHeader), std::runtime_error);

	std::istringstream wrongCount(header + "1,channel,0,50,1\n");
	EXPECT_THROW(bwe::Player player(wrongCount), std::runtime_error);

	std::istringstream wrongValue(header + "1,channel,0,abc,1,0,0\n");
	EXPECT_THROW(bwe::Player player(wrongValue), std::runtime_error);

	std::istringstream unknownKind(header + "1,bogus,0,50,1,0,0\n");
	EXPECT_THROW(bwe::Player player(unknownKind), std::runtime_error);
}
