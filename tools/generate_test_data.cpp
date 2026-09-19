// Generates recordings of simulated shared radio channels.
//
// Runs the closed loop receiver statistics -> SharedLinkAllocator ->
// feedback -> senders and records receiver statistics, allocations, the
// total estimate and the true capacity.
//
// Usage:
//   bwe_generate_test_data --scenario <name> --output <file.csv>
//                          [--senders N] [--duration S] [--seed N]
//                          [--capacity BPS] [--algorithm NAME]
//                          [--param KEY=VALUE]...
//
// Scenarios: overload, capacity_drop, radio_loss, jitter, mixed_quality,
// fading.

#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "bwe/recording.hpp"
#include "bwe/shared_link_allocator.hpp"
#include "support/shared_channel_simulator.hpp"

namespace
{

using std::chrono::milliseconds;
using std::chrono::seconds;

struct Options
{
	std::string scenario;
	std::string output;
	int senders = 3;
	double duration_s = 60.0;
	unsigned seed = 7;
	double capacity_bps = 12e6;
	std::string algorithm = "hybrid";
	bwe::Parameters parameters = {{"aimd.loss_threshold", 0.05},
								  {"delay.low_threshold_ms", 30.0},
								  {"delay.high_threshold_ms", 100.0}};
};

void PrintUsage()
{
	std::cerr
		<< "Usage: bwe_generate_test_data --scenario <name> --output <file>\n"
		   "         [--senders N] [--duration S] [--seed N]\n"
		   "         [--capacity BPS] [--algorithm NAME] [--param KEY=VALUE]\n"
		   "Scenarios: overload, capacity_drop, radio_loss, jitter,\n"
		   "           mixed_quality, fading\n"
		   "Without --param the radio parameters of the README are used; the\n"
		   "first --param replaces them.\n";
}

Options ParseOptions(int argc, char** argv)
{
	Options options;
	bool custom_parameters = false;
	for (int i = 1; i < argc; ++i)
	{
		const std::string name = argv[i];
		if (i + 1 >= argc)
		{
			throw std::invalid_argument("missing value for " + name);
		}
		const std::string value = argv[++i];
		if (name == "--scenario")
		{
			options.scenario = value;
		}
		else if (name == "--output")
		{
			options.output = value;
		}
		else if (name == "--senders")
		{
			options.senders = std::stoi(value);
		}
		else if (name == "--duration")
		{
			options.duration_s = std::stod(value);
		}
		else if (name == "--seed")
		{
			options.seed = static_cast<unsigned>(std::stoul(value));
		}
		else if (name == "--capacity")
		{
			options.capacity_bps = std::stod(value);
		}
		else if (name == "--algorithm")
		{
			options.algorithm = value;
		}
		else if (name == "--param")
		{
			const std::size_t equals = value.find('=');
			if (equals == std::string::npos)
			{
				throw std::invalid_argument("--param expects KEY=VALUE");
			}
			if (!custom_parameters)
			{
				options.parameters.clear();
				custom_parameters = true;
			}
			options.parameters[value.substr(0, equals)] =
				std::stod(value.substr(equals + 1));
		}
		else
		{
			throw std::invalid_argument("unknown option " + name);
		}
	}
	if (options.scenario.empty() || options.output.empty())
	{
		throw std::invalid_argument("--scenario and --output are required");
	}
	if (options.senders < 1 || options.duration_s <= 0.0)
	{
		throw std::invalid_argument("--senders and --duration must be positive");
	}
	return options;
}

bwe::test_support::SharedChannelConfig MakeConfig(const Options& options)
{
	bwe::test_support::SharedChannelConfig config;
	config.capacity_profile = {{seconds(0), options.capacity_bps}};
	config.max_bps.assign(options.senders, options.capacity_bps / 2.0);
	config.seed = options.seed;

	const std::string& scenario = options.scenario;
	if (scenario == "overload")
	{
		// Demand of all senders exceeds the capacity.
	}
	else if (scenario == "capacity_drop")
	{
		config.capacity_profile.push_back(
			{std::chrono::duration_cast<bwe::Duration>(
				 std::chrono::duration<double>(options.duration_s / 2.0)),
			 options.capacity_bps / 2.0});
	}
	else if (scenario == "radio_loss")
	{
		config.radio_loss = 0.02;
		config.max_bps.assign(options.senders,
							  0.75 * options.capacity_bps / options.senders);
	}
	else if (scenario == "jitter")
	{
		config.jitter_max = milliseconds(20);
		config.spike_probability = 0.05;
		config.spike_delay = milliseconds(80);
	}
	else if (scenario == "mixed_quality")
	{
		config.radio.assign(options.senders, {1.0, 0.0});
		config.radio.back() = {0.5, 0.05};
	}
	else if (scenario == "fading")
	{
		config.fading_amplitude = 0.3;
	}
	else
	{
		throw std::invalid_argument("unknown scenario " + scenario);
	}
	return config;
}

void Generate(const Options& options, std::ostream& out)
{
	const auto config = MakeConfig(options);
	bwe::test_support::SharedChannelSimulator simulator(config);
	bwe::SharedLinkAllocator allocator(options.algorithm, options.parameters);
	for (int i = 0; i < simulator.SenderCount(); ++i)
	{
		bwe::FlowConfig flow;
		flow.max_bps = config.max_bps[i];
		allocator.AddFlow(static_cast<bwe::FlowId>(i), flow);
	}

	bwe::RecordingWriter writer(out);
	const double sample_s =
		std::chrono::duration<double>(config.sample_interval).count();
	const int samples = static_cast<int>(options.duration_s / sample_s);
	for (int k = 0; k < samples; ++k)
	{
		simulator.Step();
		const bwe::Duration now = simulator.Now();
		writer.WriteTrueCapacity(now, simulator.CapacityBps());
		for (int i = 0; i < simulator.SenderCount(); ++i)
		{
			const bwe::ReceiverMeasurement stats = simulator.Receiver(i);
			allocator.Update(static_cast<bwe::FlowId>(i), stats);
			writer.WriteReceiver(now, static_cast<bwe::FlowId>(i), stats);
		}
		allocator.Tick(now);
		writer.WriteEstimate(now, std::nullopt, options.algorithm,
							 allocator.GetTotalEstimate());
		for (const bwe::FlowAllocation& allocation : allocator.Allocate())
		{
			writer.WriteAllocation(now, allocation);
			simulator.SendFeedback(static_cast<int>(allocation.flow),
								   allocation.target_bps);
		}
	}
}

}  // namespace

int main(int argc, char** argv)
{
	try
	{
		const Options options = ParseOptions(argc, argv);
		std::ofstream out(options.output);
		if (!out)
		{
			std::cerr << "cannot open " << options.output << "\n";
			return EXIT_FAILURE;
		}
		Generate(options, out);
		return out ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	catch (const std::exception& error)
	{
		std::cerr << "error: " << error.what() << "\n";
		PrintUsage();
		return EXIT_FAILURE;
	}
}
