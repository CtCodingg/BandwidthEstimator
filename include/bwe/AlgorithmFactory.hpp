#pragma once

#include "bwe/IAlgorithm.hpp"
#include "bwe/Types.hpp"

#include <memory>

namespace bwe
{

/// Creates estimation algorithms.
class AlgorithmFactory
{
public:
	/// Creates the algorithm selected in @p config.
	/// @return The new algorithm, never null.
	/// @throws std::invalid_argument if the algorithm is unknown or its settings are invalid.
	static std::unique_ptr<IAlgorithm> create(const Config& config);
};

}
