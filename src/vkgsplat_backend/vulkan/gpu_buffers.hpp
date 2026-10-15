#ifndef VKGSPLAT_BACKEND_VULKAN_GPU_BUFFERS_HPP
#define VKGSPLAT_BACKEND_VULKAN_GPU_BUFFERS_HPP

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/error.hpp>

#include <vkexec/error.hpp>
#include <vkexec/sync_wait.hpp>
#include <vkexec_vma/gpu_buffer.hpp>
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

using GpuBuffer = vkexec::vma::gpu_buffer;
using DescriptorHeapBuffer = vkexec::descriptor_heap_buffer<GpuBuffer>;

namespace gpu_buffers_detail {

  [[nodiscard]] inline auto MakeGpuBufferError(std::string message) -> Error
  { return MakeError(std::errc::io_error, std::move(message)); }

  [[nodiscard]] inline auto MakeGpuBufferError(vkexec::error const &err) -> Error
  { return MakeGpuBufferError(err.message()); }

  [[nodiscard]] inline auto CreateGpuBuffer(vkexec::vma::allocator &allocator,
    VkDeviceSize size,
    vkexec::vma::gpu_buffer_memory memory,
    bool shader_device_address) -> std::expected<GpuBuffer, Error>
  {
    auto created = vkexec::try_sync_wait_value(vkexec::vma::factory::make_gpu_buffer(allocator,
      vkexec::vma::gpu_buffer_create_info{
        .size = size,
        .memory = memory,
        .shader_device_address = shader_device_address,
      }));
    if (!created) { return std::unexpected(MakeGpuBufferError(created.error())); }
    return std::move(*created);
  }

}// namespace gpu_buffers_detail

/// Host-visible storage with buffer device address.
[[nodiscard]] inline auto CreateStorageBuffer(vkexec::vma::allocator &allocator, VkDeviceSize size)
  -> std::expected<GpuBuffer, Error>
{ return gpu_buffers_detail::CreateGpuBuffer(allocator, size, vkexec::vma::gpu_buffer_memory::host_visible, true); }

/// Device-local storage (transfer + indirect capable) with buffer device address.
[[nodiscard]] inline auto CreateDeviceStorageBuffer(vkexec::vma::allocator &allocator, VkDeviceSize size)
  -> std::expected<GpuBuffer, Error>
{ return gpu_buffers_detail::CreateGpuBuffer(allocator, size, vkexec::vma::gpu_buffer_memory::device_local, true); }

/// Host-visible staging buffer for GPU -> CPU readback (no BDA).
[[nodiscard]] inline auto CreateStagingBuffer(vkexec::vma::allocator &allocator, VkDeviceSize size)
  -> std::expected<GpuBuffer, Error>
{ return gpu_buffers_detail::CreateGpuBuffer(allocator, size, vkexec::vma::gpu_buffer_memory::staging, false); }

[[nodiscard]] inline auto CreateDescriptorHeapBuffer(vkexec::vma::allocator &allocator,
  VkDeviceSize size,
  VkDeviceSize alignment) -> std::expected<DescriptorHeapBuffer, Error>
{
  auto created = vkexec::try_sync_wait_value(
    vkexec::factory::make_descriptor_heap_buffer(allocator, size, alignment));
  if (!created) { return std::unexpected(gpu_buffers_detail::MakeGpuBufferError(created.error())); }
  return std::move(*created);
}

[[nodiscard]] inline auto DeviceAddressOrZero(GpuBuffer const &buffer) noexcept -> VkDeviceAddress
{
  auto const address = buffer.device_address();
  return address ? *address : VkDeviceAddress{ 0 };
}

[[nodiscard]] inline auto DeviceAddressOrZero(DescriptorHeapBuffer const &buffer) noexcept -> VkDeviceAddress
{
  auto const address = buffer.device_address();
  return address ? *address : VkDeviceAddress{ 0 };
}

[[nodiscard]] inline auto DeviceAddressOrZero(std::optional<GpuBuffer> const &buffer) noexcept -> VkDeviceAddress
{ return buffer ? DeviceAddressOrZero(*buffer) : VkDeviceAddress{ 0 }; }

[[nodiscard]] inline auto DeviceAddressOrZero(std::optional<DescriptorHeapBuffer> const &buffer) noexcept
  -> VkDeviceAddress
{ return buffer ? DeviceAddressOrZero(*buffer) : VkDeviceAddress{ 0 }; }

template<TriviallyCopyable T>
[[nodiscard]] inline auto WriteMapped(GpuBuffer &buffer, std::span<T const> data) -> std::expected<void, Error>
{
  if (data.size_bytes() > buffer.size()) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "WriteMapped: data exceeds buffer size"));
  }
  auto mapped = buffer.mapped();
  if (mapped.size() < data.size_bytes()) {
    return std::unexpected(MakeError(std::errc::io_error, "WriteMapped: buffer is not host-mapped"));
  }
  std::memcpy(mapped.data(), data.data(), data.size_bytes());
  auto flushed = buffer.flush();
  if (!flushed) { return std::unexpected(gpu_buffers_detail::MakeGpuBufferError(flushed.error())); }
  return {};
}

template<TriviallyCopyable T>
[[nodiscard]] inline auto WriteMapped(DescriptorHeapBuffer &buffer, std::span<T const> data)
  -> std::expected<void, Error>
{
  if (data.size_bytes() > buffer.size()) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "WriteMapped: data exceeds buffer size"));
  }
  auto mapped = buffer.mapped();
  if (mapped.size() < data.size_bytes()) {
    return std::unexpected(MakeError(std::errc::io_error, "WriteMapped: buffer is not host-mapped"));
  }
  std::memcpy(mapped.data(), data.data(), data.size_bytes());
  auto flushed = buffer.flush();
  if (!flushed) { return std::unexpected(gpu_buffers_detail::MakeGpuBufferError(flushed.error())); }
  return {};
}

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_GPU_BUFFERS_HPP
