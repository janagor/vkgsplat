#include "vulkan/sync_objects/fence.hpp"

#include <expected>
#include <functional>
#include <system_error>
#include <utility>

#include "vulkan/initializers.hpp"
#include <vkgsplat_utility/error.hpp>

#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

Fence::Fence(VkFence fence, std::reference_wrapper<vkb::DispatchTable> disp) noexcept : fence_(fence), disp_(disp) {}

Fence::Fence(Fence &&other) noexcept : fence_(std::exchange(other.fence_, VK_NULL_HANDLE)), disp_(other.disp_) {}

auto Fence::operator=(Fence &&other) noexcept -> Fence &
{
  if (this != &other) {
    cleanup();
    fence_ = std::exchange(other.fence_, VK_NULL_HANDLE);
    disp_ = other.disp_;
  }
  return *this;
}

Fence::~Fence() noexcept { cleanup(); }

void Fence::cleanup() noexcept
{
  if (fence_ != VK_NULL_HANDLE) { disp_.get().destroyFence(fence_, nullptr); }
  fence_ = VK_NULL_HANDLE;
}

auto Fence::create(std::reference_wrapper<vkb::DispatchTable> disp, VkFenceCreateFlags flags)
  -> std::expected<Fence, Error>
{
  auto const info = initializers::FenceCreateInfo(flags);
  VkFence fence = VK_NULL_HANDLE;
  if (disp.get().createFence(&info, nullptr, &fence) != VK_SUCCESS) {
    return std::unexpected{ Error{ std::make_error_code(std::errc::io_error), "Failed to create Fence" } };
  }
  return Fence(fence, disp);
}

}// namespace vkgsplat
