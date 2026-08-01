#pragma once

#include "compute/tensor.hpp"// NOLINT(misc-header-include-cycle)

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/error.hpp>

#include <cstring>
#include <print>
#include <system_error>
#include <utility>

namespace vkgsplat::compute {

template<TriviallyCopyable T>
auto Tensor<T>::create(Init &init, std::vector<T> data) -> std::expected<Tensor, Error>
{
  Tensor result;
  result.host_data_ = std::move(data);

  auto gpu_buffer = init.gpu_allocator.create_storage_buffer(result.byte_size());
  if (!gpu_buffer) {
    std::println("Failed to create tensor GPU buffer!");
    return std::unexpected(gpu_buffer.error());
  }

  result.buffer_ = *gpu_buffer;

  if (!result.sync_to_device(init)) {
    init.gpu_allocator.destroy_buffer(result.buffer_);
    return std::unexpected(make_error(std::errc::io_error, "Failed to sync tensor to device"));
  }

  return result;
}

template<TriviallyCopyable T>
auto Tensor<T>::create(Init &init, size_t count, T fill) -> std::expected<Tensor, Error>
{
  return create(init, std::vector<T>(count, fill));
}

template<TriviallyCopyable T>
void Tensor<T>::destroy(Init &init) noexcept
{
  init.gpu_allocator.destroy_buffer(buffer_);
  buffer_ = {};
  host_data_.clear();
}

template<TriviallyCopyable T>
auto Tensor<T>::sync_to_device(Init &init) const noexcept -> bool
{
  if (buffer_.handle == VK_NULL_HANDLE) { return false; }
  return static_cast<bool>(init.gpu_allocator.write_buffer(buffer_, std::span<const T>{ host_data_ }));
}

template<TriviallyCopyable T>
auto Tensor<T>::sync_from_device(Init &init) noexcept -> bool
{
  if (buffer_.handle == VK_NULL_HANDLE) { return false; }

  auto const host_values = init.gpu_allocator.read_buffer<T>(buffer_, host_data_.size());
  if (!host_values) { return false; }

  host_data_ = std::move(*host_values);
  return true;
}

}// namespace vkgsplat::compute
