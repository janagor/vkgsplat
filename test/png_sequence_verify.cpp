#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <vkgsplat_utility/types.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int kRgbaChannels = 4;
constexpr int kArgumentCount = 5;

[[nodiscard]] auto Verify(std::filesystem::path const &directory,
  vkgsplat::u32 width,
  vkgsplat::u32 height,
  vkgsplat::u32 frame_count) -> bool
{
  std::vector<std::uint8_t> first_pixels;
  bool changed = false;
  for (vkgsplat::u32 index = 0U; index < frame_count; ++index) {
    auto const path = directory / std::format("frame_{:06}.png", index);
    auto const filename = path.string();
    int decoded_width{};
    int decoded_height{};
    int channels{};
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{
      stbi_load(filename.c_str(), &decoded_width, &decoded_height, &channels, kRgbaChannels), stbi_image_free
    };
    if (!pixels || decoded_width != static_cast<int>(width) || decoded_height != static_cast<int>(height)) {
      std::cerr << "Missing or invalid sequence PNG: " << filename << '\n';
      return false;
    }
    auto const byte_count = static_cast<size_t>(width) * height * static_cast<size_t>(kRgbaChannels);
    std::vector<std::uint8_t> const decoded{ pixels.get(), pixels.get() + byte_count };
    if (index == 0U) { first_pixels = decoded; }
    else if (decoded != first_pixels) { changed = true; }
  }
  size_t file_count = 0U;
  for (auto const &entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".png") { return false; }
    ++file_count;
  }
  if (file_count != frame_count || !changed) {
    std::cerr << "Sequence frame count or orbit contents are incorrect\n";
    return false;
  }
  return true;
}
}// namespace

auto main(int argc, char **argv) -> int
{
  if (argc != kArgumentCount) { return EXIT_FAILURE; }
  try {
    auto const width = static_cast<vkgsplat::u32>(std::stoul(argv[2]));
    auto const height = static_cast<vkgsplat::u32>(std::stoul(argv[3]));
    auto const count = static_cast<vkgsplat::u32>(std::stoul(argv[4]));
    return Verify(argv[1], width, height, count) ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (...) {
    return EXIT_FAILURE;
  }
}
