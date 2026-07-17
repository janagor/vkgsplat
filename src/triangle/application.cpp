#include "application.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <utility>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app_config.hpp"
#include "app_state.hpp"
#include "backend/vulkan/depth_buffer.hpp"
#include "backend/vulkan/device.hpp"
#include "backend/vulkan/gpu_allocator.hpp"
#include "backend/vulkan/graphics_pipeline.hpp"
#include "backend/vulkan/renderer.hpp"
#include "backend/vulkan/swapchain.hpp"
#include "camera.hpp"
#include "io/ply/load_splats.hpp"
#include "mesh_gpu.hpp"
#include "sphere_setup.hpp"
#include "vulkan_context.hpp"


namespace vkgsplat {

auto run(std::span<char *const> args) noexcept -> int
{
  try {
    auto const config = parse_app_config(args);
    if (!config) { return -1; }

    RenderData render_data;
    render_data.procedural = config->source == SplatSource::Procedural;

    std::optional<SplatCpuData> splats{};
    if (!render_data.procedural) {
      auto loaded = load_splats_from_ply(config->ply_path, config->splat_count);
      if (!loaded) {
        std::println(stderr, "Failed to load splats: {}", loaded.error());
        return -1;
      }
      splats = std::move(*loaded);
      std::println("Loaded {} splats from {}", splats->positions.size(), config->ply_path);
    } else {
      std::println("Using procedural mode with {} spheres", config->splat_count);
    }

    Init init;

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

    if (render_data.procedural) {
      if (!create_sphere_buffers(init, render_data, config->splat_count)) { return -1; }
    } else {
      if (!create_sphere_buffers(init, render_data, config->splat_count, std::cref(*splats))) { return -1; }
    }

    render_data.sort_size = next_power_of_2(render_data.splat_count);
    if (!init_sphere_setup(init, render_data)) { return -1; }
    if (0 != create_graphics_pipeline(init, render_data)) { return -1; }
    if (!create_depth_buffer(init, render_data)) { return -1; }
    if (0 != create_command_resources(init, render_data)) { return -1; }
    if (0 != create_sync_objects(init, render_data)) { return -1; }

    // NOLINTNEXTLINE(misc-const-correctness) -- updated by input handlers once wired up
    Camera camera{ k_default_camera_position };

    while (0 == glfwWindowShouldClose(init.window)) {
      glfwPollEvents();
      int const res = draw_frame(init, render_data, camera);
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
