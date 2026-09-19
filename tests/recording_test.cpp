#include "bwe/recording.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace bwe {
namespace {

using std::chrono::milliseconds;

SenderMeasurement FullSender() {
  SenderMeasurement m;
  m.common.timestamp = milliseconds(1234);
  m.common.rtt = std::chrono::microseconds(40'123);
  m.common.link_capacity_bps = 0.1;
  m.common.mss_bytes = 1500;
  m.packets_sent = 1;
  m.packets_sent_unique = 2;
  m.packets_lost = 3;
  m.packets_retransmitted = 4;
  m.packets_dropped = 5;
  m.bytes_sent = 18'446'744'073'709'551'615u;  // Largest 64-bit value.
  m.bytes_sent_unique = 7;
  m.bytes_retransmitted = 8;
  m.bytes_dropped = 9;
  m.send_buffer_delay = milliseconds(120);
  m.send_buffer_bytes = 10;
  m.send_buffer_packets = 11;
  m.flight_size_packets = 12;
  m.congestion_window_packets = 13;
  m.max_bandwidth_bps = 123456789.123456789;
  return m;
}

ReceiverMeasurement FullReceiver() {
  ReceiverMeasurement m;
  m.common.timestamp = milliseconds(999);
  m.common.rtt = milliseconds(20);
  m.common.link_capacity_bps = 1e-300;
  m.common.mss_bytes = 1456;
  m.packets_received = 21;
  m.packets_received_unique = 22;
  m.packets_lost = 23;
  m.packets_dropped = 24;
  m.packets_belated = 25;
  m.bytes_received = 26;
  m.bytes_received_unique = 27;
  m.bytes_lost = 28;
  m.bytes_dropped = 29;
  m.receive_buffer_delay = milliseconds(80);
  m.receive_buffer_bytes = 30;
  m.receive_buffer_packets = 31;
  m.tsbpd_delay = milliseconds(120);
  m.reorder_distance_packets = 32;
  return m;
}

void ExpectEqual(const CommonStats& a, const CommonStats& b) {
  EXPECT_EQ(a.timestamp, b.timestamp);
  EXPECT_EQ(a.rtt, b.rtt);
  EXPECT_EQ(a.link_capacity_bps, b.link_capacity_bps);
  EXPECT_EQ(a.mss_bytes, b.mss_bytes);
}

void ExpectEqual(const SenderMeasurement& a, const SenderMeasurement& b) {
  ExpectEqual(a.common, b.common);
  EXPECT_EQ(a.packets_sent, b.packets_sent);
  EXPECT_EQ(a.packets_sent_unique, b.packets_sent_unique);
  EXPECT_EQ(a.packets_lost, b.packets_lost);
  EXPECT_EQ(a.packets_retransmitted, b.packets_retransmitted);
  EXPECT_EQ(a.packets_dropped, b.packets_dropped);
  EXPECT_EQ(a.bytes_sent, b.bytes_sent);
  EXPECT_EQ(a.bytes_sent_unique, b.bytes_sent_unique);
  EXPECT_EQ(a.bytes_retransmitted, b.bytes_retransmitted);
  EXPECT_EQ(a.bytes_dropped, b.bytes_dropped);
  EXPECT_EQ(a.send_buffer_delay, b.send_buffer_delay);
  EXPECT_EQ(a.send_buffer_bytes, b.send_buffer_bytes);
  EXPECT_EQ(a.send_buffer_packets, b.send_buffer_packets);
  EXPECT_EQ(a.flight_size_packets, b.flight_size_packets);
  EXPECT_EQ(a.congestion_window_packets, b.congestion_window_packets);
  EXPECT_EQ(a.max_bandwidth_bps, b.max_bandwidth_bps);
}

void ExpectEqual(const ReceiverMeasurement& a, const ReceiverMeasurement& b) {
  ExpectEqual(a.common, b.common);
  EXPECT_EQ(a.packets_received, b.packets_received);
  EXPECT_EQ(a.packets_received_unique, b.packets_received_unique);
  EXPECT_EQ(a.packets_lost, b.packets_lost);
  EXPECT_EQ(a.packets_dropped, b.packets_dropped);
  EXPECT_EQ(a.packets_belated, b.packets_belated);
  EXPECT_EQ(a.bytes_received, b.bytes_received);
  EXPECT_EQ(a.bytes_received_unique, b.bytes_received_unique);
  EXPECT_EQ(a.bytes_lost, b.bytes_lost);
  EXPECT_EQ(a.bytes_dropped, b.bytes_dropped);
  EXPECT_EQ(a.receive_buffer_delay, b.receive_buffer_delay);
  EXPECT_EQ(a.receive_buffer_bytes, b.receive_buffer_bytes);
  EXPECT_EQ(a.receive_buffer_packets, b.receive_buffer_packets);
  EXPECT_EQ(a.tsbpd_delay, b.tsbpd_delay);
  EXPECT_EQ(a.reorder_distance_packets, b.reorder_distance_packets);
}

std::vector<Record> ReadText(const std::string& text) {
  std::istringstream in(text);
  return ReadRecording(in);
}

std::string WriteRecords(const std::vector<Record>& records) {
  std::ostringstream out;
  RecordingWriter writer(out);
  for (const Record& record : records) {
    writer.Write(record);
  }
  return out.str();
}

std::string WriteAllTypes() {
  std::ostringstream out;
  RecordingWriter writer(out);
  writer.WriteSender(milliseconds(500), 1, FullSender());
  writer.WriteReceiver(milliseconds(500), 2, FullReceiver());
  writer.WriteEstimate(milliseconds(500), std::nullopt, "hybrid",
                       BandwidthEstimate{4.2e6, 0.6, milliseconds(400), true});
  writer.WriteAllocation(milliseconds(500), FlowAllocation{2, 3.3e6});
  writer.WriteTrueCapacity(milliseconds(500), 12e6);
  return out.str();
}

void ExpectReadError(const std::string& text, const std::string& expected) {
  try {
    ReadText(text);
    FAIL() << "std::invalid_argument expected for: " << text;
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find(expected), std::string::npos)
        << error.what();
  }
}

const std::string kHeader = "# bwe-recording v1\n";

TEST(RecordingTest, RoundTripsAllRecordTypes) {
  const std::string text = WriteAllTypes();
  const std::vector<Record> records = ReadText(text);
  ASSERT_EQ(records.size(), 5u);

  EXPECT_EQ(records[0].time, milliseconds(500));
  EXPECT_EQ(records[0].flow, FlowId{1});
  ExpectEqual(std::get<SenderMeasurement>(records[0].data), FullSender());

  EXPECT_EQ(records[1].flow, FlowId{2});
  ExpectEqual(std::get<ReceiverMeasurement>(records[1].data),
              FullReceiver());

  const auto& estimate = std::get<EstimateRecord>(records[2].data);
  EXPECT_FALSE(records[2].flow.has_value());
  EXPECT_EQ(estimate.algorithm, "hybrid");
  EXPECT_EQ(estimate.estimate.bits_per_second, 4.2e6);
  EXPECT_EQ(estimate.estimate.confidence, 0.6);
  EXPECT_EQ(estimate.estimate.timestamp, milliseconds(400));
  EXPECT_TRUE(estimate.estimate.valid);

  const auto& allocation = std::get<FlowAllocation>(records[3].data);
  EXPECT_EQ(allocation.flow, FlowId{2});
  EXPECT_EQ(allocation.target_bps, 3.3e6);

  EXPECT_FALSE(records[4].flow.has_value());
  EXPECT_EQ(std::get<TrueCapacityRecord>(records[4].data).capacity_bps, 12e6);

  // Writing the read records again yields the identical text.
  EXPECT_EQ(WriteRecords(records), text);
}

TEST(RecordingTest, LeavesUnsetFieldsEmpty) {
  SenderMeasurement sparse;
  sparse.common.timestamp = milliseconds(7);
  std::ostringstream out;
  RecordingWriter writer(out);
  writer.WriteSender(milliseconds(7), 1, sparse);

  const std::vector<Record> records = ReadText(out.str());
  ASSERT_EQ(records.size(), 1u);
  ExpectEqual(std::get<SenderMeasurement>(records[0].data), sparse);
}

TEST(RecordingTest, RoundTripsSpecialValues) {
  ReceiverMeasurement m;
  m.common.link_capacity_bps = std::numeric_limits<double>::quiet_NaN();
  std::ostringstream out;
  RecordingWriter writer(out);
  writer.WriteReceiver(Duration{0}, 1, m);
  writer.WriteTrueCapacity(Duration{0},
                           std::numeric_limits<double>::infinity());
  writer.WriteAllocation(Duration{0},
                         {1, -std::numeric_limits<double>::infinity()});

  const std::vector<Record> records = ReadText(out.str());
  ASSERT_EQ(records.size(), 3u);
  EXPECT_TRUE(std::isnan(
      *std::get<ReceiverMeasurement>(records[0].data).common
           .link_capacity_bps));
  EXPECT_EQ(std::get<TrueCapacityRecord>(records[1].data).capacity_bps,
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(std::get<FlowAllocation>(records[2].data).target_bps,
            -std::numeric_limits<double>::infinity());
}

// Decimal comma as in a German locale.
struct CommaDecimal : std::numpunct<char> {
  char do_decimal_point() const override { return ','; }
  char do_thousands_sep() const override { return '.'; }
  std::string do_grouping() const override { return "\3"; }
};

TEST(RecordingTest, IsIndependentOfGlobalLocale) {
  const std::locale previous = std::locale::global(
      std::locale(std::locale::classic(), new CommaDecimal));
  std::ostringstream out;
  {
    RecordingWriter writer(out);
    writer.WriteTrueCapacity(Duration{0}, 1500000.25);
  }
  std::vector<Record> records;
  std::string error;
  try {
    records = ReadText(out.str());
  } catch (const std::exception& e) {
    error = e.what();
  }
  std::locale::global(previous);

  EXPECT_EQ(error, "");
  EXPECT_NE(out.str().find("1500000.25"), std::string::npos) << out.str();
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(std::get<TrueCapacityRecord>(records[0].data).capacity_bps,
            1500000.25);
}

TEST(RecordingTest, EscapesAlgorithmNames) {
  std::ostringstream out;
  RecordingWriter writer(out);
  writer.WriteEstimate(Duration{0}, 1, "my,\"algo\"", BandwidthEstimate{});

  const std::vector<Record> records = ReadText(out.str());
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(std::get<EstimateRecord>(records[0].data).algorithm,
            "my,\"algo\"");
}

TEST(RecordingTest, AcceptsWindowsLineEndings) {
  std::string text = WriteAllTypes();
  std::string windows;
  for (char c : text) {
    if (c == '\n') {
      windows += '\r';
    }
    windows += c;
  }
  const std::vector<Record> records = ReadText(windows);
  ASSERT_EQ(records.size(), 5u);
  ExpectEqual(std::get<ReceiverMeasurement>(records[1].data),
              FullReceiver());
}

TEST(RecordingTest, AcceptsMissingAndReorderedColumns) {
  const std::vector<Record> records =
      ReadText(kHeader +
               "flow,record,time_us,timestamp_us,rtt_us\n"
               "3,receiver,1000,900,20000\n");
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records[0].flow, FlowId{3});
  EXPECT_EQ(records[0].time, std::chrono::microseconds(1000));
  const auto& m = std::get<ReceiverMeasurement>(records[0].data);
  EXPECT_EQ(m.common.timestamp, std::chrono::microseconds(900));
  EXPECT_EQ(m.common.rtt, milliseconds(20));
  EXPECT_FALSE(m.packets_received.has_value());
}

TEST(RecordingTest, ReportsMalformedInputWithLineNumber) {
  const std::string columns = "record,time_us,flow,timestamp_us,rtt_us\n";
  ExpectReadError("record,time_us\n", "line 1");
  ExpectReadError("# bwe-recording v2\n" + columns, "line 1");
  ExpectReadError(kHeader + "record,time_us,unknown\n", "line 2");
  ExpectReadError(kHeader + "record,time_us,time_us\n", "duplicate");
  ExpectReadError(kHeader + columns + "receiver,1,2,3\n", "line 3");
  ExpectReadError(kHeader + columns + "unknown,1,2,3,4\n", "record type");
  ExpectReadError(kHeader + columns + "receiver,1,2,3,abc\n", "abc");
  ExpectReadError(kHeader + columns + "receiver,1,2,,4\n", "timestamp_us");
  ExpectReadError(kHeader + columns + "receiver,1,2,3,\"4\n", "quote");
  ExpectReadError(kHeader + "record,time_us,target_bps\nallocation,1,5\n",
                  "flow");
  ExpectReadError(kHeader +
                      "record,time_us,algorithm,bits_per_second,confidence,"
                      "valid\nestimate,1,a,1,1,2\n",
                  "valid");
}

}  // namespace
}  // namespace bwe
