#include "vulkan/sync_objects/semaphore.hpp"

#include <array>
#include <expected>
#include <functional>
#include <limits>
#include <system_error>
#include <utility>

#include "vulkan/initializers.hpp"
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

Semaphore::Semaphore(VkSemaphore semaphore, std::reference_wrapper<vkb::DispatchTable> disp) noexcept
  : semaphore_(semaphore), disp_(disp)
{}

Semaphore::Semaphore(Semaphore &&other) noexcept
  : semaphore_(std::exchange(other.semaphore_, VK_NULL_HANDLE)), disp_(other.disp_)
{}

auto Semaphore::operator=(Semaphore &&other) noexcept -> Semaphore &
{
  if (this != &other) {
    cleanup();
    semaphore_ = std::exchange(other.semaphore_, VK_NULL_HANDLE);
    disp_ = other.disp_;
  }
  return *this;
}

Semaphore::~Semaphore() noexcept { cleanup(); }

void Semaphore::cleanup() noexcept
{
  if (semaphore_ != VK_NULL_HANDLE) { disp_.get().destroySemaphore(semaphore_, nullptr); }
  semaphore_ = VK_NULL_HANDLE;
}

auto Semaphore::create(std::reference_wrapper<vkb::DispatchTable> disp) -> std::expected<Semaphore, Error>
{
  auto const info = initializers::SemaphoreCreateInfo();
  VkSemaphore semaphore = VK_NULL_HANDLE;
  if (disp.get().createSemaphore(&info, nullptr, &semaphore) != VK_SUCCESS) {
    return std::unexpected{ Error{ std::make_error_code(std::errc::io_error), "Failed to create Semaphore" } };
  }
  return Semaphore(semaphore, disp);
}

auto Semaphore::create_timeline(std::reference_wrapper<vkb::DispatchTable> disp, u64 initial_value)
  -> std::expected<Semaphore, Error>
{
  auto type_info = initializers::SemaphoreTypeCreateInfo(VK_SEMAPHORE_TYPE_TIMELINE, initial_value);
  auto const info = initializers::SemaphoreCreateInfo(&type_info);
  VkSemaphore semaphore = VK_NULL_HANDLE;
  if (disp.get().createSemaphore(&info, nullptr, &semaphore) != VK_SUCCESS) {
    return std::unexpected{
      Error{ std::make_error_code(std::errc::io_error), "Failed to create timeline Semaphore" }
    };
  }
  return Semaphore(semaphore, disp);
}

auto Semaphore::wait_value(u64 value) const -> std::expected<void, Error>
{
  if (value == 0U || semaphore_ == VK_NULL_HANDLE) { return {}; }

  std::array<VkSemaphore, 1> const semaphores{ semaphore_ };
  std::array<u64, 1> const values{ value };
  auto const wait_info = initializers::SemaphoreWaitInfo(semaphores, values);
  if (disp_.get().waitSemaphores(&wait_info, std::numeric_limits<u64>::max()) != VK_SUCCESS) {
    return std::unexpected{ Error{ std::make_error_code(std::errc::io_error), "Failed to wait on timeline Semaphore" } };
  }
  return {};
}

}// namespace vkgsplat
