#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/renderer_adapter.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_headless/headless_platform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace {

inline constexpr int kSkipped = 77;
inline constexpr int kExpectedArguments = 3;
inline constexpr int kRgbChannels = 3;
inline constexpr size_t kGreenChannel = 1U;
inline constexpr size_t kBlueChannel = 2U;
inline constexpr int kColorTolerance = 4;
inline constexpr size_t kMinimumColoredPixels = 1'000U;

auto Run(std::filesystem::path const &scene_path, std::filesystem::path const &image_path) -> int
{
  auto const profile = vkgsplat::benchmark::BaselineProfile();
  auto platform = vkgsplat::HeadlessPlatform::create({ .width = profile.width, .height = profile.height });
  if (!platform) {
    std::cerr << platform.error().message() << '\n';
    return EXIT_FAILURE;
  }
  auto renderer = vkgsplat::Renderer::create(vkgsplat::benchmark::MakeRendererConfig(profile, scene_path), *platform);
  if (!renderer) {
    std::cerr << renderer.error().message() << '\n';
    if (renderer.error().code() == std::make_error_code(std::errc::function_not_supported)) {
      std::cout << "SKIP: headless Vulkan support is unavailable\n";
      return kSkipped;
    }
    return EXIT_FAILURE;
  }
  vkgsplat::Camera const camera{ profile.camera };
  if (auto result = renderer->draw(camera); !result) {
    std::cerr << result.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (auto result = renderer->wait_idle(); !result) {
    std::cerr << result.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (renderer->completed_frame_count() != 1U) {
    std::cerr << "Expected one completed frame\n";
    return EXIT_FAILURE;
  }
  if (auto result = renderer->save_frame_png(image_path.string()); !result) {
    std::cerr << result.error().message() << '\n';
    return EXIT_FAILURE;
  }
  int width{};
  int height{};
  int channels{};
  auto *raw_pixels = stbi_load(image_path.string().c_str(), &width, &height, &channels, kRgbChannels);
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> const pixels{ raw_pixels, stbi_image_free };
  if (!pixels || !std::cmp_equal(width, profile.width) || !std::cmp_equal(height, profile.height)) {
    std::cerr << "Invalid PNG dimensions or content\n";
    return EXIT_FAILURE;
  }
  size_t colored_pixels{};
  auto const pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
  std::span<stbi_uc const> const image_bytes{ pixels.get(), pixel_count * static_cast<size_t>(kRgbChannels) };
  for (size_t index = 0; index < pixel_count; ++index) {
    auto const offset = index * kRgbChannels;
    if (image_bytes[offset] > kColorTolerance || image_bytes[offset + kGreenChannel] > kColorTolerance
        || image_bytes[offset + kBlueChannel] > kColorTolerance) {
      ++colored_pixels;
    }
  }
  if (colored_pixels < kMinimumColoredPixels) {
    std::cerr << "Rendered scene has too few non-background pixels: " << colored_pixels << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "Rendered " << colored_pixels << " non-background pixels\n";
  return EXIT_SUCCESS;
}

}// namespace

auto main(int argc, char **argv) -> int
{
  if (argc != kExpectedArguments) {
    std::cerr << "Usage: fixture_gpu_smoke SCENE.ply OUTPUT.png\n";
    return EXIT_FAILURE;
  }
  try {
    std::span<char *> const arguments{ argv, static_cast<size_t>(argc) };
    return Run(arguments[1], arguments[2]);
  } catch (std::exception const &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
