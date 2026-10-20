#include "video_device.hpp"

#include <algorithm>
#include <numeric>

namespace vkgsplat::vulkan::video {

auto ChooseEncodeQueue(Report const &report) noexcept -> std::optional<std::uint32_t>
{
  if (!report.usable()) { return std::nullopt; }
  return report.encode_queue_families.front();
}

auto RankCandidates(std::span<CandidateInfo const> candidates) -> std::vector<std::size_t>
{
  std::vector<std::size_t> order(candidates.size());
  std::iota(order.begin(), order.end(), 0U);
  std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) -> bool {
    auto const &a = candidates[left];
    auto const &b = candidates[right];
    if (a.preferred_rendering_device != b.preferred_rendering_device) { return a.preferred_rendering_device; }
    bool const a_discrete = a.device_type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    bool const b_discrete = b.device_type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    if (a_discrete != b_discrete) { return a_discrete; }
    return a.name < b.name;
  });
  return order;
}

}// namespace vkgsplat::vulkan::video
