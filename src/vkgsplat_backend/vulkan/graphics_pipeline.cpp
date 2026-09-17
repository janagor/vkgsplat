#include "vulkan/graphics_pipeline.hpp"

#include <cstdint>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app_state.hpp"
#include "shader.hpp"
#include "vulkan_context.hpp"

#include <vkexec/sync_wait.hpp>
#include <vkexec_extensions/descriptor_heap/heap_graphics_pipeline.hpp>
#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat {
namespace {

  [[nodiscard]] auto SpirvWords(std::vector<char> const &code, std::string_view path)
    -> std::optional<std::span<uint32_t const>>
  {
    if (code.size() < sizeof(uint32_t) || (code.size() % sizeof(uint32_t)) != 0) {
      std::println("Invalid SPIR-V size for {}: {}", path, code.size());
      return std::nullopt;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return std::span{ reinterpret_cast<uint32_t const *>(code.data()), code.size() / sizeof(uint32_t) };
  }

}// namespace

void DestroyGraphicsPipeline(vulkan::Context &context, RenderData &data)
{
  (void)context;
  data.graphics_pipeline.reset();
}

auto CreateGraphicsPipeline(vulkan::Context &context, RenderData &data) -> int
{
  DestroyGraphicsPipeline(context, data);

  if (context.vkexec_context == nullptr) {
    std::println("vkexec context missing for graphics pipeline");
    return -1;
  }

  std::string const vert_path = std::string(kShaderDirectory) + "/sphere.vert.spv";
  std::string const frag_path = std::string(kShaderDirectory) + "/sphere.frag.spv";
  auto const vert_code = ReadFile(vert_path);
  auto const frag_code = ReadFile(frag_path);
  auto const vert_spirv = SpirvWords(vert_code, vert_path);
  auto const frag_spirv = SpirvWords(frag_code, frag_path);
  if (!vert_spirv || !frag_spirv) { return -1; }

  vkexec::heap_graphics_layout_desc desc{};
  desc.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  desc.blend = vkexec::blend_mode::premultiplied;
  desc.depth_test = false;
  desc.depth_write = false;
  desc.color_formats = { data.color_format };
  desc.depth_format = VK_FORMAT_UNDEFINED;

  auto created = vkexec::try_sync_wait_value(
    vkexec::heap_graphics_pipeline::create(*context.vkexec_context, *vert_spirv, *frag_spirv, desc));
  if (!created) {
    std::println("Failed to create heap graphics pipeline: {}", created.error().message());
    return -1;
  }

  data.graphics_pipeline = std::move(*created);
  return 0;
}

}// namespace vkgsplat
