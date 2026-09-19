#ifndef VKGSPLAT_BACKEND_VULKAN_GPU_BUFFERS_HPP
#define VKGSPLAT_BACKEND_VULKAN_GPU_BUFFERS_HPP

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/error.hpp>

#include <vkexec/context.hpp>
#include <vkexec/error.hpp>
#include <vkexec/gpu_buffer.hpp>
#include <vkexec/sync_wait.hpp>
#include <vkexec_extensions/descriptor_heap/buffer.hpp>
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace vkgsplat::vulkan {
namespace gpu_buffers_detail {

  [[nodiscard]] inline auto MakeGpuBufferError(std::string message) -> Error
  { return MakeError(std::errc::io_error, std::move(message)); }

  [[nodiscard]] inline auto MakeGpuBufferError(vkexec::error const &err) -> Error
  { return MakeGpuBufferError(err.message()); }

  [[nodiscard]] inline auto CreateGpuBuffer(vkexec::context &ctx,
    VkDeviceSize size,
    vkexec::gpu_buffer_memory memory,
    bool shader_device_address) -> std::expected<vkexec::gpu_buffer, Error>
  {
    auto created = vkexec::try_sync_wait_value(vkexec::gpu_buffer::create(ctx,
      vkexec::gpu_buffer_create_info{
        .size = size,
        .memory = memory,
        .shader_device_address = shader_device_address,
      }));
    if (!created) { return std::unexpected(MakeGpuBufferError(created.error())); }
    return std::move(*created);
  }

}// namespace gpu_buffers_detail

/// Host-visible storage with buffer device address.
[[nodiscard]] inline auto CreateStorageBuffer(vkexec::context &ctx, VkDeviceSize size)
  -> std::expected<vkexec::gpu_buffer, Error>
{ return gpu_buffers_detail::CreateGpuBuffer(ctx, size, vkexec::gpu_buffer_memory::host_visible, true); }

/// Device-local storage (transfer + indirect capable) with buffer device address.
[[nodiscard]] inline auto CreateDeviceStorageBuffer(vkexec::context &ctx, VkDeviceSize size)
  -> std::expected<vkexec::gpu_buffer, Error>
{ return gpu_buffers_detail::CreateGpuBuffer(ctx, size, vkexec::gpu_buffer_memory::device_local, true); }

/// Host-visible staging buffer for GPU→CPU readback (no BDA).
[[nodiscard]] inline auto CreateStagingBuffer(vkexec::context &ctx, VkDeviceSize size)
  -> std::expected<vkexec::gpu_buffer, Error>
{ return gpu_buffers_detail::CreateGpuBuffer(ctx, size, vkexec::gpu_buffer_memory::staging, false); }

[[nodiscard]] inline auto CreateDescriptorHeapBuffer(vkexec::context &ctx, VkDeviceSize size)
  -> std::expected<vkexec::descriptor_heap_buffer, Error>
{
  auto created = vkexec::try_sync_wait_value(vkexec::descriptor_heap_buffer::create(ctx, size));
  if (!created) { return std::unexpected(gpu_buffers_detail::MakeGpuBufferError(created.error())); }
  return std::move(*created);
}

[[nodiscard]] inline auto DeviceAddressOrZero(vkexec::gpu_buffer const &buffer) noexcept -> VkDeviceAddress
{
  auto const address = buffer.device_address();
  return address ? *address : VkDeviceAddress{ 0 };
}

[[nodiscard]] inline auto DeviceAddressOrZero(vkexec::descriptor_heap_buffer const &buffer) noexcept -> VkDeviceAddress
{
  auto const address = buffer.device_address();
  return address ? *address : VkDeviceAddress{ 0 };
}

[[nodiscard]] inline auto DeviceAddressOrZero(std::optional<vkexec::gpu_buffer> const &buffer) noexcept
  -> VkDeviceAddress
{ return buffer ? DeviceAddressOrZero(*buffer) : VkDeviceAddress{ 0 }; }

[[nodiscard]] inline auto DeviceAddressOrZero(std::optional<vkexec::descriptor_heap_buffer> const &buffer) noexcept
  -> VkDeviceAddress
{ return buffer ? DeviceAddressOrZero(*buffer) : VkDeviceAddress{ 0 }; }

template<TriviallyCopyable T>
[[nodiscard]] inline auto WriteMapped(vkexec::gpu_buffer const &buffer, std::span<T const> data)
  -> std::expected<void, Error>
{
  if (data.size_bytes() > buffer.size()) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "WriteMapped: data exceeds buffer size"));
  }
  auto const mapped = buffer.mapped();
  if (mapped.size() < data.size_bytes()) {
    return std::unexpected(MakeError(std::errc::io_error, "WriteMapped: buffer is not host-mapped"));
  }
  std::memcpy(mapped.data(), data.data(), data.size_bytes());
  return {};
}

template<TriviallyCopyable T>
[[nodiscard]] inline auto WriteMapped(vkexec::descriptor_heap_buffer const &buffer, std::span<T const> data)
  -> std::expected<void, Error>
{
  if (data.size_bytes() > buffer.size()) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "WriteMapped: data exceeds buffer size"));
  }
  auto const mapped = buffer.mapped();
  if (mapped.size() < data.size_bytes()) {
    return std::unexpected(MakeError(std::errc::io_error, "WriteMapped: buffer is not host-mapped"));
  }
  std::memcpy(mapped.data(), data.data(), data.size_bytes());
  return {};
}

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_GPU_BUFFERS_HPP
