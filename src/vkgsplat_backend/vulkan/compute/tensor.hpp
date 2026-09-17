#ifndef VKGSPLAT_BACKEND_VULKAN_COMPUTE_TENSOR_HPP
#define VKGSPLAT_BACKEND_VULKAN_COMPUTE_TENSOR_HPP

#include "vulkan/gpu_allocator.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/error.hpp>

#include <cstddef>
#include <cstring>
#include <expected>
#include <initializer_list>
#include <print>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace vkgsplat::compute {

template<TriviallyCopyable T> class Tensor
{
public:
  Tensor() = default;
  ~Tensor() = default;

  Tensor(Tensor const &) = delete;
  auto operator=(Tensor const &) -> Tensor & = delete;
  Tensor(Tensor &&other) noexcept
    : host_data_(std::move(other.host_data_)), buffer_(std::exchange(other.buffer_, {}))
  {}
  auto operator=(Tensor &&other) noexcept -> Tensor &
  {
    if (this != &other) {
      host_data_ = std::move(other.host_data_);
      buffer_ = std::exchange(other.buffer_, {});
    }
    return *this;
  }

  [[nodiscard]] static auto create(vulkan::Context &context, std::vector<T> data) -> std::expected<Tensor, Error>;
  [[nodiscard]] static auto create(vulkan::Context &context, size_t count, T fill = {}) -> std::expected<Tensor, Error>;

  void destroy(vulkan::Context &context) noexcept;

  [[nodiscard]] auto buffer() const noexcept -> vulkan::Buffer const & { return buffer_; }
  [[nodiscard]] auto host_data() const noexcept -> std::vector<T> const & { return host_data_; }
  [[nodiscard]] auto host_data() noexcept -> std::vector<T> & { return host_data_; }
  [[nodiscard]] auto size() const noexcept -> size_t { return host_data_.size(); }
  [[nodiscard]] auto byte_size() const noexcept -> VkDeviceSize
  { return static_cast<VkDeviceSize>(host_data_.size() * sizeof(T)); }

  [[nodiscard]] auto sync_to_device(vulkan::Context &context) const noexcept -> bool;
  [[nodiscard]] auto sync_from_device(vulkan::Context &context) noexcept -> bool;

private:
  std::vector<T> host_data_;
  vulkan::Buffer buffer_;
};

template<TriviallyCopyable T>
[[nodiscard]] auto MakeTensor(vulkan::Context &context, std::initializer_list<T> values) -> std::expected<Tensor<T>, Error>
{ return Tensor<T>::create(context, std::vector<T>{ values }); }

template<TriviallyCopyable T>
[[nodiscard]] auto MakeTensor(vulkan::Context &context, size_t count, T fill = {}) -> std::expected<Tensor<T>, Error>
{ return Tensor<T>::create(context, count, fill); }

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

#endif// VKGSPLAT_BACKEND_VULKAN_COMPUTE_TENSOR_HPP
