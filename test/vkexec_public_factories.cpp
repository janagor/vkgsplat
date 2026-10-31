// Include order is the regression: tensor.hpp introduces factory::detail.
// clang-format off
#include <vkexec/tensor.hpp>
#include <vkexec/compute_pipeline.hpp>
#include <vkexec_extensions/timeline_semaphore/frame_ring.hpp>
#include <vkexec_vma/allocator.hpp>
// clang-format on

#include <cstdint>
#include <span>
#include <string_view>

void compile_public_factories(vkexec::context &ctx,
  std::span<std::uint32_t const> spirv,
  std::string_view glsl,
  vkexec::layout_desc const &layout,
  vkexec::frame_ring_create_info const &info)
{
  [[maybe_unused]] auto spv = vkexec::factory::make_compute_pipeline(ctx, spirv, layout);
  [[maybe_unused]] auto src = vkexec::factory::make_compute_pipeline(ctx, glsl, layout);
  [[maybe_unused]] auto ring = vkexec::factory::make_frame_ring(ctx, info);
  [[maybe_unused]] auto allocator = vkexec::vma::factory::make_allocator(ctx);
}
