#include "parameter_reader.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace bwe
{
namespace internal
{
namespace
{

std::string FormatNumber(double value)
{
	std::ostringstream stream;
	stream << value;
	return stream.str();
}

}  // namespace

bool Range::Contains(double value) const noexcept
{
	const bool above_lower =
		lower_inclusive ? value >= lower : value > lower;
	const bool below_upper =
		upper_inclusive ? value <= upper : value < upper;
	return above_lower && below_upper;
}

std::string Range::ToString() const
{
	return (lower_inclusive ? "[" : "(") + FormatNumber(lower) + ", " +
		   FormatNumber(upper) + (upper_inclusive ? "]" : ")");
}

ParameterReader::ParameterReader(std::string_view algorithm,
								 const Parameters& parameters)
	: algorithm_(algorithm), parameters_(&parameters)
{
}

double ParameterReader::Get(std::string_view key, double default_value,
							const Range& range)
{
	read_keys_.emplace(key);
	const auto it = parameters_->find(key);
	if (it == parameters_->end())
	{
		return default_value;
	}
	if (!range.Contains(it->second))
	{
		throw std::out_of_range("bwe: " + algorithm_ + " parameter '" +
								std::string(key) + "' = " +
								FormatNumber(it->second) + " is outside " +
								range.ToString());
	}
	return it->second;
}

int ParameterReader::GetInteger(std::string_view key, int default_value,
								int lower, int upper)
{
	const auto it = parameters_->find(key);
	if (it != parameters_->end() && std::isfinite(it->second) &&
		std::trunc(it->second) != it->second)
	{
		throw std::invalid_argument("bwe: " + algorithm_ + " parameter '" +
									std::string(key) + "' = " +
									FormatNumber(it->second) +
									" is not an integer");
	}
	return static_cast<int>(Get(key, default_value, Range::Closed(lower, upper)));
}

void ParameterReader::CheckNoUnknownKeys() const
{
	for (const auto& entry : *parameters_)
	{
		if (read_keys_.find(entry.first) == read_keys_.end())
		{
			throw std::invalid_argument("bwe: unknown " + algorithm_ +
										" parameter '" + entry.first + "'");
		}
	}
}

}  // namespace internal
}  // namespace bwe
