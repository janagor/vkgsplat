#include "application.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <print>
#include <utility>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app_state.hpp"
#include "backend/vulkan/device.hpp"
#include "depth_buffer.hpp"
#include "graphics_pipeline.hpp"
#include "mesh_gpu.hpp"
#include "renderer.hpp"
#include "sphere_setup.hpp"
#include "vulkan_context.hpp"

#include <backend/vulkan/gpu_allocator.hpp>
#include <backend/vulkan/swapchain.hpp>

namespace vkgsplat {

auto run() noexcept -> int
{
  try {
    Init init;
    RenderData render_data;

    auto const init_result = device_initialization(init);
    if (!init_result.has_value()) {
      std::println("Inicjalizacja urządzenia nie powiodła się: {}", init_result.error().message());
      return -1;
    }

    auto gpu_allocator = vulkan::GPUAllocator::create(init.instance, init.device, init.device.physical_device);
    if (!gpu_allocator) {
      std::println("Nie udało się utworzyć alokatora GPU!");
      return -1;
    }
    init.gpu_allocator = std::move(*gpu_allocator);

    auto swapchain = vulkan::Swapchain::create(init.device, init.window, std::ref(init.disp));
    if (!swapchain) { return -1; }
    init.swapchain = std::make_unique<vulkan::Swapchain>(std::move(*swapchain));

    if (!get_queues(init, render_data).has_value()) { return -1; }
    if (!create_sphere_buffers(init, render_data)) { return -1; }
    if (!init_sphere_setup(init, render_data)) { return -1; }
    if (0 != create_graphics_pipeline(init, render_data)) { return -1; }
    if (!create_depth_buffer(init, render_data)) { return -1; }
    if (0 != create_command_pool(init, render_data)) { return -1; }
    if (0 != create_command_buffers(init, render_data)) { return -1; }
    if (0 != create_sync_objects(init, render_data)) { return -1; }

    while (0 == glfwWindowShouldClose(init.window)) {
      glfwPollEvents();
      int const res = draw_frame(init, render_data);
      if (res != 0) {
        std::cout << "failed to draw frame \n";
        return -1;
      }
    }
    init.disp.deviceWaitIdle();

    cleanup(init, render_data);
  } catch (std::exception const &) {
    return -1;
  } catch (...) {
    return -1;
  }
  return 0;
}

}// namespace vkgsplat
