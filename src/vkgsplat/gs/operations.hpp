#pragma once

#include "compute/operation.hpp"
#include "gs/binning.hpp"
#include "gs/projection.hpp"
#include "gs/rasterization.hpp"
#include "gs/sorting.hpp"

namespace vkgsplat::gs {

class OpProjection : public compute::Operation
{
public:
  void record(Init const &init, RenderData const &data, VkCommandBuffer cmd) override
  { dispatch_projection(init, data, data.project_push, cmd); }
};

class OpBinning : public compute::Operation
{
public:
  void record(Init const &init, RenderData const &data, VkCommandBuffer cmd) override
  { dispatch_binning(init, data, data.bin_push, cmd); }
};

class OpSorting : public compute::Operation
{
public:
  void record(Init const &init, RenderData const &data, VkCommandBuffer cmd) override
  { dispatch_sorting(init, data, data.sort_push, cmd); }
};

class OpRasterization : public compute::Operation
{
public:
  void record(Init const &init, RenderData const &data, VkCommandBuffer cmd) override
  {
    dispatch_rasterization(init, data, data.raster_push, cmd, data.present_image_index);
  }
};

}// namespace vkgsplat::gs
