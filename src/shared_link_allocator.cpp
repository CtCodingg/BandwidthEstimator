#include "bwe/shared_link_allocator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

#include "bwe/bandwidth_estimator.hpp"

namespace bwe
{
namespace
{

using Counter = std::optional<std::uint64_t> ReceiverMeasurement::*;

// Cumulative counters that are summed over all flows.
constexpr Counter kCounters[] = {
	&ReceiverMeasurement::packets_received,
	&ReceiverMeasurement::packets_received_unique,
	&ReceiverMeasurement::packets_lost,
	&ReceiverMeasurement::packets_dropped,
	&ReceiverMeasurement::packets_belated,
	&ReceiverMeasurement::bytes_received,
	&ReceiverMeasurement::bytes_received_unique,
	&ReceiverMeasurement::bytes_lost,
	&ReceiverMeasurement::bytes_dropped,
};

// Adds the counter increases of one flow to the totals. A decreasing
// counter (reset) contributes nothing.
void AddDeltas(const ReceiverMeasurement& previous,
			   const ReceiverMeasurement& current,
			   ReceiverMeasurement& totals)
{
	for (Counter counter : kCounters)
	{
		const auto& from = previous.*counter;
		const auto& to = current.*counter;
		if (!from || !to || *to < *from)
		{
			continue;
		}
		totals.*counter = (totals.*counter).value_or(0) + (*to - *from);
	}
}

// Median of the values; sorts them in place.
double Median(std::vector<double>& values)
{
	std::sort(values.begin(), values.end());
	const std::size_t middle = values.size() / 2;
	return values.size() % 2 == 1
			   ? values[middle]
			   : (values[middle - 1] + values[middle]) / 2.0;
}

bool IsValidRate(double value)
{
	return std::isfinite(value) && value >= 0.0;
}

}  // namespace

struct SharedLinkAllocator::Impl
{
	// Guards all members; always taken before the estimator's own mutex.
	mutable std::mutex mutex;

	struct Flow
	{
		FlowConfig config;
		std::optional<ReceiverMeasurement> latest;
		std::optional<ReceiverMeasurement> consumed;
		bool updated = false;
		std::optional<Duration> last_seen;
	};

	std::unique_ptr<ReceiverEstimator> estimator;
	Duration flow_timeout;
	std::map<FlowId, Flow> flows;
	ReceiverMeasurement totals;
	// Scratch buffers, reserved in AddFlow() so Tick() does not allocate.
	std::vector<double> rtts;
	std::vector<double> capacities;
};

SharedLinkAllocator::SharedLinkAllocator(std::string_view algorithm,
										 const Parameters& parameters,
										 Duration flow_timeout)
	: impl_(std::make_unique<Impl>())
{
	if (flow_timeout <= Duration{0})
	{
		throw std::out_of_range("bwe: flow_timeout must be greater than 0");
	}
	impl_->estimator =
		EstimatorFactory::Instance().CreateReceiver(algorithm, parameters);
	impl_->flow_timeout = flow_timeout;
}

SharedLinkAllocator::~SharedLinkAllocator() = default;

void SharedLinkAllocator::AddFlow(FlowId flow, const FlowConfig& config)
{
	const std::string name = "bwe: flow " + std::to_string(flow);
	if (!(config.weight > 0.0 && config.weight <= 1e6))
	{
		throw std::out_of_range(name + ": weight must be in (0, 1e6]");
	}
	if (!IsValidRate(config.min_bps))
	{
		throw std::out_of_range(name + ": min_bps must be >= 0");
	}
	if (config.max_bps)
	{
		if (!IsValidRate(*config.max_bps) || *config.max_bps == 0.0)
		{
			throw std::out_of_range(name + ": max_bps must be > 0");
		}
		if (config.min_bps > *config.max_bps)
		{
			throw std::invalid_argument(name + ": min_bps exceeds max_bps");
		}
	}
	std::lock_guard<std::mutex> lock(impl_->mutex);
	if (impl_->flows.count(flow) != 0)
	{
		throw std::invalid_argument(name + " already exists");
	}
	impl_->rtts.reserve(impl_->flows.size() + 1);
	impl_->capacities.reserve(impl_->flows.size() + 1);
	impl_->flows[flow].config = config;
}

void SharedLinkAllocator::RemoveFlow(FlowId flow) noexcept
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->flows.erase(flow);
}

void SharedLinkAllocator::Update(
	FlowId flow, const ReceiverMeasurement& measurement) noexcept
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	const auto it = impl_->flows.find(flow);
	if (it == impl_->flows.end())
	{
		return;
	}
	it->second.latest = measurement;
	it->second.updated = true;
}

void SharedLinkAllocator::Tick(Duration now) noexcept
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	Impl& impl = *impl_;
	impl.rtts.clear();
	impl.capacities.clear();

	ReceiverMeasurement combined = impl.totals;
	combined.common.timestamp = now;
	bool any_active = false;
	double lowest_reserve = std::numeric_limits<double>::infinity();

	for (auto& entry : impl.flows)
	{
		Impl::Flow& flow = entry.second;
		if (flow.updated)
		{
			if (flow.consumed)
			{
				AddDeltas(*flow.consumed, *flow.latest, impl.totals);
			}
			flow.consumed = flow.latest;
			flow.last_seen = now;
			flow.updated = false;
		}
		if (!flow.last_seen || now - *flow.last_seen > impl.flow_timeout)
		{
			continue;
		}

		any_active = true;
		const ReceiverMeasurement& latest = *flow.latest;
		if (latest.common.rtt)
		{
			impl.rtts.push_back(static_cast<double>(latest.common.rtt->count()));
		}
		if (latest.common.link_capacity_bps)
		{
			impl.capacities.push_back(*latest.common.link_capacity_bps);
		}
		if (latest.common.mss_bytes)
		{
			combined.common.mss_bytes =
				std::max(combined.common.mss_bytes.value_or(0),
						 *latest.common.mss_bytes);
		}
		if (latest.receive_buffer_delay && latest.tsbpd_delay &&
			latest.tsbpd_delay->count() > 0)
		{
			const double reserve =
				static_cast<double>(latest.receive_buffer_delay->count()) /
				static_cast<double>(latest.tsbpd_delay->count());
			if (reserve < lowest_reserve)
			{
				lowest_reserve = reserve;
				combined.receive_buffer_delay = latest.receive_buffer_delay;
				combined.tsbpd_delay = latest.tsbpd_delay;
			}
		}
	}
	if (!any_active)
	{
		return;
	}

	for (Counter counter : kCounters)
	{
		combined.*counter = impl.totals.*counter;
	}
	if (!impl.rtts.empty())
	{
		combined.common.rtt =
			Duration(static_cast<Duration::rep>(Median(impl.rtts)));
	}
	if (!impl.capacities.empty())
	{
		combined.common.link_capacity_bps = Median(impl.capacities);
	}
	impl.estimator->Update(combined);
}

BandwidthEstimate SharedLinkAllocator::GetTotalEstimate() const noexcept
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->estimator->GetEstimate();
}

std::vector<FlowAllocation> SharedLinkAllocator::Allocate() const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	const BandwidthEstimate estimate = impl_->estimator->GetEstimate();
	if (!estimate.valid || impl_->flows.empty())
	{
		return {};
	}

	std::vector<FlowAllocation> result;
	std::vector<const FlowConfig*> configs;
	double remaining = estimate.bits_per_second;
	for (const auto& [id, flow] : impl_->flows)
	{
		result.push_back({id, flow.config.min_bps});
		configs.push_back(&flow.config);
		remaining -= flow.config.min_bps;
	}

	// Water-filling: distribute by weight; flows reaching their maximum are
	// fixed and the rest is redistributed among the others.
	std::vector<bool> open(result.size());
	for (std::size_t i = 0; i < result.size(); ++i)
	{
		const auto& max_bps = configs[i]->max_bps;
		open[i] = !max_bps || *max_bps > result[i].target_bps;
	}
	while (remaining > 0.0)
	{
		double weights = 0.0;
		for (std::size_t i = 0; i < result.size(); ++i)
		{
			if (open[i])
			{
				weights += configs[i]->weight;
			}
		}
		if (weights == 0.0)
		{
			break;
		}
		bool capped = false;
		for (std::size_t i = 0; i < result.size(); ++i)
		{
			const auto& max_bps = configs[i]->max_bps;
			const double share = remaining * configs[i]->weight / weights;
			if (open[i] && max_bps && result[i].target_bps + share >= *max_bps)
			{
				remaining -= *max_bps - result[i].target_bps;
				result[i].target_bps = *max_bps;
				open[i] = false;
				capped = true;
			}
		}
		if (capped)
		{
			continue;
		}
		for (std::size_t i = 0; i < result.size(); ++i)
		{
			if (open[i])
			{
				result[i].target_bps += remaining * configs[i]->weight / weights;
			}
		}
		break;
	}
	return result;
}

}  // namespace bwe
