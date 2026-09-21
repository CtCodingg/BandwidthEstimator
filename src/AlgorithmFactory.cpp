#include "bwe/AlgorithmFactory.hpp"

#include "bwe/AimdAlgorithm.hpp"
#include "bwe/TfrcAlgorithm.hpp"

#include <stdexcept>

namespace bwe
{

std::unique_ptr<IAlgorithm> AlgorithmFactory::Create(const Config& config)
{
	switch (config.algorithm)
	{
	case AlgorithmType::kTfrc:
		return std::make_unique<TfrcAlgorithm>(config.packet_size_bytes);
	case AlgorithmType::kAimd:
		return std::make_unique<AimdAlgorithm>(config.packet_size_bytes);
	}
	throw std::invalid_argument("bwe::AlgorithmFactory: unknown algorithm");
}

}
