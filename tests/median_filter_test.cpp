#include "filters/median_filter.hpp"

#include <gtest/gtest.h>

namespace bwe {
namespace internal {
namespace {

TEST(MedianFilterTest, IsEmptyInitially) {
  MedianFilter filter(3);
  EXPECT_FALSE(filter.HasValue());
  EXPECT_DOUBLE_EQ(filter.Value(), 0.0);
}

TEST(MedianFilterTest, ReturnsMedianOfOddCount) {
  MedianFilter filter(3);
  filter.Update(10.0);
  filter.Update(100.0);
  filter.Update(20.0);
  EXPECT_DOUBLE_EQ(filter.Value(), 20.0);
}

TEST(MedianFilterTest, AveragesMiddleValuesOfEvenCount) {
  MedianFilter filter(4);
  filter.Update(10.0);
  filter.Update(20.0);
  EXPECT_DOUBLE_EQ(filter.Value(), 15.0);
}

TEST(MedianFilterTest, DropsOldestValue) {
  MedianFilter filter(3);
  filter.Update(1.0);
  filter.Update(100.0);
  filter.Update(100.0);
  filter.Update(2.0);
  filter.Update(3.0);
  EXPECT_DOUBLE_EQ(filter.Value(), 3.0);
}

TEST(MedianFilterTest, ClampsWindowSize) {
  MedianFilter filter(0);
  filter.Update(1.0);
  filter.Update(2.0);
  EXPECT_DOUBLE_EQ(filter.Value(), 2.0);
}

TEST(MedianFilterTest, ResetDiscardsValues) {
  MedianFilter filter(3);
  filter.Update(1.0);
  filter.Reset();
  EXPECT_FALSE(filter.HasValue());
}

}  // namespace
}  // namespace internal
}  // namespace bwe
