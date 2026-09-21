#pragma once

#include "bwe/Types.hpp"

namespace bwe
{

/// Interface of an estimation algorithm.
/// Calls are serialized by the Estimator, implementations need no own locking.
class IAlgorithm
{
public:
	virtual ~IAlgorithm() = default;

	/// Calculates the rate the sender of a stream may use.
	/// @param input Current measurements of the stream.
	/// @return Estimated rate in bit/s.
	/// @throws std::invalid_argument if a value of @p input is out of range.
	virtual double estimate(const Input& input) = 0;
};

}
