#include "algorithms/tfrc_estimator.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "algorithms/mathis_estimator.hpp"

namespace bwe {
namespace internal {
namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;

IntervalSample MakeSample(double loss_rate) {
  IntervalSample sample;
  sample.timestamp = seconds(1);
  sample.duration = seconds(1);
  sample.rtt = milliseconds(100);
  sample.mss_bytes = 1500;
  sample.loss_rate = loss_rate;
  return sample;
}

// Reference implementation of the RFC 5348 equation in bit/s.
double ExpectedBps(double s, double r, double p, double b, double t_rto) {
  return s * 8.0 /
         (r * std::sqrt(2.0 * b * p / 3.0) +
          t_rto * 3.0 * std::sqrt(3.0 * b * p / 8.0) * p *
              (1.0 + 32.0 * p * p));
}

TEST(TfrcParametersTest, UsesDefaultsForEmptyParameters) {
  const TfrcParameters parameters = TfrcCore::ParseParameters({});
  EXPECT_DOUBLE_EQ(parameters.packets_per_ack, 1.0);
  EXPECT_DOUBLE_EQ(parameters.rto_factor, 4.0);
  EXPECT_DOUBLE_EQ(parameters.min_loss_rate, 1e-4);
  EXPECT_DOUBLE_EQ(parameters.smoothing, 0.3);
}

TEST(TfrcParametersTest, AcceptsValidValues) {
  const TfrcParameters parameters = TfrcCore::ParseParameters(
      {{"packets_per_ack", 2.0}, {"rto_factor", 1.0}});
  EXPECT_DOUBLE_EQ(parameters.packets_per_ack, 2.0);
  EXPECT_DOUBLE_EQ(parameters.rto_factor, 1.0);
}

TEST(TfrcParametersTest, RejectsUnknownKey) {
  EXPECT_THROW(TfrcCore::ParseParameters({{"constant", 1.0}}),
               std::invalid_argument);
}

TEST(TfrcParametersTest, RejectsValuesOutOfRange) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(TfrcCore::ParseParameters({{"packets_per_ack", 0.0}}),
               std::out_of_range);
  EXPECT_THROW(TfrcCore::ParseParameters({{"rto_factor", 101.0}}),
               std::out_of_range);
  EXPECT_THROW(TfrcCore::ParseParameters({{"rto_factor", nan}}),
               std::out_of_range);
  EXPECT_THROW(TfrcCore::ParseParameters({{"min_loss_rate", 1.0}}),
               std::out_of_range);
  EXPECT_THROW(TfrcCore::ParseParameters({{"smoothing", 0.0}}),
               std::out_of_range);
}

TEST(TfrcCoreTest, IsInvalidWithoutSamples) {
  TfrcCore core(TfrcParameters{});
  EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(TfrcCoreTest, AppliesEquation) {
  TfrcCore core(TfrcParameters{});
  core.Update(MakeSample(0.01));

  const BandwidthEstimate estimate = core.GetEstimate();
  EXPECT_TRUE(estimate.valid);
  EXPECT_EQ(estimate.timestamp, seconds(1));
  EXPECT_NEAR(estimate.bits_per_second,
              ExpectedBps(1500, 0.1, 0.01, 1.0, 0.4), 1e-6);
}

TEST(TfrcCoreTest, UsesConfiguredParameters) {
  TfrcParameters parameters;
  parameters.packets_per_ack = 2.0;
  parameters.rto_factor = 2.0;
  TfrcCore core(parameters);
  core.Update(MakeSample(0.05));
  EXPECT_NEAR(core.GetEstimate().bits_per_second,
              ExpectedBps(1500, 0.1, 0.05, 2.0, 0.2), 1e-6);
}

TEST(TfrcCoreTest, UsesMinimumLossRateWithoutLoss) {
  TfrcCore core(TfrcParameters{});
  core.Update(MakeSample(0.0));
  EXPECT_NEAR(core.GetEstimate().bits_per_second,
              ExpectedBps(1500, 0.1, 1e-4, 1.0, 0.4), 1e-6);
}

TEST(TfrcCoreTest, MatchesMathisAtLowLoss) {
  TfrcCore tfrc(TfrcParameters{});
  MathisParameters mathis_parameters;
  mathis_parameters.constant = std::sqrt(1.5);
  MathisCore mathis(mathis_parameters);

  tfrc.Update(MakeSample(1e-4));
  mathis.Update(MakeSample(1e-4));

  const double ratio = tfrc.GetEstimate().bits_per_second /
                       mathis.GetEstimate().bits_per_second;
  EXPECT_NEAR(ratio, 1.0, 0.01);
}

TEST(TfrcCoreTest, IsBelowMathisAtHighLoss) {
  TfrcCore tfrc(TfrcParameters{});
  MathisParameters mathis_parameters;
  mathis_parameters.constant = std::sqrt(1.5);
  MathisCore mathis(mathis_parameters);

  tfrc.Update(MakeSample(0.1));
  mathis.Update(MakeSample(0.1));

  EXPECT_LT(tfrc.GetEstimate().bits_per_second,
            0.5 * mathis.GetEstimate().bits_per_second);
}

TEST(TfrcCoreTest, SmoothsEstimates) {
  TfrcParameters parameters;
  parameters.smoothing = 0.5;
  TfrcCore core(parameters);
  core.Update(MakeSample(0.01));
  core.Update(MakeSample(0.04));

  const double first = ExpectedBps(1500, 0.1, 0.01, 1.0, 0.4);
  const double second = ExpectedBps(1500, 0.1, 0.04, 1.0, 0.4);
  EXPECT_NEAR(core.GetEstimate().bits_per_second, (first + second) / 2,
              1e-6);
}

TEST(TfrcCoreTest, IgnoresIncompleteSamples) {
  TfrcCore core(TfrcParameters{});
  IntervalSample without_rtt = MakeSample(0.01);
  without_rtt.rtt.reset();
  IntervalSample without_loss = MakeSample(0.01);
  without_loss.loss_rate.reset();
  IntervalSample without_mss = MakeSample(0.01);
  without_mss.mss_bytes.reset();

  core.Update(without_rtt);
  core.Update(without_loss);
  core.Update(without_mss);
  EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(TfrcCoreTest, ResetDiscardsEstimate) {
  TfrcCore core(TfrcParameters{});
  core.Update(MakeSample(0.01));
  core.Reset();
  EXPECT_FALSE(core.GetEstimate().valid);
}

TEST(TfrcEstimatorTest, WorksOnBothSides) {
  TfrcSenderEstimator sender(TfrcParameters{});
  SenderMeasurement first;
  first.common.timestamp = seconds(0);
  first.packets_sent = 0;
  first.packets_lost = 0;
  SenderMeasurement second = first;
  second.common.timestamp = seconds(1);
  second.common.rtt = milliseconds(100);
  second.common.mss_bytes = 1500;
  second.packets_sent = 1000;
  second.packets_lost = 10;
  sender.Update(first);
  sender.Update(second);

  TfrcReceiverEstimator receiver(TfrcParameters{});
  ReceiverMeasurement first_received;
  first_received.common.timestamp = seconds(0);
  first_received.packets_received = 0;
  first_received.packets_lost = 0;
  ReceiverMeasurement second_received = first_received;
  second_received.common.timestamp = seconds(1);
  second_received.common.rtt = milliseconds(100);
  second_received.common.mss_bytes = 1500;
  second_received.packets_received = 990;
  second_received.packets_lost = 10;
  receiver.Update(first_received);
  receiver.Update(second_received);

  const double expected = ExpectedBps(1500, 0.1, 0.01, 1.0, 0.4);
  EXPECT_NEAR(sender.GetEstimate().bits_per_second, expected, 1e-6);
  EXPECT_NEAR(receiver.GetEstimate().bits_per_second, expected, 1e-6);
  EXPECT_EQ(sender.Name(), "tfrc");
  EXPECT_EQ(receiver.Name(), "tfrc");
}

TEST(TfrcRegistrationTest, IsAvailableInGlobalFactory) {
  EstimatorFactory& factory = EstimatorFactory::Instance();
  ASSERT_TRUE(factory.HasSenderAlgorithm("tfrc"));
  ASSERT_TRUE(factory.HasReceiverAlgorithm("tfrc"));
  EXPECT_EQ(factory.CreateSender("tfrc")->Name(), "tfrc");
  EXPECT_THROW(factory.CreateReceiver("tfrc", {{"rto_factor", 0.0}}),
               std::out_of_range);
}

}  // namespace
}  // namespace internal
}  // namespace bwe
