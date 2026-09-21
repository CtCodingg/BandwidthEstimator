#include "bwe/Estimator.hpp"
#include "bwe/Player.hpp"
#include "bwe/Recorder.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{

constexpr double ChannelCapacityBps = 10e6;
constexpr double BaseRttMs = 40.0;
constexpr double MaxQueueDelayMs = 60.0;
constexpr int StepCount = 30;
constexpr std::uint32_t UpdateIntervalMs = 20; // fast on purpose, keeps this demo short

/// State of the shared channel during one step.
struct ChannelState
{
	double dropRatePercent = 0.0;
	double rttMs = 0.0;
};

/// Simple channel model: everything above the capacity is dropped, the delay grows with the load.
ChannelState simulateChannel(const std::vector<double>& sendRatesBps)
{
	double totalBps = 0.0;
	for (double rateBps : sendRatesBps)
	{
		totalBps += rateBps;
	}

	ChannelState state;
	if (totalBps > ChannelCapacityBps)
	{
		state.dropRatePercent = 100.0 * (totalBps - ChannelCapacityBps) / totalBps;
	}
	state.rttMs = BaseRttMs + MaxQueueDelayMs * std::min(totalBps / ChannelCapacityBps, 1.0);
	return state;
}

void printHeader(std::size_t senderCount)
{
	std::printf("step    drop%%    rttMs");
	for (std::size_t i = 0; i < senderCount; ++i)
	{
		std::printf("  sender%zu Mbit/s", i);
	}
	std::printf("\n");
}

void printStep(int step, const ChannelState& channel, const std::vector<double>& sendRatesBps)
{
	std::printf("%4d %8.2f %8.1f", step, channel.dropRatePercent, channel.rttMs);
	for (double rateBps : sendRatesBps)
	{
		std::printf(" %16.3f", rateBps / 1e6);
	}
	std::printf("\n");
}

/// Runs the closed loop senders -> channel -> receiver -> senders and records it.
/// Estimator recalculates on its own background thread, so every step waits a bit after
/// pushing its measurements before polling outputs() for the result.
void runLive(bwe::Config config, const std::string& fileName)
{
	std::ofstream file(fileName);
	if (!file)
	{
		throw std::runtime_error("cannot open " + fileName + " for writing");
	}
	config.updateIntervalMs = UpdateIntervalMs;

	// Current rate of each sender, the index is the stream id.
	std::vector<double> sendRatesBps(3);
	sendRatesBps[0] = 0.5e6;
	sendRatesBps[1] = 1e6;
	sendRatesBps[2] = 2e6;

	// All three streams share one channel; sender 2 is weighted to get twice the share of the others.
	const std::vector<double> weights = { 1.0, 1.0, 2.0 };

	bwe::Recorder recorder(file);
	bwe::Estimator estimator(config);

	printHeader(sendRatesBps.size());
	for (int step = 1; step <= StepCount; ++step)
	{
		const ChannelState channel = simulateChannel(sendRatesBps);
		const std::vector<double> sentBps = sendRatesBps;

		// Receiver side: push the channel condition and each stream's measurement individually,
		// as they would arrive in practice, not necessarily all at the same time.
		estimator.updateChannel(channel.rttMs, channel.dropRatePercent);
		recorder.recordChannel(channel.rttMs, channel.dropRatePercent);

		for (std::size_t i = 0; i < sentBps.size(); ++i)
		{
			bwe::StreamInput stream;
			stream.streamId = static_cast<bwe::StreamId>(i);
			stream.receiveRateBps = sentBps[i] * (1.0 - channel.dropRatePercent / 100.0);
			stream.weight = weights[i];
			estimator.updateStream(stream);
			recorder.recordStream(stream);
		}

		// Give the background thread time to recalculate with the values just pushed.
		std::this_thread::sleep_for(std::chrono::milliseconds(2 * config.updateIntervalMs));

		// Each sender takes over its new rate; no callbacks needed, outputs() is polled directly.
		for (const bwe::Output& output : estimator.outputs())
		{
			sendRatesBps[output.streamId] = output.rateBps;
		}

		printStep(step, channel, sendRatesBps);
	}
}

/// Replays the recorded events into a fresh estimator and prints its final outputs.
/// Estimator's timing is real (background thread + wall clock), so unlike a purely stateless
/// estimator this cannot promise bit-identical results; it is a sanity check, not a diff.
bool replay(bwe::Config config, const std::string& fileName)
{
	std::ifstream file(fileName);
	if (!file)
	{
		throw std::runtime_error("cannot open " + fileName + " for reading");
	}
	config.updateIntervalMs = UpdateIntervalMs;

	const bwe::Player player(file);
	bwe::Estimator simulation(config);          // other algorithm or settings possible
	player.replay(simulation);

	std::this_thread::sleep_for(std::chrono::milliseconds(2 * config.updateIntervalMs));
	const std::vector<bwe::Output> outputs = simulation.outputs();

	std::printf("\nReplayed %zu events from %s, final outputs:\n", player.events().size(), fileName.c_str());
	for (const bwe::Output& output : outputs)
	{
		std::printf("  sender%u: %.3f Mbit/s\n", output.streamId, output.rateBps / 1e6);
	}
	return !outputs.empty();
}

}

int main(int argc, char* argv[])
{
	const std::string fileName = argc > 1 ? argv[1] : "example.csv";
	try
	{
		const bwe::Config config;
		runLive(config, fileName);
		return replay(config, fileName) ? 0 : 1;
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "Error: %s\n", e.what());
		return 1;
	}
}
