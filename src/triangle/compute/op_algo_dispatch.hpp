#pragma once
#include "compute/algorithm.hpp"
#include "compute/operation.hpp"
#include <array>

namespace vkgsplat::compute {

class OpAlgoDispatch : public Operation {
public:
  OpAlgoDispatch(Algorithm const &algorithm, std::array<uint32_t, 3> workgroup_size)
      : algorithm_(algorithm), workgroup_size_(workgroup_size) {}

  void record(Init const &init, RenderData const &data, VkCommandBuffer cmd) override;

private:
  Algorithm const &algorithm_;
  std::array<uint32_t, 3> workgroup_size_;
};

} // namespace vkgsplat::compute
