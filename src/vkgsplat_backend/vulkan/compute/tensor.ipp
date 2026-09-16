#ifndef VKGSPLAT_BACKEND_VULKAN_COMPUTE_TENSOR_IPP
#define VKGSPLAT_BACKEND_VULKAN_COMPUTE_TENSOR_IPP

#include "compute/tensor.hpp"// NOLINT(misc-header-include-cycle)

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/error.hpp>

#include <cstring>
#include <print>
#include <system_error>
#include <utility>

namespace vkgsplat::compute {

template<TriviallyCopyable T>
auto Tensor<T>::create(vulkan::Context &context, std::vector<T> data) -> std::expected<Tensor, Error>
{
  Tensor result;
  result.host_data_ = std::move(data);

  auto gpu_buffer = context.gpu_allocator.create_storage_buffer(result.byte_size());
  if (!gpu_buffer) {
    std::println("Failed to create tensor GPU buffer!");
    return std::unexpected(gpu_buffer.error());
  }

  result.buffer_ = std::move(*gpu_buffer);

  if (!result.sync_to_device(context)) {
    context.gpu_allocator.destroy_buffer(result.buffer_);
    return std::unexpected(MakeError(std::errc::io_error, "Failed to sync tensor to device"));
  }

  return result;
}

template<TriviallyCopyable T>
auto Tensor<T>::create(vulkan::Context &context, size_t count, T fill) -> std::expected<Tensor, Error>
{
  return create(context, std::vector<T>(count, fill));
}

template<TriviallyCopyable T>
void Tensor<T>::destroy(vulkan::Context &context) noexcept
{
  context.gpu_allocator.destroy_buffer(buffer_);
  buffer_ = {};
  host_data_.clear();
}

template<TriviallyCopyable T>
auto Tensor<T>::sync_to_device(vulkan::Context &context) const noexcept -> bool
{
  if (buffer_.empty()) { return false; }
  return static_cast<bool>(context.gpu_allocator.write_buffer(buffer_, std::span<const T>{ host_data_ }));
}

template<TriviallyCopyable T>
auto Tensor<T>::sync_from_device(vulkan::Context &context) noexcept -> bool
{
  if (buffer_.empty()) { return false; }

  auto const host_values = context.gpu_allocator.read_buffer<T>(buffer_, host_data_.size());
  if (!host_values) { return false; }

  host_data_ = std::move(*host_values);
  return true;
}

}// namespace vkgsplat::compute

#endif// VKGSPLAT_BACKEND_VULKAN_COMPUTE_TENSOR_IPP
