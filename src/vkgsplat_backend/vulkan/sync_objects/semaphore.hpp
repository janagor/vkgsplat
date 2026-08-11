#ifndef VKGSPLAT_BACKEND_VULKAN_SYNC_OBJECTS_SEMAPHORE_HPP
#define VKGSPLAT_BACKEND_VULKAN_SYNC_OBJECTS_SEMAPHORE_HPP

#include <expected>
#include <functional>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

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

  [[nodiscard]] static auto create_timeline(std::reference_wrapper<vkb::DispatchTable> disp, u64 initial_value = 0)
    -> std::expected<Semaphore, Error>;

  // Host wait until this timeline semaphore reaches at least `value` (no-op when value == 0).
  [[nodiscard]] auto wait_value(u64 value) const -> std::expected<void, Error>;

  [[nodiscard]] auto handle() const noexcept -> VkSemaphore { return semaphore_; }

private:
  Semaphore(VkSemaphore semaphore, std::reference_wrapper<vkb::DispatchTable> disp) noexcept;
  void cleanup() noexcept;

  VkSemaphore semaphore_{ VK_NULL_HANDLE };
  std::reference_wrapper<vkb::DispatchTable> disp_;
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_SYNC_OBJECTS_SEMAPHORE_HPP
