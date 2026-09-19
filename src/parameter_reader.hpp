/// @file
/// @brief Validated reading of algorithm parameters.

#ifndef BWE_SRC_PARAMETER_READER_HPP_
#define BWE_SRC_PARAMETER_READER_HPP_

#include <functional>
#include <set>
#include <string>
#include <string_view>

#include "bwe/estimator_factory.hpp"

namespace bwe {
namespace internal {

/// @brief Interval of valid parameter values.
struct Range {
  double lower = 0.0;
  double upper = 0.0;
  bool lower_inclusive = true;
  bool upper_inclusive = true;

  /// @brief Interval (lower, upper).
  static constexpr Range Open(double lower, double upper) {
    return {lower, upper, false, false};
  }
  /// @brief Interval (lower, upper].
  static constexpr Range OpenClosed(double lower, double upper) {
    return {lower, upper, false, true};
  }
  /// @brief Interval [lower, upper].
  static constexpr Range Closed(double lower, double upper) {
    return {lower, upper, true, true};
  }

  /// @brief Checks whether `value` lies in the interval; false for NaN.
  bool Contains(double value) const noexcept;

  /// @brief Returns the interval in mathematical notation, e.g. "(0, 1]".
  std::string ToString() const;
};

/// @brief Reads parameters with defaults and range checks.
class ParameterReader {
 public:
  /// @param algorithm Algorithm name used in error messages.
  /// @param parameters Parameters to read; must outlive the reader.
  ParameterReader(std::string_view algorithm, const Parameters& parameters);

  /// @brief Returns the value of `key`, or `default_value` if not given.
  /// @throws std::out_of_range Given value lies outside `range`.
  double Get(std::string_view key, double default_value, const Range& range);

  /// @brief Returns the integral value of `key`, or `default_value` if not
  ///        given.
  /// @throws std::invalid_argument Given value is not integral.
  /// @throws std::out_of_range Given value lies outside [lower, upper].
  int GetInteger(std::string_view key, int default_value, int lower,
                 int upper);

  /// @brief Checks that every given key has been read.
  /// @throws std::invalid_argument A key was not read by Get().
  void CheckNoUnknownKeys() const;

 private:
  std::string algorithm_;
  const Parameters* parameters_;
  std::set<std::string, std::less<>> read_keys_;
};

}  // namespace internal
}  // namespace bwe

#endif  // BWE_SRC_PARAMETER_READER_HPP_
