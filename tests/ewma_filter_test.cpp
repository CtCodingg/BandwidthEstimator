#include "filters/ewma_filter.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace bwe {
namespace internal {
namespace {

TEST(EwmaFilterTest, IsEmptyInitially) {
  EwmaFilter filter(0.5);
  EXPECT_FALSE(filter.HasValue());
  EXPECT_DOUBLE_EQ(filter.Confidence(), 0.0);
}

TEST(EwmaFilterTest, TakesFirstValueAsIs) {
  EwmaFilter filter(0.1);
  filter.Update(100.0);
  EXPECT_TRUE(filter.HasValue());
  EXPECT_DOUBLE_EQ(filter.Value(), 100.0);
}

TEST(EwmaFilterTest, SmoothsFollowingValues) {
  EwmaFilter filter(0.25);
  filter.Update(100.0);
  filter.Update(200.0);
  EXPECT_DOUBLE_EQ(filter.Value(), 125.0);
}

TEST(EwmaFilterTest, SmoothingOneKeepsLatestValue) {
  EwmaFilter filter(1.0);
  filter.Update(100.0);
  filter.Update(200.0);
  EXPECT_DOUBLE_EQ(filter.Value(), 200.0);
}

TEST(EwmaFilterTest, ConfidenceRisesToOne) {
  EwmaFilter filter(0.5, 4);
  filter.Update(1.0);
  EXPECT_DOUBLE_EQ(filter.Confidence(), 0.25);
  for (int i = 0; i < 10; ++i) {
    filter.Update(1.0);
  }
  EXPECT_DOUBLE_EQ(filter.Confidence(), 1.0);
}

TEST(EwmaFilterTest, ResetDiscardsValues) {
  EwmaFilter filter(0.5);
  filter.Update(100.0);
  filter.Reset();
  EXPECT_FALSE(filter.HasValue());
  EXPECT_DOUBLE_EQ(filter.Value(), 0.0);
}

TEST(EwmaFilterTest, MakeEstimateReflectsState) {
  EwmaFilter filter(0.5, 2);
  EXPECT_FALSE(MakeEstimate(filter, Duration{0}).valid);

  filter.Update(1e6);
  const BandwidthEstimate estimate =
      MakeEstimate(filter, std::chrono::seconds(3));
  EXPECT_TRUE(estimate.valid);
  EXPECT_DOUBLE_EQ(estimate.bits_per_second, 1e6);
  EXPECT_DOUBLE_EQ(estimate.confidence, 0.5);
  EXPECT_EQ(estimate.timestamp, std::chrono::seconds(3));
}

}  // namespace
}  // namespace internal
}  // namespace bwe
