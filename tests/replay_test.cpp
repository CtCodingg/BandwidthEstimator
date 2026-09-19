// Replays every recording in tests/data through the SharedLinkAllocator.
// Own recordings can be added to that directory; checks against the true
// capacity only apply to recordings that contain it (simulated data).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "bwe/recording.hpp"
#include "bwe/shared_link_allocator.hpp"

#ifndef BWE_TEST_DATA_DIR
#error "BWE_TEST_DATA_DIR must point to tests/data"
#endif

namespace bwe
{
namespace
{

const Parameters kRadioParameters = {{"aimd.loss_threshold", 0.05},
									 {"delay.low_threshold_ms", 30.0},
									 {"delay.high_threshold_ms", 100.0}};

// Ticks before the estimate must be valid.
constexpr int kWarmupTicks = 10;

std::vector<std::filesystem::path> DataFiles()
{
	std::vector<std::filesystem::path> files;
	for (const auto& entry :
		 std::filesystem::directory_iterator(BWE_TEST_DATA_DIR))
	{
		if (entry.is_regular_file() && entry.path().extension() == ".csv")
		{
			files.push_back(entry.path());
		}
	}
	std::sort(files.begin(), files.end());
	return files;
}

struct ReplayResult
{
	int ticks = 0;
	int valid = 0;
	int implausible = 0;       // Not finite or not positive.
	int with_truth = 0;
	int above_truth = 0;       // More than 20 % above the true capacity.
};

// Groups the records by time; every group ends with a Tick().
ReplayResult Replay(const std::vector<Record>& records)
{
	SharedLinkAllocator allocator("hybrid", kRadioParameters);
	std::set<FlowId> flows;
	std::optional<Duration> current;
	std::optional<double> truth;
	ReplayResult result;

	const auto tick = [&](Duration now)
	{
		allocator.Tick(now);
		++result.ticks;
		const BandwidthEstimate estimate = allocator.GetTotalEstimate();
		if (result.ticks > kWarmupTicks)
		{
			result.valid += estimate.valid ? 1 : 0;
			if (!std::isfinite(estimate.bits_per_second) ||
				estimate.bits_per_second <= 0.0)
			{
				++result.implausible;
			}
			if (truth)
			{
				++result.with_truth;
				if (estimate.bits_per_second > 1.2 * *truth)
				{
					++result.above_truth;
				}
			}
		}
		truth.reset();
	};

	for (const Record& record : records)
	{
		if (current && record.time != *current)
		{
			tick(*current);
		}
		current = record.time;
		if (const auto* stats = std::get_if<ReceiverMeasurement>(&record.data))
		{
			if (!record.flow)
			{
				continue;
			}
			if (flows.insert(*record.flow).second)
			{
				allocator.AddFlow(*record.flow);
			}
			allocator.Update(*record.flow, *stats);
		}
		else if (const auto* capacity =
					   std::get_if<TrueCapacityRecord>(&record.data))
		{
			truth = capacity->capacity_bps;
		}
	}
	if (current)
	{
		tick(*current);
	}
	return result;
}

TEST(ReplayTest, FindsRecordings)
{
	EXPECT_FALSE(DataFiles().empty()) << "no recordings in " << BWE_TEST_DATA_DIR;
}

TEST(ReplayTest, ReplaysAllRecordings)
{
	for (const std::filesystem::path& file : DataFiles())
	{
		std::ifstream in(file);
		ASSERT_TRUE(in.good()) << file.string();
		std::vector<Record> records;
		try
		{
			records = ReadRecording(in);
		}
		catch (const std::exception& error)
		{
			FAIL() << file.string() << ": " << error.what();
		}

		const ReplayResult result = Replay(records);
		const std::string name = file.filename().string();
		ASSERT_GT(result.ticks, kWarmupTicks) << name;
		EXPECT_EQ(result.valid, result.ticks - kWarmupTicks) << name;
		EXPECT_EQ(result.implausible, 0) << name;
		// Transitions may briefly overshoot; at most 10 % of the ticks.
		EXPECT_LE(result.above_truth, result.with_truth / 10) << name;
	}
}

}  // namespace
}  // namespace bwe
