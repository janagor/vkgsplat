#ifndef VKGSPLAT_BACKEND_VULKAN_COMPUTE_OP_TENSOR_SYNC_DEVICE_HPP
#define VKGSPLAT_BACKEND_VULKAN_COMPUTE_OP_TENSOR_SYNC_DEVICE_HPP

#include "compute/operation.hpp"
#include "compute/tensor.hpp"

#include <vkgsplat_utility/concepts.hpp>

#include <functional>
#include <print>
#include <vector>

namespace vkgsplat::compute {

class OpTensorSyncDevice : public Operation
{
public:
  template<TriviallyCopyable T> void add(Tensor<T> &tensor)
  {
    syncs_.emplace_back([&tensor](Init &init) -> bool { return tensor.sync_to_device(init); });
  }

  void pre_eval(Init &init, RenderData const &data, VkCommandBuffer cmd) override;
  void record([[maybe_unused]] Init const &init,
    [[maybe_unused]] RenderData const &data,
    [[maybe_unused]] VkCommandBuffer cmd) override
  {}

private:
  std::vector<std::function<bool(Init &)>> syncs_;
};

}// namespace vkgsplat::compute

#endif// VKGSPLAT_BACKEND_VULKAN_COMPUTE_OP_TENSOR_SYNC_DEVICE_HPP
