#pragma once

#include <expected>
#include <functional>

#include <vkgsplat/error.hpp>

#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

class Fence
{
public:
  Fence() = delete;
  ~Fence() noexcept;

  Fence(Fence const &) = delete;
  auto operator=(Fence const &) -> Fence & = delete;

  Fence(Fence &&other) noexcept;
  auto operator=(Fence &&other) noexcept -> Fence &;

  [[nodiscard]] static auto create(std::reference_wrapper<vkb::DispatchTable> disp, VkFenceCreateFlags flags = 0)
    -> std::expected<Fence, Error>;

  [[nodiscard]] auto handle() const noexcept -> VkFence { return fence_; }

private:
  Fence(VkFence fence, std::reference_wrapper<vkb::DispatchTable> disp) noexcept;
  void cleanup() noexcept;

  VkFence fence_{ VK_NULL_HANDLE };
  std::reference_wrapper<vkb::DispatchTable> disp_;
};

}// namespace vkgsplat
