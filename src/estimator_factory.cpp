#include "bwe/estimator_factory.hpp"

#include <mutex>
#include <stdexcept>
#include <utility>

#include "algorithms/aimd_estimator.hpp"
#include "algorithms/delay_estimator.hpp"
#include "algorithms/hybrid_estimator.hpp"
#include "algorithms/link_capacity_estimator.hpp"
#include "algorithms/mathis_estimator.hpp"
#include "algorithms/send_buffer_estimator.hpp"
#include "algorithms/side_adapter.hpp"
#include "algorithms/tfrc_estimator.hpp"
#include "algorithms/tsbpd_reserve_estimator.hpp"

namespace bwe
{
namespace
{

template <typename Creator>
using CreatorMap = std::map<std::string, Creator, std::less<>>;

template <typename Creator>
void Register(CreatorMap<Creator>& map, std::string name, Creator creator,
			  std::string_view side)
{
	if (name.empty())
	{
		throw std::invalid_argument("bwe: " + std::string(side) +
									" algorithm name must not be empty");
	}
	if (!creator)
	{
		throw std::invalid_argument("bwe: creator of " + std::string(side) +
									" algorithm '" + name +
									"' must not be empty");
	}
	if (map.find(name) != map.end())
	{
		throw std::invalid_argument("bwe: " + std::string(side) + " algorithm '" +
									name + "' is already registered");
	}
	map.emplace(std::move(name), std::move(creator));
}

template <typename Creator>
Creator FindCreator(const CreatorMap<Creator>& map, std::string_view name,
					std::string_view side)
{
	const auto it = map.find(name);
	if (it == map.end())
	{
		throw std::invalid_argument("bwe: unknown " + std::string(side) +
									" algorithm '" + std::string(name) + "'");
	}
	return it->second;
}

template <typename EstimatorType>
std::unique_ptr<EstimatorType> CheckNotNull(
	std::unique_ptr<EstimatorType> estimator, std::string_view name,
	std::string_view side)
{
	if (!estimator)
	{
		throw std::logic_error("bwe: creator of " + std::string(side) +
							   " algorithm '" + std::string(name) +
							   "' returned nullptr");
	}
	return estimator;
}

template <typename Creator>
std::vector<std::string> Names(const CreatorMap<Creator>& map)
{
	std::vector<std::string> names;
	names.reserve(map.size());
	for (const auto& entry : map)
	{
		names.push_back(entry.first);
	}
	return names;
}

constexpr std::string_view kSender = "sender";
constexpr std::string_view kReceiver = "receiver";

template <typename Core>
void RegisterBothSides(EstimatorFactory& factory)
{
	factory.RegisterSender(std::string(Core::kName),
						   internal::CreateSenderAdapter<Core>);
	factory.RegisterReceiver(std::string(Core::kName),
							 internal::CreateReceiverAdapter<Core>);
}

}  // namespace

struct EstimatorFactory::Impl
{
	mutable std::mutex mutex;
	CreatorMap<SenderCreator> senders;
	CreatorMap<ReceiverCreator> receivers;
};

EstimatorFactory::EstimatorFactory()
	: impl_(new Impl)
{
}

EstimatorFactory::~EstimatorFactory()
{
	delete impl_;
}

EstimatorFactory& EstimatorFactory::Instance()
{
	// Intentionally leaked to avoid destruction order issues.
	static EstimatorFactory* const instance = []
	{
		auto* factory = new EstimatorFactory();
		RegisterBothSides<internal::MathisCore>(*factory);
		RegisterBothSides<internal::TfrcCore>(*factory);
		RegisterBothSides<internal::AimdCore>(*factory);
		factory->RegisterSender(
			std::string(internal::SendBufferCore::kName),
			internal::CreateSenderAdapter<internal::SendBufferCore>);
		RegisterBothSides<internal::DelayCore>(*factory);
		RegisterBothSides<internal::LinkCapacityCore>(*factory);
		factory->RegisterReceiver(
			std::string(internal::TsbpdReserveCore::kName),
			internal::CreateReceiverAdapter<internal::TsbpdReserveCore>);
		RegisterBothSides<internal::HybridCore>(*factory);
		return factory;
	}();
	return *instance;
}

void EstimatorFactory::RegisterSender(std::string name,
									  SenderCreator creator)
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	Register(impl_->senders, std::move(name), std::move(creator), kSender);
}

void EstimatorFactory::RegisterReceiver(std::string name,
										ReceiverCreator creator)
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	Register(impl_->receivers, std::move(name), std::move(creator), kReceiver);
}

std::unique_ptr<SenderEstimator> EstimatorFactory::CreateSender(
	std::string_view name, const Parameters& parameters) const
{
	SenderCreator creator;
	{
		std::lock_guard<std::mutex> lock(impl_->mutex);
		creator = FindCreator(impl_->senders, name, kSender);
	}
	// The creator runs without holding the lock.
	return CheckNotNull(creator(parameters), name, kSender);
}

std::unique_ptr<ReceiverEstimator> EstimatorFactory::CreateReceiver(
	std::string_view name, const Parameters& parameters) const
{
	ReceiverCreator creator;
	{
		std::lock_guard<std::mutex> lock(impl_->mutex);
		creator = FindCreator(impl_->receivers, name, kReceiver);
	}
	// The creator runs without holding the lock.
	return CheckNotNull(creator(parameters), name, kReceiver);
}

bool EstimatorFactory::HasSenderAlgorithm(std::string_view name) const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->senders.find(name) != impl_->senders.end();
}

bool EstimatorFactory::HasReceiverAlgorithm(std::string_view name) const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->receivers.find(name) != impl_->receivers.end();
}

std::vector<std::string> EstimatorFactory::SenderAlgorithms() const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return Names(impl_->senders);
}

std::vector<std::string> EstimatorFactory::ReceiverAlgorithms() const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return Names(impl_->receivers);
}

}  // namespace bwe
