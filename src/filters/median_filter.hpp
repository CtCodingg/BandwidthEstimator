/// @file
/// @brief Sliding-window median without dynamic allocation.

#ifndef BWE_SRC_FILTERS_MEDIAN_FILTER_HPP_
#define BWE_SRC_FILTERS_MEDIAN_FILTER_HPP_

#include <algorithm>
#include <array>

namespace bwe {
namespace internal {

/// @brief Median of the last `window_size` values.
class MedianFilter {
 public:
  /// @brief Largest supported window size.
  static constexpr int kMaxWindowSize = 31;

  /// @param window_size Number of values; clamped to [1, kMaxWindowSize].
  explicit MedianFilter(int window_size) noexcept
      : window_size_(std::clamp(window_size, 1, kMaxWindowSize)) {}

  /// @brief Adds a value, replacing the oldest one when the window is full.
  void Update(double value) noexcept {
    values_[next_] = value;
    next_ = (next_ + 1) % window_size_;
    count_ = std::min(count_ + 1, window_size_);
  }

  /// @brief Returns true once a value has been added.
  bool HasValue() const noexcept { return count_ > 0; }

  /// @brief Returns the median; the mean of the two middle values for an
  ///        even count; 0 without values.
  double Value() const noexcept {
    if (count_ == 0) {
      return 0.0;
    }
    std::array<double, kMaxWindowSize> sorted = values_;
    std::sort(sorted.begin(), sorted.begin() + count_);
    const int middle = count_ / 2;
    return count_ % 2 == 1 ? sorted[middle]
                           : (sorted[middle - 1] + sorted[middle]) / 2.0;
  }

  /// @brief Discards all values.
  void Reset() noexcept {
    count_ = 0;
    next_ = 0;
  }

 private:
  std::array<double, kMaxWindowSize> values_{};
  int window_size_;
  int count_ = 0;
  int next_ = 0;
};

}  // namespace internal
}  // namespace bwe

#endif  // BWE_SRC_FILTERS_MEDIAN_FILTER_HPP_
