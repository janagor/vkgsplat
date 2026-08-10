#ifndef VKGSPLAT_BACKEND_VULKAN_COMPUTE_OP_TENSOR_SYNC_LOCAL_HPP
#define VKGSPLAT_BACKEND_VULKAN_COMPUTE_OP_TENSOR_SYNC_LOCAL_HPP

#include "compute/operation.hpp"
#include "compute/tensor.hpp"

#include <vkgsplat_utility/concepts.hpp>

#include <functional>
#include <print>
#include <vector>

namespace vkgsplat::compute {

class OpTensorSyncLocal : public Operation
{
public:
  template<TriviallyCopyable T> void add(Tensor<T> &tensor)
  {
    syncs_.emplace_back([&tensor](vulkan::Context &context) -> bool { return tensor.sync_from_device(context); });
  }

  void post_eval(vulkan::Context &context, RenderData const &data, VkCommandBuffer cmd) override;
  void record([[maybe_unused]] vulkan::Context const &context,
    [[maybe_unused]] RenderData const &data,
    [[maybe_unused]] VkCommandBuffer cmd) override
  {}

private:
  std::vector<std::function<bool(vulkan::Context &)>> syncs_;
};

}// namespace vkgsplat::compute

#endif// VKGSPLAT_BACKEND_VULKAN_COMPUTE_OP_TENSOR_SYNC_LOCAL_HPP
