#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include "bwe/shared_link_allocator.hpp"
#include "support/shared_channel_simulator.hpp"

namespace bwe
{
namespace
{

using std::chrono::seconds;
using test_support::SharedChannelConfig;
using test_support::SharedChannelSimulator;

// Receiver-side hybrid tuned for radio links: random loss below 5 % is not
// congestion, and the delay thresholds tolerate radio jitter.
const Parameters kRadioParameters = {{"aimd.loss_threshold", 0.05},
									 {"delay.low_threshold_ms", 30.0},
									 {"delay.high_threshold_ms", 100.0}};

struct LoopOptions
{
	double duration_s = 30.0;
	std::vector<double> weights;  // Default weight 1.
	int join_sender = -1;         // Sender that starts at join_at_s.
	double join_at_s = 0.0;
	int leave_sender = -1;        // Sender that stops at leave_at_s.
	double leave_at_s = 0.0;
};

struct Trace
{
	std::vector<double> time_s;
	std::vector<double> capacity_bps;
	std::vector<std::vector<double>> rate_bps;  // [sample][sender]
};

FlowConfig MakeFlowConfig(const SharedChannelConfig& config,
						  const LoopOptions& options, int sender)
{
	FlowConfig flow;
	if (!options.weights.empty())
	{
		flow.weight = options.weights[sender];
	}
	flow.max_bps = config.max_bps[sender];
	return flow;
}

// Receiver statistics -> allocator -> delayed feedback -> senders.
Trace RunClosedLoop(const SharedChannelConfig& config,
					const LoopOptions& options)
{
	SharedChannelSimulator simulator(config);
	SharedLinkAllocator allocator("hybrid", kRadioParameters);
	const int senders = simulator.SenderCount();
	std::vector<bool> active(senders, true);
	for (int i = 0; i < senders; ++i)
	{
		if (i == options.join_sender)
		{
			active[i] = false;
			simulator.SetActive(i, false);
		}
		else
		{
			allocator.AddFlow(i, MakeFlowConfig(config, options, i));
		}
	}

	Trace trace;
	const int samples = static_cast<int>(options.duration_s * 2);
	for (int k = 0; k < samples; ++k)
	{
		const double start_s = k * 0.5;
		if (options.join_sender >= 0 && start_s == options.join_at_s)
		{
			active[options.join_sender] = true;
			simulator.SetActive(options.join_sender, true);
			allocator.AddFlow(options.join_sender,
							  MakeFlowConfig(config, options, options.join_sender));
		}
		if (options.leave_sender >= 0 && start_s == options.leave_at_s)
		{
			active[options.leave_sender] = false;
			simulator.SetActive(options.leave_sender, false);
			allocator.RemoveFlow(options.leave_sender);
		}

		simulator.Step();
		for (int i = 0; i < senders; ++i)
		{
			if (active[i])
			{
				allocator.Update(i, simulator.Receiver(i));
			}
		}
		allocator.Tick(simulator.Now());
		for (const FlowAllocation& allocation : allocator.Allocate())
		{
			simulator.SendFeedback(static_cast<int>(allocation.flow),
								   allocation.target_bps);
		}

		trace.time_s.push_back(start_s + 0.5);
		trace.capacity_bps.push_back(simulator.CapacityBps());
		std::vector<double> rates;
		for (int i = 0; i < senders; ++i)
		{
			rates.push_back(simulator.RateBps(i));
		}
		trace.rate_bps.push_back(rates);
	}
	return trace;
}

std::vector<int> SamplesBetween(const Trace& trace, double from_s,
								double to_s)
{
	std::vector<int> samples;
	for (int k = 0; k < static_cast<int>(trace.time_s.size()); ++k)
	{
		if (trace.time_s[k] >= from_s && trace.time_s[k] <= to_s)
		{
			samples.push_back(k);
		}
	}
	return samples;
}

double Sum(const std::vector<double>& values)
{
	double sum = 0.0;
	for (double value : values)
	{
		sum += value;
	}
	return sum;
}

// Jain's fairness index over the active (non-zero) rates.
double JainIndex(const std::vector<double>& rates)
{
	double sum = 0.0;
	double squares = 0.0;
	int count = 0;
	for (double rate : rates)
	{
		if (rate > 0.0)
		{
			sum += rate;
			squares += rate * rate;
			++count;
		}
	}
	return count == 0 ? 1.0 : sum * sum / (count * squares);
}

SharedChannelConfig ChannelOf(double capacity_bps)
{
	SharedChannelConfig config;
	config.capacity_profile = {{seconds(0), capacity_bps}};
	return config;
}

TEST(SharedChannelScenarioTest, ConvergesToFairShareUnderOverload)
{
	// Demand 3 x 6 Mbit/s on a 12 Mbit/s channel.
	const Trace trace = RunClosedLoop(ChannelOf(12e6), LoopOptions{});
	for (int k : SamplesBetween(trace, 20.0, 30.0))
	{
		const double sum = Sum(trace.rate_bps[k]);
		EXPECT_LE(sum, trace.capacity_bps[k]) << "t=" << trace.time_s[k];
		EXPECT_GE(sum, 0.8 * trace.capacity_bps[k]) << "t=" << trace.time_s[k];
		EXPECT_GE(JainIndex(trace.rate_bps[k]), 0.99) << "t=" << trace.time_s[k];
	}
}

TEST(SharedChannelScenarioTest, FollowsCapacityDrop)
{
	SharedChannelConfig config = ChannelOf(12e6);
	config.capacity_profile.push_back({seconds(20), 6e6});
	LoopOptions options;
	options.duration_s = 40.0;
	const Trace trace = RunClosedLoop(config, options);
	for (int k : SamplesBetween(trace, 25.0, 40.0))
	{
		const double sum = Sum(trace.rate_bps[k]);
		EXPECT_LE(sum, trace.capacity_bps[k]) << "t=" << trace.time_s[k];
		EXPECT_GE(sum, 0.8 * trace.capacity_bps[k]) << "t=" << trace.time_s[k];
	}
}

TEST(SharedChannelScenarioTest, ToleratesRandomRadioLoss)
{
	// 2 % radio loss, demand 3 x 3 Mbit/s below the 12 Mbit/s capacity.
	SharedChannelConfig config = ChannelOf(12e6);
	config.radio_loss = 0.02;
	config.max_bps = {3e6, 3e6, 3e6};
	const Trace trace = RunClosedLoop(config, LoopOptions{});
	for (int k : SamplesBetween(trace, 15.0, 30.0))
	{
		for (double rate : trace.rate_bps[k])
		{
			EXPECT_GE(rate, 0.8 * 3e6) << "t=" << trace.time_s[k];
		}
	}
}

TEST(SharedChannelScenarioTest, RedistributesWhenSendersJoinAndLeave)
{
	LoopOptions options;
	options.duration_s = 60.0;
	options.join_sender = 2;
	options.join_at_s = 20.0;
	options.leave_sender = 0;
	options.leave_at_s = 40.0;
	const Trace trace = RunClosedLoop(ChannelOf(12e6), options);

	for (int k : SamplesBetween(trace, 25.0, 39.5))
	{
		const std::vector<double>& rates = trace.rate_bps[k];
		const double mean = Sum(rates) / 3.0;
		EXPECT_LE(Sum(rates), trace.capacity_bps[k]) << "t=" << trace.time_s[k];
		for (double rate : rates)
		{
			EXPECT_NEAR(rate, mean, 0.1 * mean) << "t=" << trace.time_s[k];
		}
	}
	for (int k : SamplesBetween(trace, 45.0, 60.0))
	{
		const std::vector<double>& rates = trace.rate_bps[k];
		EXPECT_EQ(rates[0], 0.0) << "t=" << trace.time_s[k];
		EXPECT_LE(Sum(rates), trace.capacity_bps[k]) << "t=" << trace.time_s[k];
		EXPECT_GE(rates[1], 0.4 * trace.capacity_bps[k])
			<< "t=" << trace.time_s[k];
		EXPECT_GE(rates[2], 0.4 * trace.capacity_bps[k])
			<< "t=" << trace.time_s[k];
	}
}

TEST(SharedChannelScenarioTest, RespectsWeights)
{
	LoopOptions options;
	options.weights = {2.0, 1.0, 1.0};
	const Trace trace = RunClosedLoop(ChannelOf(12e6), options);
	for (int k : SamplesBetween(trace, 20.0, 30.0))
	{
		const std::vector<double>& rates = trace.rate_bps[k];
		EXPECT_LE(Sum(rates), trace.capacity_bps[k]) << "t=" << trace.time_s[k];
		EXPECT_NEAR(rates[0] / rates[1], 2.0, 0.2) << "t=" << trace.time_s[k];
		EXPECT_NEAR(rates[1] / rates[2], 1.0, 0.1) << "t=" << trace.time_s[k];
	}
}

}  // namespace
}  // namespace bwe
