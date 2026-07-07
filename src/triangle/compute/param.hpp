#pragma once

#include "backend/vulkan/gpu_allocator.hpp"
#include "compute/tensor.hpp"
#include "descriptor/descriptor_heap.hpp"
#include "types.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace vkgsplat {

namespace compute {

  class Param
  {
  public:
    virtual ~Param() = default;

    [[nodiscard]] virtual auto heap_slot() const -> HeapSlot = 0;
    [[nodiscard]] virtual auto buffer() const -> vulkan::Buffer const & = 0;
    [[nodiscard]] virtual auto byte_size() const -> VkDeviceSize = 0;
  };

  template<typename T>
    requires std::is_trivially_copyable_v<T>
  class TensorParam : public Param
  {
  public:
    TensorParam(Tensor<T> &tensor, HeapSlot slot) : tensor_(tensor), slot_(slot) {}

    [[nodiscard]] auto heap_slot() const -> HeapSlot override { return slot_; }
    [[nodiscard]] auto buffer() const -> vulkan::Buffer const & override { return tensor_.buffer(); }
    [[nodiscard]] auto byte_size() const -> VkDeviceSize override { return tensor_.byte_size(); }

  private:
    Tensor<T> &tensor_;
    HeapSlot slot_;
  };

  class BufferParam : public Param
  {
  public:
    BufferParam(vulkan::Buffer const &buffer, VkDeviceSize byte_size, HeapSlot slot)
      : buffer_(buffer), byte_size_(byte_size), slot_(slot)
    {}

    [[nodiscard]] auto heap_slot() const -> HeapSlot override { return slot_; }
    [[nodiscard]] auto buffer() const -> vulkan::Buffer const & override { return buffer_; }
    [[nodiscard]] auto byte_size() const -> VkDeviceSize override { return byte_size_; }

  private:
    vulkan::Buffer const &buffer_;
    VkDeviceSize byte_size_;
    HeapSlot slot_;
  };

  class ParamList
  {
  public:
    ParamList() = default;
    ParamList(ParamList const &) = delete;
    ParamList &operator=(ParamList const &) = delete;
    ParamList(ParamList &&) noexcept = default;
    ParamList &operator=(ParamList &&) noexcept = default;
    template<typename T>
      requires std::is_trivially_copyable_v<T>
    auto add(Tensor<T> &tensor, HeapSlot slot) -> ParamList &
    {
      params_.push_back(std::make_unique<TensorParam<T>>(tensor, slot));
      return *this;
    }

    auto add(vulkan::Buffer const &buffer, HeapSlot slot) -> ParamList & { return add(buffer, 0, slot); }

    auto add(vulkan::Buffer const &buffer, VkDeviceSize byte_size, HeapSlot slot) -> ParamList &
    {
      params_.push_back(std::make_unique<BufferParam>(buffer, byte_size, slot));
      return *this;
    }

    [[nodiscard]] auto descriptor_mappings(RenderData const &data) const -> std::vector<DescriptorMapping>;

    [[nodiscard]] auto size() const -> size_t { return params_.size(); }

  private:
    std::vector<std::unique_ptr<Param>> params_;
  };

  [[nodiscard]] inline auto params() -> ParamList { return {}; }

}// namespace compute

}// namespace vkgsplat
