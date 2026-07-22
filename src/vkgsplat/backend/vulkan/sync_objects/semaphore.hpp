#pragma once

#include <expected>
#include <functional>

#include <vkgsplat_utility/error.hpp>

#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

class Semaphore
{
public:
  Semaphore() = delete;
  ~Semaphore() noexcept;

  Semaphore(Semaphore const &) = delete;
  auto operator=(Semaphore const &) -> Semaphore & = delete;

  Semaphore(Semaphore &&other) noexcept;
  auto operator=(Semaphore &&other) noexcept -> Semaphore &;

  [[nodiscard]] static auto create(std::reference_wrapper<vkb::DispatchTable> disp) -> std::expected<Semaphore, Error>;

  [[nodiscard]] auto handle() const noexcept -> VkSemaphore { return semaphore_; }

private:
  Semaphore(VkSemaphore semaphore, std::reference_wrapper<vkb::DispatchTable> disp) noexcept;
  void cleanup() noexcept;

  VkSemaphore semaphore_{ VK_NULL_HANDLE };
  std::reference_wrapper<vkb::DispatchTable> disp_;
};

}// namespace vkgsplat
