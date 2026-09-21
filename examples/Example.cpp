#include "bwe/Estimator.hpp"
#include "bwe/Player.hpp"
#include "bwe/Recorder.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

constexpr double ChannelCapacityBps = 10e6;
constexpr double BaseRttMs = 40.0;
constexpr double MaxQueueDelayMs = 60.0;
constexpr int StepCount = 30;

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
void runLive(const bwe::Config& config, const std::string& fileName)
{
	std::ofstream file(fileName);
	if (!file)
	{
		throw std::runtime_error("cannot open " + fileName + " for writing");
	}

	// Current rate of each sender, the index is the stream id.
	std::vector<double> sendRatesBps(3);
	sendRatesBps[0] = 0.5e6;
	sendRatesBps[1] = 1e6;
	sendRatesBps[2] = 2e6;

	bwe::Recorder recorder(file);
	bwe::Estimator estimator(config);
	estimator.setObserver([&recorder](const bwe::Input& input, const bwe::Output& output)
		{
			recorder.record(input, output);
		});

	// Each sender receives its own result, here it simply takes over the new rate.
	for (std::size_t i = 0; i < sendRatesBps.size(); ++i)
	{
		estimator.subscribe(static_cast<bwe::StreamId>(i), [&sendRatesBps](const bwe::Output& output)
			{
				sendRatesBps[output.streamId] = output.rateBps;
			});
	}

	printHeader(sendRatesBps.size());
	for (int step = 1; step <= StepCount; ++step)
	{
		const ChannelState channel = simulateChannel(sendRatesBps);
		const std::vector<double> sentBps = sendRatesBps;

		// Receiver side: one input per stream with the measured values.
		for (std::size_t i = 0; i < sentBps.size(); ++i)
		{
			bwe::Input input;
			input.streamId = static_cast<bwe::StreamId>(i);
			input.rttMs = channel.rttMs;
			input.dropRatePercent = channel.dropRatePercent;
			input.receiveRateBps = sentBps[i] * (1.0 - channel.dropRatePercent / 100.0);
			estimator.update(input);
		}
		printStep(step, channel, sendRatesBps);
	}
}

/// Replays the recording and checks that the results are identical.
bool replay(const bwe::Config& config, const std::string& fileName)
{
	std::ifstream file(fileName);
	if (!file)
	{
		throw std::runtime_error("cannot open " + fileName + " for reading");
	}

	const bwe::Player player(file);
	bwe::Estimator simulation(config);
	const std::vector<bwe::Output> replayed = player.replay(simulation);

	std::size_t differences = 0;
	for (std::size_t n = 0; n < replayed.size(); ++n)
	{
		if (replayed[n].rateBps != player.records()[n].output.rateBps)
		{
			++differences;
		}
	}
	std::printf("\nReplayed %zu records from %s: %zu differences\n", replayed.size(), fileName.c_str(), differences);
	return differences == 0;
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
