#include "bwe/estimator_factory.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace bwe
{
namespace
{

class FakeSenderEstimator : public SenderEstimator
{
public:
	explicit FakeSenderEstimator(double bits_per_second)
	{
		estimate_.bits_per_second = bits_per_second;
		estimate_.valid = true;
	}

	std::string_view Name() const noexcept override
	{
		return "fake";
	}

private:
	void DoUpdate(const SenderMeasurement&) noexcept override
	{
	}
	BandwidthEstimate DoGetEstimate() const noexcept override
	{
		return estimate_;
	}
	void DoReset() noexcept override
	{
	}

	BandwidthEstimate estimate_;
};

class FakeReceiverEstimator : public ReceiverEstimator
{
public:
	std::string_view Name() const noexcept override
	{
		return "fake";
	}

private:
	void DoUpdate(const ReceiverMeasurement&) noexcept override
	{
	}
	BandwidthEstimate DoGetEstimate() const noexcept override
	{
		return {};
	}
	void DoReset() noexcept override
	{
	}
};

// Accepts only the key "rate" with a value >= 0, like a real creator.
std::unique_ptr<SenderEstimator> CreateFakeSender(
	const Parameters& parameters)
{
	double rate = 1000.0;
	for (const auto& [key, value] : parameters)
	{
		if (key != "rate")
		{
			throw std::invalid_argument("unknown parameter '" + key + "'");
		}
		if (value < 0.0)
		{
			throw std::out_of_range("rate must be >= 0");
		}
		rate = value;
	}
	return std::make_unique<FakeSenderEstimator>(rate);
}

std::unique_ptr<ReceiverEstimator> CreateFakeReceiver(const Parameters&)
{
	return std::make_unique<FakeReceiverEstimator>();
}

TEST(EstimatorFactoryTest, NewFactoryIsEmpty)
{
	EstimatorFactory factory;
	EXPECT_TRUE(factory.SenderAlgorithms().empty());
	EXPECT_TRUE(factory.ReceiverAlgorithms().empty());
}

TEST(EstimatorFactoryTest, RegisterSenderMakesAlgorithmAvailable)
{
	EstimatorFactory factory;
	factory.RegisterSender("fake", CreateFakeSender);
	EXPECT_TRUE(factory.HasSenderAlgorithm("fake"));
	EXPECT_FALSE(factory.HasReceiverAlgorithm("fake"));
}

TEST(EstimatorFactoryTest, RegisterReceiverMakesAlgorithmAvailable)
{
	EstimatorFactory factory;
	factory.RegisterReceiver("fake", CreateFakeReceiver);
	EXPECT_TRUE(factory.HasReceiverAlgorithm("fake"));
	EXPECT_FALSE(factory.HasSenderAlgorithm("fake"));
}

TEST(EstimatorFactoryTest, SameNameMayBeUsedOnBothSides)
{
	EstimatorFactory factory;
	factory.RegisterSender("fake", CreateFakeSender);
	EXPECT_NO_THROW(factory.RegisterReceiver("fake", CreateFakeReceiver));
}

TEST(EstimatorFactoryTest, RegisterRejectsEmptyName)
{
	EstimatorFactory factory;
	EXPECT_THROW(factory.RegisterSender("", CreateFakeSender),
				 std::invalid_argument);
	EXPECT_THROW(factory.RegisterReceiver("", CreateFakeReceiver),
				 std::invalid_argument);
}

TEST(EstimatorFactoryTest, RegisterRejectsEmptyCreator)
{
	EstimatorFactory factory;
	EXPECT_THROW(factory.RegisterSender("fake", SenderCreator()),
				 std::invalid_argument);
	EXPECT_THROW(factory.RegisterReceiver("fake", ReceiverCreator()),
				 std::invalid_argument);
	EXPECT_FALSE(factory.HasSenderAlgorithm("fake"));
	EXPECT_FALSE(factory.HasReceiverAlgorithm("fake"));
}

TEST(EstimatorFactoryTest, RegisterRejectsDuplicateNameAndKeepsOriginal)
{
	EstimatorFactory factory;
	factory.RegisterSender("fake", CreateFakeSender);
	EXPECT_THROW(factory.RegisterSender("fake", CreateFakeSender),
				 std::invalid_argument);

	const auto estimator = factory.CreateSender("fake", {{"rate", 42.0}});
	EXPECT_DOUBLE_EQ(estimator->GetEstimate().bits_per_second, 42.0);
}

TEST(EstimatorFactoryTest, CreateSenderPassesParameters)
{
	EstimatorFactory factory;
	factory.RegisterSender("fake", CreateFakeSender);

	const auto estimator = factory.CreateSender("fake", {{"rate", 5e6}});
	ASSERT_NE(estimator, nullptr);
	EXPECT_DOUBLE_EQ(estimator->GetEstimate().bits_per_second, 5e6);
}

TEST(EstimatorFactoryTest, CreateSenderUsesDefaultsWithoutParameters)
{
	EstimatorFactory factory;
	factory.RegisterSender("fake", CreateFakeSender);

	const auto estimator = factory.CreateSender("fake");
	ASSERT_NE(estimator, nullptr);
	EXPECT_DOUBLE_EQ(estimator->GetEstimate().bits_per_second, 1000.0);
}

TEST(EstimatorFactoryTest, CreateReceiverReturnsEstimator)
{
	EstimatorFactory factory;
	factory.RegisterReceiver("fake", CreateFakeReceiver);
	EXPECT_NE(factory.CreateReceiver("fake"), nullptr);
}

TEST(EstimatorFactoryTest, CreateThrowsInvalidArgumentOnUnknownName)
{
	EstimatorFactory factory;
	factory.RegisterSender("fake", CreateFakeSender);
	EXPECT_THROW(factory.CreateSender("unknown"), std::invalid_argument);
	EXPECT_THROW(factory.CreateReceiver("fake"), std::invalid_argument);
}

TEST(EstimatorFactoryTest, CreatePropagatesInvalidArgumentOnUnknownKey)
{
	EstimatorFactory factory;
	factory.RegisterSender("fake", CreateFakeSender);
	EXPECT_THROW(factory.CreateSender("fake", {{"rat", 1.0}}),
				 std::invalid_argument);
}

TEST(EstimatorFactoryTest, CreatePropagatesOutOfRange)
{
	EstimatorFactory factory;
	factory.RegisterSender("fake", CreateFakeSender);
	EXPECT_THROW(factory.CreateSender("fake", {{"rate", -1.0}}),
				 std::out_of_range);
}

TEST(EstimatorFactoryTest, CreateThrowsLogicErrorIfCreatorReturnsNull)
{
	EstimatorFactory factory;
	factory.RegisterSender("null", [](const Parameters&)
	{
		return std::unique_ptr<SenderEstimator>();
	});
	factory.RegisterReceiver("null", [](const Parameters&)
	{
		return std::unique_ptr<ReceiverEstimator>();
	});
	EXPECT_THROW(factory.CreateSender("null"), std::logic_error);
	EXPECT_THROW(factory.CreateReceiver("null"), std::logic_error);
}

TEST(EstimatorFactoryTest, AlgorithmNamesAreSorted)
{
	EstimatorFactory factory;
	factory.RegisterSender("c", CreateFakeSender);
	factory.RegisterSender("a", CreateFakeSender);
	factory.RegisterSender("b", CreateFakeSender);
	EXPECT_EQ(factory.SenderAlgorithms(),
			  (std::vector<std::string>{"a", "b", "c"}));
}

TEST(EstimatorFactoryTest, InstanceReturnsSameFactory)
{
	EXPECT_EQ(&EstimatorFactory::Instance(), &EstimatorFactory::Instance());
}

}  // namespace
}  // namespace bwe
