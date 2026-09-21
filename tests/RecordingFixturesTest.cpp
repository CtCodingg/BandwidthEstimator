// Auto-discovery test: every *.csv file directly inside tests/fixtures/recordings/ (a real
// Recorder output, see e.g. realistic_session.csv) is replayed into a real (TFRC) Estimator.
// Drop a new recording into that folder and it is picked up and run as its own named test
// automatically - no new test code needed.
//
// If the recording includes Recorder::RecordOutput() checkpoints (EventKind::kOutput events,
// ignored by Player::Replay() itself), the final result is checked against them within a relative
// delta - the checkpoint is a recorded snapshot from whenever the fixture was made, not
// necessarily bit-identical to today's algorithm output. Without checkpoints, this falls back to
// a structural smoke test only (right stream count, finite, non-negative rates).
#include "bwe/Estimator.hpp"
#include "bwe/Player.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{

namespace fs = std::filesystem;

constexpr uint32_t kIntervalMs = 5;

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

/// @return The stream ids still known after every event in @p events has been applied, i.e. added
///         by a "stream" event and not later "remove"d.
std::set<bwe::StreamId> StreamsRemainingAfter(const std::vector<bwe::RecordedEvent>& events)
{
	std::set<bwe::StreamId> known;
	for (const bwe::RecordedEvent& event : events)
	{
		if (event.kind == bwe::EventKind::kStream)
		{
			known.insert(event.stream.stream_id);
		}
		else if (event.kind == bwe::EventKind::kRemove)
		{
			known.erase(event.stream.stream_id);
		}
	}
	return known;
}

/// @return The last Recorder::RecordOutput() value recorded per stream, if any.
std::map<bwe::StreamId, double> RecordedOutputCheckpoints(const std::vector<bwe::RecordedEvent>& events)
{
	std::map<bwe::StreamId, double> checkpoints;
	for (const bwe::RecordedEvent& event : events)
	{
		if (event.kind == bwe::EventKind::kOutput)
		{
			checkpoints[event.stream.stream_id] = event.estimated_rate_bps;
		}
	}
	return checkpoints;
}

class RecordingFixtureTest : public ::testing::Test
{
public:
	explicit RecordingFixtureTest(std::string csv_path)
		: csv_path_(std::move(csv_path))
	{
	}

protected:
	void TestBody() override
	{
		std::ifstream recording(csv_path_);
		ASSERT_TRUE(recording.is_open()) << "cannot open " << csv_path_;
		const bwe::Player player(recording);
		ASSERT_FALSE(player.Events().empty()) << csv_path_ << " has no recorded events";

		bwe::Config config; // real TFRC, default packet size
		config.update_interval_ms = kIntervalMs;
		bwe::Estimator estimator(config);
		player.Replay(estimator);

		const std::set<bwe::StreamId> expected_streams = StreamsRemainingAfter(player.Events());
		const std::vector<bwe::Output> outputs = WaitForOutputs(estimator, expected_streams.size());

		ASSERT_EQ(outputs.size(), expected_streams.size())
			<< "replaying " << csv_path_ << " never settled on the expected stream count";

		const std::map<bwe::StreamId, double> checkpoints = RecordedOutputCheckpoints(player.Events());
		for (const bwe::Output& output : outputs)
		{
			EXPECT_EQ(expected_streams.count(output.stream_id), 1u)
				<< "unexpected stream " << output.stream_id << " from " << csv_path_;
			EXPECT_TRUE(std::isfinite(output.rate_bps)) << "stream " << output.stream_id << " in " << csv_path_;
			EXPECT_GE(output.rate_bps, 0.0) << "stream " << output.stream_id << " in " << csv_path_;

			const auto checkpoint = checkpoints.find(output.stream_id);
			if (checkpoint != checkpoints.end())
			{
				EXPECT_NEAR(output.rate_bps, checkpoint->second, std::max(1.0, checkpoint->second * 0.01))
					<< "stream " << output.stream_id << " in " << csv_path_ << " vs. its recorded checkpoint";
			}
		}
	}

private:
	std::string csv_path_;
};

/// Registers one test per *.csv file directly inside @p dir (subdirectories are not scanned),
/// named after the file so a failure points straight at the recording.
void RegisterRecordingFixtureTests(const fs::path& dir)
{
	if (!fs::exists(dir))
	{
		return;
	}
	for (const fs::directory_entry& entry : fs::directory_iterator(dir))
	{
		if (!entry.is_regular_file() || entry.path().extension() != ".csv")
		{
			continue;
		}
		std::string csv_path = entry.path().string();
		const std::string test_name = entry.path().stem().string();
		::testing::RegisterTest("RecordingFixtureTest", test_name.c_str(), nullptr, nullptr, __FILE__, __LINE__,
			[csv_path]() -> ::testing::Test* { return new RecordingFixtureTest(csv_path); });
	}
}

// GoogleTest requires dynamic registration to happen before RUN_ALL_TESTS(); since this binary
// uses gtest_main (no custom main() to call it from), a static initializer is the hook available
// to run it before main() starts.
const bool kRecordingFixtureTestsRegistered =
	(RegisterRecordingFixtureTests(fs::path(BWE_TEST_FIXTURES_DIR) / "recordings"), true);

}
