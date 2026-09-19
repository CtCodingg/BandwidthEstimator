// Concurrent access to the thread-safe parts of the library. Checks are made
// in the main thread after joining; worker threads only record results.
// Run with ThreadSanitizer to detect data races.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "bwe/estimator_factory.hpp"
#include "bwe/recording.hpp"
#include "bwe/shared_link_allocator.hpp"

namespace bwe {
namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;

constexpr int kThreads = 4;
constexpr int kIterations = 2000;

ReceiverMeasurement Stats(int step) {
  ReceiverMeasurement stats;
  stats.common.timestamp = milliseconds(step);
  stats.common.rtt = milliseconds(20);
  stats.common.link_capacity_bps = 10e6;
  stats.common.mss_bytes = 1500;
  stats.packets_received = static_cast<std::uint64_t>(step) * 10;
  stats.packets_lost = 0;
  stats.bytes_received = static_cast<std::uint64_t>(step) * 13'160;
  stats.receive_buffer_delay = milliseconds(120);
  stats.tsbpd_delay = milliseconds(120);
  return stats;
}

void JoinAll(std::vector<std::thread>& threads) {
  for (std::thread& thread : threads) {
    thread.join();
  }
}

TEST(ThreadSafetyTest, AllocatorHandlesConcurrentAccess) {
  SharedLinkAllocator allocator;
  for (int flow = 0; flow < kThreads; ++flow) {
    allocator.AddFlow(static_cast<FlowId>(flow));
  }

  std::atomic<bool> running{true};
  std::atomic<int> implausible{0};
  std::vector<std::thread> threads;
  // One statistics thread per flow.
  for (int flow = 0; flow < kThreads; ++flow) {
    threads.emplace_back([&allocator, flow] {
      for (int step = 1; step <= kIterations; ++step) {
        allocator.Update(static_cast<FlowId>(flow), Stats(step));
      }
    });
  }
  // Flows joining and leaving.
  threads.emplace_back([&allocator] {
    for (int i = 0; i < kIterations / 10; ++i) {
      const FlowId flow = 100 + static_cast<FlowId>(i % 5);
      allocator.RemoveFlow(flow);
      allocator.AddFlow(flow);
      allocator.Update(flow, Stats(i + 1));
    }
  });
  // Timer thread.
  std::thread timer([&] {
    int tick = 0;
    while (running) {
      allocator.Tick(microseconds(++tick * 1000));
      for (const FlowAllocation& allocation : allocator.Allocate()) {
        if (!std::isfinite(allocation.target_bps) ||
            allocation.target_bps < 0.0) {
          ++implausible;
        }
      }
    }
    allocator.Tick(microseconds(++tick * 1000));
  });

  JoinAll(threads);
  running = false;
  timer.join();

  EXPECT_EQ(implausible.load(), 0);
  EXPECT_TRUE(allocator.GetTotalEstimate().valid);
  EXPECT_FALSE(allocator.Allocate().empty());
}

TEST(ThreadSafetyTest, WriterKeepsRecordsIntact) {
  std::ostringstream out;
  {
    RecordingWriter writer(out);
    std::vector<std::thread> threads;
    for (int flow = 0; flow < kThreads; ++flow) {
      threads.emplace_back([&writer, flow] {
        for (int step = 1; step <= kIterations / 4; ++step) {
          writer.WriteReceiver(milliseconds(step), static_cast<FlowId>(flow),
                               Stats(step));
        }
      });
    }
    JoinAll(threads);
  }

  std::istringstream in(out.str());
  std::vector<Record> records;
  ASSERT_NO_THROW(records = ReadRecording(in));
  std::map<FlowId, int> per_flow;
  for (const Record& record : records) {
    ASSERT_TRUE(record.flow.has_value());
    ++per_flow[*record.flow];
  }
  ASSERT_EQ(per_flow.size(), static_cast<std::size_t>(kThreads));
  for (const auto& [flow, count] : per_flow) {
    EXPECT_EQ(count, kIterations / 4) << "flow " << flow;
  }
}

TEST(ThreadSafetyTest, EstimatorHandlesConcurrentAccess) {
  const std::unique_ptr<ReceiverEstimator> estimator =
      EstimatorFactory::Instance().CreateReceiver("hybrid");
  std::atomic<bool> running{true};
  std::atomic<int> implausible{0};

  std::thread updater([&] {
    for (int step = 1; step <= kIterations; ++step) {
      estimator->Update(Stats(step));
    }
  });
  std::thread reader([&] {
    while (running) {
      const BandwidthEstimate estimate = estimator->GetEstimate();
      if (!std::isfinite(estimate.bits_per_second) ||
          estimate.bits_per_second < 0.0) {
        ++implausible;
      }
    }
  });
  std::thread resetter([&] {
    for (int i = 0; i < 20; ++i) {
      estimator->Reset();
      std::this_thread::yield();
    }
  });

  updater.join();
  resetter.join();
  running = false;
  reader.join();

  EXPECT_EQ(implausible.load(), 0);
  estimator->Update(Stats(kIterations + 1));
  estimator->Update(Stats(kIterations + 2));
  estimator->Update(Stats(kIterations + 3));
  EXPECT_TRUE(estimator->GetEstimate().valid);
}

TEST(ThreadSafetyTest, FactoryHandlesConcurrentAccess) {
  EstimatorFactory factory;
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&factory, &failures, t] {
      for (int i = 0; i < 50; ++i) {
        const std::string name =
            "algorithm_" + std::to_string(t) + "_" + std::to_string(i);
        factory.RegisterSender(name, [](const Parameters&) {
          return EstimatorFactory::Instance().CreateSender("aimd");
        });
        if (!factory.HasSenderAlgorithm(name) ||
            factory.CreateSender(name) == nullptr) {
          ++failures;
        }
        factory.SenderAlgorithms();
        if (EstimatorFactory::Instance().CreateReceiver("delay") == nullptr) {
          ++failures;
        }
      }
    });
  }
  JoinAll(threads);

  EXPECT_EQ(failures.load(), 0);
  EXPECT_EQ(factory.SenderAlgorithms().size(),
            static_cast<std::size_t>(kThreads * 50));
}

}  // namespace
}  // namespace bwe
