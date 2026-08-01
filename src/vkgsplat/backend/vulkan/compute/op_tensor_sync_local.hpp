#pragma once

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
  template<TriviallyCopyable T>
  void add(Tensor<T> &tensor)
  {
    syncs_.emplace_back([&tensor](Init &init) { return tensor.sync_from_device(init); });
  }

  void post_eval(Init &init, RenderData const &data, VkCommandBuffer cmd) override;
  void record([[maybe_unused]] Init const &init,
    [[maybe_unused]] RenderData const &data,
    [[maybe_unused]] VkCommandBuffer cmd) override
  {}

private:
  std::vector<std::function<bool(Init &)>> syncs_;
};

}// namespace vkgsplat::compute
