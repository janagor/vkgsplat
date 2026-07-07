#pragma once

#include "compute/tensor.hpp"//NOLINT(misc-header-include-cycle)

#include <cstring>
#include <print>
#include <utility>

namespace vkgsplat::compute {

template<typename T>
  requires std::is_trivially_copyable_v<T>
auto Tensor<T>::create(Init &init, std::vector<T> data) -> std::expected<Tensor, void *>
{
  Tensor result;
  result.host_data_ = std::move(data);

  auto gpu_buffer = init.gpu_allocator.create_storage_buffer(result.byte_size());
  if (!gpu_buffer) {
    std::println("Failed to create tensor GPU buffer!");
    return std::unexpected(nullptr);
  }

  result.buffer_ = *gpu_buffer;

  if (!result.sync_to_device(init)) {
    init.gpu_allocator.destroy_buffer(result.buffer_);
    return std::unexpected(nullptr);
  }

  return result;
}

template<typename T>
  requires std::is_trivially_copyable_v<T>
auto Tensor<T>::create(Init &init, size_t count, T fill) -> std::expected<Tensor, void *>
{ return create(init, std::vector<T>(count, fill)); }

template<typename T>
  requires std::is_trivially_copyable_v<T>
void Tensor<T>::destroy(Init &init) noexcept
{
  init.gpu_allocator.destroy_buffer(buffer_);
  buffer_ = {};
  host_data_.clear();
}

template<typename T>
  requires std::is_trivially_copyable_v<T>
auto Tensor<T>::sync_to_device(Init &init) const noexcept -> bool
{
  if (buffer_.handle == VK_NULL_HANDLE) { return false; }
  return static_cast<bool>(init.gpu_allocator.write_buffer(buffer_, std::span<const T>{ host_data_ }));
}

}// namespace vkgsplat::compute
