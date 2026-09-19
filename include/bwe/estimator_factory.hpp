/// @file
/// @brief Factory for creating bandwidth estimators by name.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "bwe/bandwidth_estimator.hpp"
#include "bwe/export.hpp"

namespace bwe
{

/// @brief Algorithm parameters as key/value pairs.
using Parameters = std::map<std::string, double, std::less<>>;

/// @brief Creates a sender-side estimator.
using SenderCreator =
	std::function<std::unique_ptr<SenderEstimator>(const Parameters&)>;

/// @brief Creates a receiver-side estimator.
using ReceiverCreator =
	std::function<std::unique_ptr<ReceiverEstimator>(const Parameters&)>;

/// @brief Creates bandwidth estimators by algorithm name.
/// @note Thread-safe. Failing calls leave the factory unchanged.
class BWE_API EstimatorFactory
{
public:
	/// @brief Creates an empty factory.
	EstimatorFactory();
	~EstimatorFactory();

	EstimatorFactory(const EstimatorFactory&) = delete;
	EstimatorFactory& operator=(const EstimatorFactory&) = delete;

	/// @brief Returns the global factory with all built-in algorithms.
	static EstimatorFactory& Instance();

	/// @brief Registers a sender-side algorithm.
	/// @param name Unique algorithm name.
	/// @param creator Creates the estimator.
	/// @throws std::invalid_argument Empty name or creator, or name in use.
	void RegisterSender(std::string name, SenderCreator creator);

	/// @brief Registers a receiver-side algorithm.
	/// @param name Unique algorithm name.
	/// @param creator Creates the estimator.
	/// @throws std::invalid_argument Empty name or creator, or name in use.
	void RegisterReceiver(std::string name, ReceiverCreator creator);

	/// @brief Creates a sender-side estimator.
	/// @param name Registered algorithm name.
	/// @param parameters Algorithm parameters; missing keys use defaults.
	/// @return The new estimator, never nullptr.
	/// @throws std::invalid_argument Unknown name, unknown or conflicting
	///         parameters.
	/// @throws std::out_of_range Parameter value out of range.
	/// @throws std::logic_error Creator returned nullptr.
	std::unique_ptr<SenderEstimator> CreateSender(
		std::string_view name, const Parameters& parameters = {}) const;

	/// @brief Creates a receiver-side estimator.
	/// @param name Registered algorithm name.
	/// @param parameters Algorithm parameters; missing keys use defaults.
	/// @return The new estimator, never nullptr.
	/// @throws std::invalid_argument Unknown name, unknown or conflicting
	///         parameters.
	/// @throws std::out_of_range Parameter value out of range.
	/// @throws std::logic_error Creator returned nullptr.
	std::unique_ptr<ReceiverEstimator> CreateReceiver(
		std::string_view name, const Parameters& parameters = {}) const;

	/// @brief Checks whether a sender-side algorithm is registered.
	bool HasSenderAlgorithm(std::string_view name) const;

	/// @brief Checks whether a receiver-side algorithm is registered.
	bool HasReceiverAlgorithm(std::string_view name) const;

	/// @brief Returns all sender-side algorithm names, sorted.
	std::vector<std::string> SenderAlgorithms() const;

	/// @brief Returns all receiver-side algorithm names, sorted.
	std::vector<std::string> ReceiverAlgorithms() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace bwe
