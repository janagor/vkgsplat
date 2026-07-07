#pragma once

#include "backend/vulkan/gpu_allocator.hpp"
#include "vulkan_context.hpp"

#include <cstddef>
#include <expected>
#include <initializer_list>
#include <span>
#include <type_traits>
#include <vector>

namespace vkgsplat::compute {

template<typename T>
  requires std::is_trivially_copyable_v<T>
class Tensor
{
public:
  Tensor() = default;
  ~Tensor() = default;

  Tensor(Tensor const &) = delete;
  Tensor &operator=(Tensor const &) = delete;
  Tensor(Tensor &&) noexcept = default;
  Tensor &operator=(Tensor &&) noexcept = default;

  [[nodiscard]] static auto create(Init &init, std::vector<T> data) -> std::expected<Tensor, void *>;
  [[nodiscard]] static auto create(Init &init, size_t count, T fill = {}) -> std::expected<Tensor, void *>;

  void destroy(Init &init) noexcept;

  [[nodiscard]] auto buffer() const noexcept -> vulkan::Buffer const & { return buffer_; }
  [[nodiscard]] auto host_data() const noexcept -> std::vector<T> const & { return host_data_; }
  [[nodiscard]] auto host_data() noexcept -> std::vector<T> & { return host_data_; }
  [[nodiscard]] auto size() const noexcept -> size_t { return host_data_.size(); }
  [[nodiscard]] auto byte_size() const noexcept -> VkDeviceSize
  { return static_cast<VkDeviceSize>(host_data_.size() * sizeof(T)); }

  [[nodiscard]] auto sync_to_device(Init &init) const noexcept -> bool;

private:
  std::vector<T> host_data_;
  vulkan::Buffer buffer_{};
};

template<typename T>
  requires std::is_trivially_copyable_v<T>
[[nodiscard]] auto tensor(Init &init, std::initializer_list<T> values) -> std::expected<Tensor<T>, void *>
{ return Tensor<T>::create(init, std::vector<T>{ values }); }

template<typename T>
  requires std::is_trivially_copyable_v<T>
[[nodiscard]] auto tensor(Init &init, size_t count, T fill = {}) -> std::expected<Tensor<T>, void *>
{ return Tensor<T>::create(init, count, fill); }

}// namespace vkgsplat::compute

#include "compute/tensor.ipp"
