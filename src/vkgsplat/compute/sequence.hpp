#pragma once
#include "compute/operation.hpp"
#include <memory>
#include <vector>

namespace vkgsplat {

struct Init;
struct RenderData;

namespace compute {

  class Sequence
  {
  public:
    auto record(std::shared_ptr<Operation> op) -> Sequence &
    {
      operations_.push_back(std::move(op));
      return *this;
    }

    void eval(Init &init, RenderData const &data, VkCommandBuffer cmd) const
    {
      for (auto const &op : operations_) { op->pre_eval(init, data, cmd); }
      for (auto const &op : operations_) { op->record(init, data, cmd); }
      for (auto const &op : operations_) { op->post_eval(init, data, cmd); }
    }

    void clear() { operations_.clear(); }

  private:
    std::vector<std::shared_ptr<Operation>> operations_;
  };

}// namespace compute

}// namespace vkgsplat
