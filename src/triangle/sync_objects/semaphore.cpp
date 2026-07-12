#include "sync_objects/semaphore.hpp"

#include <expected>
#include <functional>
#include <system_error>
#include <utility>

#include "error.hpp"
#include "backend/vulkan/initializers.hpp"

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

}// namespace vkgsplat
