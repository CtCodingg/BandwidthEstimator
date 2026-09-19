#include "parameter_reader.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace bwe {
namespace internal {
namespace {

TEST(RangeTest, RespectsBoundTypes) {
  EXPECT_FALSE(Range::Open(0.0, 1.0).Contains(0.0));
  EXPECT_TRUE(Range::Open(0.0, 1.0).Contains(0.5));
  EXPECT_FALSE(Range::Open(0.0, 1.0).Contains(1.0));
  EXPECT_FALSE(Range::OpenClosed(0.0, 1.0).Contains(0.0));
  EXPECT_TRUE(Range::OpenClosed(0.0, 1.0).Contains(1.0));
  EXPECT_TRUE(Range::Closed(0.0, 1.0).Contains(0.0));
  EXPECT_TRUE(Range::Closed(0.0, 1.0).Contains(1.0));
}

TEST(RangeTest, RejectsNan) {
  EXPECT_FALSE(Range::Closed(0.0, 1.0).Contains(
      std::numeric_limits<double>::quiet_NaN()));
}

TEST(RangeTest, FormatsInterval) {
  EXPECT_EQ(Range::Open(0.0, 1.0).ToString(), "(0, 1)");
  EXPECT_EQ(Range::OpenClosed(0.0, 10.0).ToString(), "(0, 10]");
  EXPECT_EQ(Range::Closed(1.0, 2.5).ToString(), "[1, 2.5]");
}

TEST(ParameterReaderTest, ReturnsDefaultForMissingKey) {
  const Parameters parameters;
  ParameterReader reader("test", parameters);
  EXPECT_DOUBLE_EQ(reader.Get("a", 3.0, Range::Closed(0.0, 10.0)), 3.0);
  EXPECT_NO_THROW(reader.CheckNoUnknownKeys());
}

TEST(ParameterReaderTest, ReturnsGivenValue) {
  const Parameters parameters = {{"a", 7.0}};
  ParameterReader reader("test", parameters);
  EXPECT_DOUBLE_EQ(reader.Get("a", 3.0, Range::Closed(0.0, 10.0)), 7.0);
  EXPECT_NO_THROW(reader.CheckNoUnknownKeys());
}

TEST(ParameterReaderTest, ThrowsOutOfRangeWithDetails) {
  const Parameters parameters = {{"a", 11.0}};
  ParameterReader reader("test", parameters);
  try {
    reader.Get("a", 3.0, Range::Closed(0.0, 10.0));
    FAIL() << "std::out_of_range expected";
  } catch (const std::out_of_range& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("test"), std::string::npos);
    EXPECT_NE(message.find("'a'"), std::string::npos);
    EXPECT_NE(message.find("[0, 10]"), std::string::npos);
  }
}

TEST(ParameterReaderTest, ThrowsInvalidArgumentForUnknownKey) {
  const Parameters parameters = {{"a", 1.0}, {"typo", 1.0}};
  ParameterReader reader("test", parameters);
  reader.Get("a", 3.0, Range::Closed(0.0, 10.0));
  try {
    reader.CheckNoUnknownKeys();
    FAIL() << "std::invalid_argument expected";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("'typo'"), std::string::npos);
  }
}

TEST(ParameterReaderTest, ReadsIntegers) {
  const Parameters parameters = {{"n", 7.0}};
  ParameterReader reader("test", parameters);
  EXPECT_EQ(reader.GetInteger("n", 3, 1, 10), 7);
  EXPECT_EQ(reader.GetInteger("missing", 3, 1, 10), 3);
}

TEST(ParameterReaderTest, RejectsInvalidIntegers) {
  const Parameters fraction = {{"n", 2.5}};
  ParameterReader fraction_reader("test", fraction);
  EXPECT_THROW(fraction_reader.GetInteger("n", 3, 1, 10),
               std::invalid_argument);

  const Parameters too_large = {{"n", 11.0}};
  ParameterReader range_reader("test", too_large);
  EXPECT_THROW(range_reader.GetInteger("n", 3, 1, 10), std::out_of_range);
}

}  // namespace
}  // namespace internal
}  // namespace bwe
