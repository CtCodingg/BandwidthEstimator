/// @file
/// @brief Interfaces of the bandwidth estimators.

#pragma once

#include <mutex>
#include <string_view>

#include "bwe/bandwidth_estimate.hpp"
#include "bwe/export.hpp"
#include "bwe/measurement.hpp"

namespace bwe 
{

/// @brief Common base of all estimators.
///
/// Thread-safe: the public member functions may be called concurrently
/// from any thread. They lock a mutex of the estimator and call the
/// protected Do...() functions, which implementations override and which
/// therefore never run concurrently. No member function throws; a failing
/// mutex lock (resource exhaustion) terminates the program.
class BWE_API Estimator 
{
 public:
  Estimator(const Estimator&) = delete;
  Estimator& operator=(const Estimator&) = delete;
  virtual ~Estimator() = default;

  /// @brief Returns the current estimate.
  BandwidthEstimate GetEstimate() const noexcept 
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return DoGetEstimate();
  }

  /// @brief Discards the internal state, keeps the configuration.
  void Reset() noexcept 
  {
    std::lock_guard<std::mutex> lock(mutex_);
    DoReset();
  }

  /// @brief Returns the registered algorithm name; must not change.
  virtual std::string_view Name() const noexcept = 0;

 protected:
  Estimator() = default;

  /// @brief Returns the mutex guarding the estimator state.
  std::mutex& Mutex() const noexcept { return mutex_; }

  /// @brief Implements GetEstimate(); called with the mutex held.
  virtual BandwidthEstimate DoGetEstimate() const noexcept = 0;

  /// @brief Implements Reset(); called with the mutex held.
  virtual void DoReset() noexcept = 0;

 private:
  mutable std::mutex mutex_;
};

/// @brief Estimator based on sender-side statistics.
class BWE_API SenderEstimator : public Estimator 
{
 public:
  /// @brief Processes one statistics sample.
  /// @param measurement Sender-side statistics.
  void Update(const SenderMeasurement& measurement) noexcept 
  {
    std::lock_guard<std::mutex> lock(Mutex());
    DoUpdate(measurement);
  }

 protected:
  /// @brief Implements Update(); called with the mutex held.
  virtual void DoUpdate(const SenderMeasurement& measurement) noexcept = 0;
};

/// @brief Estimator based on receiver-side statistics.
class BWE_API ReceiverEstimator : public Estimator 
{
 public:
  /// @brief Processes one statistics sample.
  /// @param measurement Receiver-side statistics.
  void Update(const ReceiverMeasurement& measurement) noexcept 
  {
    std::lock_guard<std::mutex> lock(Mutex());
    DoUpdate(measurement);
  }

 protected:
  /// @brief Implements Update(); called with the mutex held.
  virtual void DoUpdate(const ReceiverMeasurement& measurement) noexcept = 0;
};

}  // namespace bwe
