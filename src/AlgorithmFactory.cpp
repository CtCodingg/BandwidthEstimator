#include "bwe/AlgorithmFactory.hpp"

#include "bwe/TfrcAlgorithm.hpp"

#include <stdexcept>

namespace bwe
{

std::unique_ptr<IAlgorithm> AlgorithmFactory::create(const Config& config)
{
	switch (config.algorithm)
	{
	case AlgorithmType::Tfrc:
		return std::make_unique<TfrcAlgorithm>(config.packetSizeBytes);
	}
	throw std::invalid_argument("bwe::AlgorithmFactory: unknown algorithm");
}

}
