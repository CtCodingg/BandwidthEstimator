#include "bwe/Estimator.hpp"
#include "bwe/Player.hpp"
#include "bwe/Recorder.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
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
		recorder.RecordOutput(bwe::Output{ 2, 456.0 });
	}

	const bwe::Player player(file);
	ASSERT_EQ(player.Events().size(), 5u);

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

	EXPECT_EQ(player.Events()[4].kind, bwe::EventKind::kOutput);
	EXPECT_EQ(player.Events()[4].stream.stream_id, 2u);
	EXPECT_DOUBLE_EQ(player.Events()[4].estimated_rate_bps, 456.0);
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

TEST(RecordReplayTest, ReplayIgnoresRecordedOutputEvents)
{
	std::stringstream file;
	{
		bwe::Recorder recorder(file);
		recorder.RecordChannel(50.0, 1.0);
		recorder.RecordStream(bwe::StreamInput{ 1, 1e6, 1.0 });
		// Deliberately wrong: a real Estimator would never produce this. Replay must not use it.
		recorder.RecordOutput(bwe::Output{ 1, 123456789.0 });
	}

	const bwe::Player player(file);
	bwe::Estimator estimator(std::make_unique<FixedAlgorithm>(900.0), kTestIntervalMs);
	player.Replay(estimator);

	const std::vector<bwe::Output> outputs = WaitForOutputs(estimator, 1);
	ASSERT_EQ(outputs.size(), 1u);
	EXPECT_DOUBLE_EQ(outputs[0].rate_bps, 900.0); // FixedAlgorithm's rate, not the recorded 123456789.0
}

TEST(RecordReplayTest, ReplaysARealisticRecordedSession)
{
	// tests/fixtures/recordings/realistic_session.csv is a genuine Recorder output, not
	// hand-authored CSV: a video call on a channel that starts clean, then degrades and loses its
	// screen-share stream, ending with the estimator's own settled output recorded as a
	// checkpoint (Recorder::RecordOutput()). Replaying the inputs into a fresh, real (TFRC)
	// Estimator and comparing against that checkpoint exercises the whole
	// Recorder -> Player -> Estimator pipeline end to end.
	// (It is also replayed as a structural smoke test by every other *.csv in that folder, see
	// RecordingFixturesTest.cpp; that one includes this same checkpoint comparison too.)
	std::ifstream file(std::string(BWE_TEST_FIXTURES_DIR) + "/recordings/realistic_session.csv");
	ASSERT_TRUE(file.is_open());
	const bwe::Player player(file);

	std::map<bwe::StreamId, double> expected_rate_bps;
	for (const bwe::RecordedEvent& event : player.Events())
	{
		if (event.kind == bwe::EventKind::kOutput)
		{
			expected_rate_bps[event.stream.stream_id] = event.estimated_rate_bps;
		}
	}
	ASSERT_FALSE(expected_rate_bps.empty()) << "fixture has no recorded output checkpoints";

	bwe::Config config; // default algorithm (TFRC), default packet size (1316, the SRT default)
	config.update_interval_ms = kTestIntervalMs;
	bwe::Estimator estimator(config);
	player.Replay(estimator); // EventKind::kOutput events above are not fed in, only read as expectations

	const std::vector<bwe::Output> outputs = WaitForOutputs(estimator, expected_rate_bps.size());
	ASSERT_EQ(outputs.size(), expected_rate_bps.size());
	for (const bwe::Output& output : outputs)
	{
		const auto it = expected_rate_bps.find(output.stream_id);
		ASSERT_NE(it, expected_rate_bps.end()) << "unexpected stream " << output.stream_id;
		// A relative delta, not an exact match: the checkpoint is a recorded snapshot, not a value
		// re-derived from today's algorithm code on every test run.
		EXPECT_NEAR(output.rate_bps, it->second, std::max(1.0, it->second * 0.01)) << "stream " << output.stream_id;
	}
}

TEST(RecordReplayTest, AcceptsWindowsLineEndingsAndEmptyLines)
{
	std::istringstream file(std::string(bwe::Recorder::kHeader) + "\r\n1,stream,2,0,0,1000000,1.5,inf,0\r\n\r\n");
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

	std::istringstream wrong_value(header + "1,channel,0,abc,1,0,0,0,0\n");
	EXPECT_THROW(bwe::Player player(wrong_value), std::runtime_error);

	std::istringstream unknown_kind(header + "1,bogus,0,50,1,0,0,0,0\n");
	EXPECT_THROW(bwe::Player player(unknown_kind), std::runtime_error);
}
