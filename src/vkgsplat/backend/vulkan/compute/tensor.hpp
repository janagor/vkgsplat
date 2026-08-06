#ifndef VKGSPLAT_BACKEND_VULKAN_COMPUTE_TENSOR_HPP
#define VKGSPLAT_BACKEND_VULKAN_COMPUTE_TENSOR_HPP

#include "backend/vulkan/gpu_allocator.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/error.hpp>

#include <cstddef>
#include <expected>
#include <initializer_list>
#include <span>
#include <vector>

namespace vkgsplat::compute {

template<TriviallyCopyable T> class Tensor
{
public:
  Tensor() = default;
  ~Tensor() = default;

  Tensor(Tensor const &) = delete;
  auto operator=(Tensor const &) -> Tensor & = delete;
  Tensor(Tensor &&) noexcept = default;
  auto operator=(Tensor &&) noexcept -> Tensor & = default;

  [[nodiscard]] static auto create(Init &init, std::vector<T> data) -> std::expected<Tensor, Error>;
  [[nodiscard]] static auto create(Init &init, size_t count, T fill = {}) -> std::expected<Tensor, Error>;

  void destroy(Init &init) noexcept;

  [[nodiscard]] auto buffer() const noexcept -> vulkan::Buffer const & { return buffer_; }
  [[nodiscard]] auto host_data() const noexcept -> std::vector<T> const & { return host_data_; }
  [[nodiscard]] auto host_data() noexcept -> std::vector<T> & { return host_data_; }
  [[nodiscard]] auto size() const noexcept -> size_t { return host_data_.size(); }
  [[nodiscard]] auto byte_size() const noexcept -> VkDeviceSize
  { return static_cast<VkDeviceSize>(host_data_.size() * sizeof(T)); }

  [[nodiscard]] auto sync_to_device(Init &init) const noexcept -> bool;
  [[nodiscard]] auto sync_from_device(Init &init) noexcept -> bool;

private:
  std::vector<T> host_data_;
  vulkan::Buffer buffer_{};
};

template<TriviallyCopyable T>
[[nodiscard]] auto MakeTensor(Init &init, std::initializer_list<T> values) -> std::expected<Tensor<T>, Error>
{ return Tensor<T>::create(init, std::vector<T>{ values }); }

template<TriviallyCopyable T>
[[nodiscard]] auto MakeTensor(Init &init, size_t count, T fill = {}) -> std::expected<Tensor<T>, Error>
{ return Tensor<T>::create(init, count, fill); }

}// namespace vkgsplat::compute

#include "compute/tensor.ipp"

#endif// VKGSPLAT_BACKEND_VULKAN_COMPUTE_TENSOR_HPP
