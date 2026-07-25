#pragma once

#include "compute/operation.hpp"

namespace vkgsplat::gs {

class OpProjection : public compute::Operation
{
public:
  void record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer) override;
};

class OpBinning : public compute::Operation
{
public:
  void record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer) override;
};

class OpPrepareSort : public compute::Operation
{
public:
  void record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer) override;
};

class OpRadixSort : public compute::Operation
{
public:
  void record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer) override;
};

class OpRasterization : public compute::Operation
{
public:
  void record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer) override;
};

}// namespace vkgsplat::gs
