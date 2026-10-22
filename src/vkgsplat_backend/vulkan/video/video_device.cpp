#include "video_device.hpp"

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "video_capabilities.hpp"

namespace vkgsplat::vulkan::video {

auto ChooseEncodeQueue(Report const &report) noexcept -> std::optional<std::uint32_t>
{
  if (!report.usable()) { return std::nullopt; }
  return report.encode_queue_families.front();
}

auto RankCandidates(std::span<CandidateInfo const> candidates) -> std::vector<std::size_t>
{
  std::vector<std::size_t> order(candidates.size());
  std::size_t next_index = 0;
  std::ranges::generate(order, [&next_index]() -> std::size_t { return next_index++; });
  std::ranges::stable_sort(order, [&](std::size_t left, std::size_t right) -> bool {
    auto const &left_candidate = candidates.subspan(left).front();
    auto const &right_candidate = candidates.subspan(right).front();
    if (left_candidate.preferred_rendering_device != right_candidate.preferred_rendering_device) {
      return left_candidate.preferred_rendering_device;
    }
    bool const left_discrete = left_candidate.device_type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    bool const right_discrete = right_candidate.device_type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    if (left_discrete != right_discrete) { return left_discrete; }
    return left_candidate.name < right_candidate.name;
  });
  return order;
}

}// namespace vkgsplat::vulkan::video
