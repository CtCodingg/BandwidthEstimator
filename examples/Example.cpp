#include "bwe/Estimator.hpp"
#include "bwe/Player.hpp"
#include "bwe/Recorder.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{

constexpr double kChannelCapacityBps = 10e6;
constexpr double kBaseRttMs = 40.0;
constexpr double kMaxQueueDelayMs = 60.0;
constexpr int kStepCount = 30;
constexpr uint32_t kUpdateIntervalMs = 20; // fast on purpose, keeps this demo short

/// State of the shared channel during one step.
struct ChannelState
{
	double drop_rate_percent = 0.0;
	double rtt_ms = 0.0;
};

/// Simple channel model: everything above the capacity is dropped, the delay grows with the load.
ChannelState SimulateChannel(const std::vector<double>& send_rates_bps)
{
	double total_bps = 0.0;
	for (double rate_bps : send_rates_bps)
	{
		total_bps += rate_bps;
	}

	ChannelState state;
	if (total_bps > kChannelCapacityBps)
	{
		state.drop_rate_percent = 100.0 * (total_bps - kChannelCapacityBps) / total_bps;
	}
	state.rtt_ms = kBaseRttMs + kMaxQueueDelayMs * std::min(total_bps / kChannelCapacityBps, 1.0);
	return state;
}

void PrintHeader(size_t sender_count)
{
	std::printf("step    drop%%    rttMs");
	for (size_t i = 0; i < sender_count; ++i)
	{
		std::printf("  sender%zu Mbit/s", i);
	}
	std::printf("\n");
}

void PrintStep(int step, const ChannelState& channel, const std::vector<double>& send_rates_bps)
{
	std::printf("%4d %8.2f %8.1f", step, channel.drop_rate_percent, channel.rtt_ms);
	for (double rate_bps : send_rates_bps)
	{
		std::printf(" %16.3f", rate_bps / 1e6);
	}
	std::printf("\n");
}

/// Runs the closed loop senders -> channel -> receiver -> senders and records it.
/// Estimator recalculates on its own background thread, so every step waits a bit after
/// pushing its measurements before polling Outputs() for the result.
void RunLive(bwe::Config config, const std::string& file_name)
{
	std::ofstream file(file_name);
	if (!file)
	{
		throw std::runtime_error("cannot open " + file_name + " for writing");
	}
	config.update_interval_ms = kUpdateIntervalMs;

	// Current rate of each sender, the index is the stream id.
	std::vector<double> send_rates_bps(3);
	send_rates_bps[0] = 0.5e6;
	send_rates_bps[1] = 1e6;
	send_rates_bps[2] = 2e6;

	// All three streams share one channel; sender 2 is weighted to get twice the share of the
	// others, but is capped at 4 Mbit/s so it cannot keep rising without limit.
	const std::vector<double> weights = { 1.0, 1.0, 2.0 };
	const std::vector<double> max_rates_bps = {
		std::numeric_limits<double>::infinity(),
		std::numeric_limits<double>::infinity(),
		4e6
	};

	bwe::Recorder recorder(file);
	bwe::Estimator estimator(config);

	PrintHeader(send_rates_bps.size());
	for (int step = 1; step <= kStepCount; ++step)
	{
		const ChannelState channel = SimulateChannel(send_rates_bps);
		const std::vector<double> sent_bps = send_rates_bps;

		// Receiver side: push the channel condition and each stream's measurement individually,
		// as they would arrive in practice, not necessarily all at the same time.
		estimator.UpdateChannel(channel.rtt_ms, channel.drop_rate_percent);
		recorder.RecordChannel(channel.rtt_ms, channel.drop_rate_percent);

		for (size_t i = 0; i < sent_bps.size(); ++i)
		{
			bwe::StreamInput stream;
			stream.stream_id = static_cast<bwe::StreamId>(i);
			stream.receive_rate_bps = sent_bps[i] * (1.0 - channel.drop_rate_percent / 100.0);
			stream.weight = weights[i];
			stream.max_rate_bps = max_rates_bps[i];
			estimator.UpdateStream(stream);
			recorder.RecordStream(stream);
		}

		// Give the background thread time to recalculate with the values just pushed.
		std::this_thread::sleep_for(std::chrono::milliseconds(2 * config.update_interval_ms));

		// Each sender takes over its new rate; no callbacks needed, Outputs() is polled directly.
		for (const bwe::Output& output : estimator.Outputs())
		{
			send_rates_bps[output.stream_id] = output.rate_bps;
		}

		PrintStep(step, channel, send_rates_bps);
	}
}

/// Replays the recorded events into a fresh estimator and prints its final outputs.
/// Estimator's timing is real (background thread + wall clock), so unlike a purely stateless
/// estimator this cannot promise bit-identical results; it is a sanity check, not a diff.
bool ReplayRecording(bwe::Config config, const std::string& file_name)
{
	std::ifstream file(file_name);
	if (!file)
	{
		throw std::runtime_error("cannot open " + file_name + " for reading");
	}
	config.update_interval_ms = kUpdateIntervalMs;

	const bwe::Player player(file);
	bwe::Estimator simulation(config); // other algorithm or settings possible
	player.Replay(simulation);

	std::this_thread::sleep_for(std::chrono::milliseconds(2 * config.update_interval_ms));
	const std::vector<bwe::Output> outputs = simulation.Outputs();

	std::printf("\nReplayed %zu events from %s, final outputs:\n", player.Events().size(), file_name.c_str());
	for (const bwe::Output& output : outputs)
	{
		std::printf("  sender%u: %.3f Mbit/s\n", output.stream_id, output.rate_bps / 1e6);
	}
	return !outputs.empty();
}

}

int main(int argc, char* argv[])
{
	const std::string file_name = argc > 1 ? argv[1] : "example.csv";
	try
	{
		const bwe::Config config;
		RunLive(config, file_name);
		return ReplayRecording(config, file_name) ? 0 : 1;
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "Error: %s\n", e.what());
		return 1;
	}
}
