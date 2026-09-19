#include "algorithms/hybrid_estimator.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace bwe {
namespace internal {

HybridParameters HybridCore::ParseParameters(const Parameters& parameters) {
  Parameters aimd;
  Parameters delay;
  Parameters link_capacity;
  Parameters send_buffer;
  Parameters tsbpd_reserve;

  for (const auto& [key, value] : parameters) {
    const std::size_t dot = key.find('.');
    const std::string_view prefix =
        dot == std::string::npos ? std::string_view()
                                 : std::string_view(key).substr(0, dot);
    const std::string name =
        dot == std::string::npos ? std::string() : key.substr(dot + 1);

    Parameters* target = nullptr;
    if (prefix == AimdCore::kName) {
      target = &aimd;
    } else if (prefix == DelayCore::kName) {
      target = &delay;
    } else if (prefix == LinkCapacityCore::kName) {
      target = &link_capacity;
    } else if (prefix == SendBufferCore::kName) {
      target = &send_buffer;
    } else if (prefix == TsbpdReserveCore::kName) {
      target = &tsbpd_reserve;
    }
    if (target == nullptr || name.empty()) {
      throw std::invalid_argument("bwe: unknown hybrid parameter '" + key +
                                  "'");
    }
    target->emplace(name, value);
  }

  HybridParameters result;
  result.aimd = AimdCore::ParseParameters(aimd);
  result.delay = DelayCore::ParseParameters(delay);
  result.link_capacity = LinkCapacityCore::ParseParameters(link_capacity);
  result.send_buffer = SendBufferCore::ParseParameters(send_buffer);
  result.tsbpd_reserve = TsbpdReserveCore::ParseParameters(tsbpd_reserve);
  return result;
}

HybridCore::HybridCore(const HybridParameters& parameters)
    : aimd_(parameters.aimd),
      delay_(parameters.delay),
      link_capacity_(parameters.link_capacity),
      send_buffer_(parameters.send_buffer),
      tsbpd_reserve_(parameters.tsbpd_reserve) {}

void HybridCore::Update(const IntervalSample& sample) noexcept {
  aimd_.Update(sample);
  delay_.Update(sample);
  link_capacity_.Update(sample);
  send_buffer_.Update(sample);
  tsbpd_reserve_.Update(sample);
}

BandwidthEstimate HybridCore::GetEstimate() const noexcept {
  const std::array<BandwidthEstimate, 5> estimates = {
      aimd_.GetEstimate(), delay_.GetEstimate(), link_capacity_.GetEstimate(),
      send_buffer_.GetEstimate(), tsbpd_reserve_.GetEstimate()};

  BandwidthEstimate result;
  for (const BandwidthEstimate& estimate : estimates) {
    if (estimate.valid &&
        (!result.valid || estimate.bits_per_second < result.bits_per_second)) {
      result = estimate;
    }
  }
  return result;
}

void HybridCore::Reset() noexcept {
  aimd_.Reset();
  delay_.Reset();
  link_capacity_.Reset();
  send_buffer_.Reset();
  tsbpd_reserve_.Reset();
}

}  // namespace internal
}  // namespace bwe
