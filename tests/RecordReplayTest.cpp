#include "bwe/Estimator.hpp"
#include "bwe/Player.hpp"
#include "bwe/Recorder.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{

bwe::Input makeInput(std::size_t n)
{
	bwe::Input input;
	input.streamId = static_cast<bwe::StreamId>(n % 3);
	input.rttMs = 20.0 + 7.3 * static_cast<double>(n);
	input.dropRatePercent = 0.37 * static_cast<double>(n);
	input.receiveRateBps = 1e6 + 123456.789 * static_cast<double>(n);
	return input;
}

}

TEST(RecordReplayTest, ReplayReproducesRecordedResults)
{
	constexpr std::size_t Count = 10;
	const bwe::Config config;
	std::stringstream file;
	std::vector<bwe::Output> recorded;
	{
		bwe::Recorder recorder(file);
		bwe::Estimator estimator(config);
		estimator.setObserver([&recorder](const bwe::Input& input, const bwe::Output& output)
			{
				recorder.record(input, output);
			});
		for (std::size_t n = 0; n < Count; ++n)
		{
			recorded.push_back(estimator.update(makeInput(n)));
		}
	}

	const bwe::Player player(file);
	ASSERT_EQ(player.records().size(), Count);
	for (std::size_t n = 0; n < Count; ++n)
	{
		const bwe::Record& record = player.records()[n];
		const bwe::Input input = makeInput(n);
		EXPECT_EQ(record.sequence, n + 1);
		EXPECT_EQ(record.input.streamId, input.streamId);
		EXPECT_EQ(record.input.rttMs, input.rttMs);
		EXPECT_EQ(record.input.dropRatePercent, input.dropRatePercent);
		EXPECT_EQ(record.input.receiveRateBps, input.receiveRateBps);
		EXPECT_EQ(record.output.rateBps, recorded[n].rateBps);
	}

	bwe::Estimator replayEstimator(config);
	int notified = 0;
	replayEstimator.subscribe(0, [&notified](const bwe::Output&)
		{
			++notified;
		});
	const std::vector<bwe::Output> replayed = player.replay(replayEstimator);
	ASSERT_EQ(replayed.size(), Count);
	for (std::size_t n = 0; n < Count; ++n)
	{
		EXPECT_EQ(replayed[n].streamId, recorded[n].streamId);
		EXPECT_EQ(replayed[n].rateBps, recorded[n].rateBps);
	}
	EXPECT_EQ(notified, 4);
}

TEST(RecordReplayTest, AcceptsWindowsLineEndingsAndEmptyLines)
{
	std::istringstream file(std::string(bwe::Recorder::Header) + "\r\n1,2,50,1,1000000,123\r\n\r\n");
	const bwe::Player player(file);
	ASSERT_EQ(player.records().size(), 1u);
	EXPECT_EQ(player.records()[0].input.streamId, 2u);
	EXPECT_DOUBLE_EQ(player.records()[0].output.rateBps, 123.0);
}

TEST(RecordReplayTest, RejectsInvalidFiles)
{
	const std::string header = std::string(bwe::Recorder::Header) + "\n";

	std::istringstream empty("");
	EXPECT_THROW(bwe::Player player(empty), std::runtime_error);

	std::istringstream wrongHeader("foo\n");
	EXPECT_THROW(bwe::Player player(wrongHeader), std::runtime_error);

	std::istringstream wrongCount(header + "1,2,3\n");
	EXPECT_THROW(bwe::Player player(wrongCount), std::runtime_error);

	std::istringstream wrongValue(header + "1,2,abc,1,1,1\n");
	EXPECT_THROW(bwe::Player player(wrongValue), std::runtime_error);
}
