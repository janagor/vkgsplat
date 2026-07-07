#include "swapchain.hpp"

#include <expected>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include "error.hpp"
#include "graphics_pipeline.hpp"
#include "renderer.hpp"
#include "vulkan_bootstrap.hpp"

#include <VkBootstrap.h>

namespace vkgsplat {

auto create_swapchain(Init &init) -> std::expected<void, Error>
{
  vkb::SwapchainBuilder swapchain_builder{ init.device };
  return VKBResultToExpected(swapchain_builder.set_old_swapchain(init.swapchain).build())
    .and_then([&](auto const &swapchain) {
      vkb::destroy_swapchain(init.swapchain);
      init.swapchain = swapchain;
      return std::expected<void, Error>{};
    });
}

auto create_swapchain_images(Init &init, RenderData &data) -> int
{
  data.swapchain_images = init.swapchain.get_images().value();
  data.swapchain_image_views = init.swapchain.get_image_views().value();
  return 0;
}

auto recreate_swapchain(Init &init, RenderData &data) -> int
{
  init.disp.deviceWaitIdle();

  init.disp.destroyCommandPool(data.command_pool, nullptr);
  init.disp.destroyPipeline(data.graphics_pipeline, nullptr);

  init.swapchain.destroy_image_views(data.swapchain_image_views);

  if (!create_swapchain(init).has_value()) { return -1; }
  if (0 != create_graphics_pipeline(init, data)) { return -1; }
  if (0 != create_swapchain_images(init, data)) { return -1; }
  if (0 != create_command_pool(init, data)) { return -1; }
  if (0 != create_command_buffers(init, data)) { return -1; }
  return 0;
}

}// namespace vkgsplat
