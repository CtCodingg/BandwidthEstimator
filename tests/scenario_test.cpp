#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "bwe/estimator_factory.hpp"
#include "support/link_simulator.hpp"

namespace bwe
{
namespace
{

using std::chrono::seconds;
using test_support::LinkSimulator;
using test_support::LinkSimulatorConfig;

// Live source with 6 Mbit/s over a link with 10, 4 and 8 Mbit/s.
constexpr double kInputBps = 6e6;
constexpr int kSamples = 120;  // 60 s at 500 ms.
constexpr int kWarmupSamples = 4;
// Last sample before the end of each 20 s phase (t = 19.5 s, 39.5 s, 59.5 s).
constexpr int kEndOfPhase1 = 38;
constexpr int kEndOfPhase2 = 78;
constexpr int kEndOfPhase3 = 118;

// Algorithms expected to follow the capacity changes.
const std::set<std::string> kSenderTracking = {
	"aimd", "delay", "hybrid", "link_capacity", "send_buffer"};
const std::set<std::string> kReceiverTracking = {
	"aimd", "delay", "hybrid", "link_capacity", "tsbpd_reserve"};
// Algorithms only checked for plausibility.
const std::set<std::string> kPlausibilityOnly = {"mathis", "tfrc"};

LinkSimulatorConfig ScenarioConfig()
{
	LinkSimulatorConfig config;
	config.input_bps = kInputBps;
	config.capacity_profile = {{seconds(0), 10e6},
							   {seconds(20), 4e6},
							   {seconds(40), 8e6}};
	return config;
}

struct Run
{
	std::vector<double> capacity_bps;
	std::map<std::string, std::vector<BandwidthEstimate>> estimates;
};

Run RunSenderScenario()
{
	EstimatorFactory& factory = EstimatorFactory::Instance();
	std::map<std::string, std::unique_ptr<SenderEstimator>> estimators;
	for (const std::string& name : factory.SenderAlgorithms())
	{
		estimators[name] = factory.CreateSender(name);
	}
	LinkSimulator simulator(ScenarioConfig());
	Run run;
	for (int i = 0; i < kSamples; ++i)
	{
		simulator.Step();
		run.capacity_bps.push_back(simulator.CapacityBps());
		for (auto& [name, estimator] : estimators)
		{
			estimator->Update(simulator.Sender());
			run.estimates[name].push_back(estimator->GetEstimate());
		}
	}
	return run;
}

Run RunReceiverScenario()
{
	EstimatorFactory& factory = EstimatorFactory::Instance();
	std::map<std::string, std::unique_ptr<ReceiverEstimator>> estimators;
	for (const std::string& name : factory.ReceiverAlgorithms())
	{
		estimators[name] = factory.CreateReceiver(name);
	}
	LinkSimulator simulator(ScenarioConfig());
	Run run;
	for (int i = 0; i < kSamples; ++i)
	{
		simulator.Step();
		run.capacity_bps.push_back(simulator.CapacityBps());
		for (auto& [name, estimator] : estimators)
		{
			estimator->Update(simulator.Receiver());
			run.estimates[name].push_back(estimator->GetEstimate());
		}
	}
	return run;
}

void ExpectPlausible(const Run& run)
{
	for (const auto& [name, estimates] : run.estimates)
	{
		for (int i = kWarmupSamples; i < kSamples; ++i)
		{
			const BandwidthEstimate& estimate = estimates[i];
			EXPECT_TRUE(estimate.valid) << name << " at sample " << i;
			EXPECT_TRUE(std::isfinite(estimate.bits_per_second))
				<< name << " at sample " << i;
			EXPECT_GT(estimate.bits_per_second, 0.0) << name << " at sample " << i;
		}
	}
}

void ExpectTracksCapacity(const Run& run, const std::set<std::string>& names)
{
	for (const std::string& name : names)
	{
		const auto it = run.estimates.find(name);
		ASSERT_NE(it, run.estimates.end()) << name;
		const std::vector<BandwidthEstimate>& estimates = it->second;
		// Without congestion the full input rate gets through.
		EXPECT_GE(estimates[kEndOfPhase1].bits_per_second, kInputBps) << name;
		// During congestion at most 10 % above the capacity.
		EXPECT_LE(estimates[kEndOfPhase2].bits_per_second,
				  1.1 * run.capacity_bps[kEndOfPhase2])
			<< name;
		// Recovered after the capacity rose again.
		EXPECT_GE(estimates[kEndOfPhase3].bits_per_second, kInputBps) << name;
	}
}

// At the end of every phase at most 20 % above the true capacity.
void ExpectNoOverestimation(const Run& run,
							const std::set<std::string>& names)
{
	for (const std::string& name : names)
	{
		const std::vector<BandwidthEstimate>& estimates = run.estimates.at(name);
		for (int i : {kEndOfPhase1, kEndOfPhase2, kEndOfPhase3})
		{
			EXPECT_LE(estimates[i].bits_per_second, 1.2 * run.capacity_bps[i])
				<< name << " at sample " << i;
		}
	}
}

void ExpectAllAlgorithmsCovered(const std::vector<std::string>& registered,
								const std::set<std::string>& tracking)
{
	for (const std::string& name : registered)
	{
		EXPECT_TRUE(tracking.count(name) == 1 ||
					kPlausibilityOnly.count(name) == 1)
			<< "algorithm '" << name << "' is not covered by the scenario";
	}
}

TEST(ScenarioTest, CoversAllRegisteredAlgorithms)
{
	const EstimatorFactory& factory = EstimatorFactory::Instance();
	ExpectAllAlgorithmsCovered(factory.SenderAlgorithms(), kSenderTracking);
	ExpectAllAlgorithmsCovered(factory.ReceiverAlgorithms(), kReceiverTracking);
}

TEST(ScenarioTest, SenderEstimatesArePlausible)
{
	ExpectPlausible(RunSenderScenario());
}

TEST(ScenarioTest, ReceiverEstimatesArePlausible)
{
	ExpectPlausible(RunReceiverScenario());
}

TEST(ScenarioTest, SenderEstimatesTrackCapacity)
{
	ExpectTracksCapacity(RunSenderScenario(), kSenderTracking);
}

TEST(ScenarioTest, ReceiverEstimatesTrackCapacity)
{
	ExpectTracksCapacity(RunReceiverScenario(), kReceiverTracking);
}

TEST(ScenarioTest, SenderEstimatesDoNotOverestimate)
{
	ExpectNoOverestimation(RunSenderScenario(), kSenderTracking);
}

TEST(ScenarioTest, ReceiverEstimatesDoNotOverestimate)
{
	ExpectNoOverestimation(RunReceiverScenario(), kReceiverTracking);
}

TEST(ScenarioTest, LinkCapacityFollowsReportedCapacity)
{
	const Run run = RunSenderScenario();
	const std::vector<BandwidthEstimate>& estimates =
		run.estimates.at("link_capacity");
	// Loss-free phases: utilization 0.9 of the capacity despite noise.
	for (int i : {kEndOfPhase1, kEndOfPhase3})
	{
		EXPECT_GE(estimates[i].bits_per_second, 0.7 * run.capacity_bps[i]) << i;
		EXPECT_LE(estimates[i].bits_per_second, 1.0 * run.capacity_bps[i]) << i;
	}
}

}  // namespace
}  // namespace bwe
