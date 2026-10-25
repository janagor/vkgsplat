#include "benchmark/fixture/ply_writer.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/engine.hpp>
#include <vkgsplat/frame.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_headless/offscreen_platform.hpp>
#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr vkgsplat::u32 kWidth = 64U;
constexpr vkgsplat::u32 kHeight = 64U;
constexpr vkgsplat::u32 kBoundaryWidth = 17U;
constexpr vkgsplat::u32 kHdWidth = 1280U;
constexpr vkgsplat::u32 kHdHeight = 720U;
constexpr vkgsplat::u32 kFullHdWidth = 1920U;
constexpr vkgsplat::u32 kFullHdHeight = 1080U;
constexpr vkgsplat::u32 kUhdWidth = 3840U;
constexpr vkgsplat::u32 kUhdHeight = 2160U;
constexpr vkgsplat::u32 kChannels = 4U;
constexpr vkgsplat::u32 kTileSize = 16U;
constexpr vkgsplat::u32 kProjectedStride = 11U;
constexpr vkgsplat::u32 kProjectedColorOffset = 7U;
constexpr vkgsplat::u32 kColorChannels = 3U;
constexpr vkgsplat::u32 kChannelMaximum = 255U;
constexpr vkgsplat::f32 kOffset = 0.35F;
constexpr vkgsplat::f32 kLogScale = 0.5F;
constexpr vkgsplat::f32 kSmallLogScale = -2.5F;
constexpr vkgsplat::f32 kOpacityLogit = 4.0F;
constexpr vkgsplat::f32 kShDc = 1.0F / vkgsplat::gs::kShC0;
constexpr double kCameraDistance = 6.0;
constexpr vkgsplat::f32 kBehindCameraZ = 12.0F;
constexpr double kCameraSideOffset = 1.0;
constexpr double kMinimumPsnrDb = 20.0;
constexpr double kMinimumForegroundPsnrDb = 20.0;
constexpr vkgsplat::u32 kForegroundThreshold = 8U;
constexpr size_t kPercentileNumerator = 99U;
constexpr size_t kPercentileDenominator = 100U;
constexpr vkgsplat::u64 kOverflowBudgetBytes = 32U;
constexpr float kAlphaMaximum = 0.99F;
constexpr float kAlphaMinimum = 1.0F / static_cast<float>(kChannelMaximum);
constexpr float kTransmittanceMinimum = 1.0e-4F;
constexpr vkgsplat::u32 kReferenceColorTolerance = 2U;

struct ReferenceIntersection
{
  vkgsplat::u32 depth_bits{};
  vkgsplat::u32 tile{};
  vkgsplat::u32 gaussian{};
};

[[nodiscard]] auto ReferenceBounds(float mean, float radius, vkgsplat::u32 viewport,
  vkgsplat::u32 grid, bool upper) -> vkgsplat::u32
{
  float const edge = std::clamp(mean + (upper ? radius : -radius), 0.0F, static_cast<float>(viewport));
  float const tile = edge / static_cast<float>(kTileSize);
  return std::min(static_cast<vkgsplat::u32>(upper ? std::ceil(tile) : std::floor(tile)), grid);
}

[[nodiscard]] auto BuildReferenceIntersections(vkgsplat::Renderer::TiledDebugSnapshot const &snapshot,
  vkgsplat::RgbaFrame const &frame, std::vector<ReferenceIntersection> &intersections) -> bool
{
  for (vkgsplat::u32 gaussian = 0U; gaussian < snapshot.counts.size(); ++gaussian) {
    size_t const base = static_cast<size_t>(gaussian) * kProjectedStride;
    float const mean_x = snapshot.projected.at(base);
    float const mean_y = snapshot.projected.at(base + 1U);
    float const depth = snapshot.projected.at(base + 5U);
    float const radius = snapshot.projected.at(base + 6U);
    vkgsplat::u32 first_x{};
    vkgsplat::u32 first_y{};
    vkgsplat::u32 last_x{};
    vkgsplat::u32 last_y{};
    if (radius >= 1.0F && std::isfinite(radius) && std::isfinite(depth) && depth > 0.0F
        && std::isfinite(mean_x) && std::isfinite(mean_y)
        && mean_x + radius > 0.0F && mean_y + radius > 0.0F
        && mean_x - radius < static_cast<float>(frame.width)
        && mean_y - radius < static_cast<float>(frame.height)) {
      first_x = ReferenceBounds(mean_x, radius, frame.width, snapshot.tiles_x, false);
      first_y = ReferenceBounds(mean_y, radius, frame.height, snapshot.tiles_y, false);
      last_x = ReferenceBounds(mean_x, radius, frame.width, snapshot.tiles_x, true);
      last_y = ReferenceBounds(mean_y, radius, frame.height, snapshot.tiles_y, true);
    }
    vkgsplat::u32 const count = (last_x - first_x) * (last_y - first_y);
    if (snapshot.counts.at(gaussian) != count || snapshot.offsets.at(gaussian) != intersections.size()) {
      std::cerr << "tile count or exclusive offset differs at Gaussian " << gaussian << '\n';
      return false;
    }
    for (vkgsplat::u32 tile_y = first_y; tile_y < last_y; ++tile_y) {
      for (vkgsplat::u32 tile_x = first_x; tile_x < last_x; ++tile_x) {
        intersections.push_back({ .depth_bits = std::bit_cast<vkgsplat::u32>(depth),
          .tile = (tile_y * snapshot.tiles_x) + tile_x, .gaussian = gaussian });
      }
    }
  }
  return true;
}

[[nodiscard]] auto ValidateIntersectionOrder(vkgsplat::Renderer::TiledDebugSnapshot const &snapshot,
  std::vector<ReferenceIntersection> &intersections, std::vector<vkgsplat::u32> &ranges) -> bool
{
  if (snapshot.offsets.back() != intersections.size() || snapshot.intersections != intersections.size()
      || snapshot.emitted_indices.size() != intersections.size()) { return false; }
  for (size_t index = 0U; index < intersections.size(); ++index) {
    auto const &entry = intersections.at(index);
    if (snapshot.emitted_key_words.at(index * 2U) != entry.depth_bits
        || snapshot.emitted_key_words.at((index * 2U) + 1U) != entry.tile
        || snapshot.emitted_indices.at(index) != entry.gaussian) {
      std::cerr << "emitted key or Gaussian index differs at " << index << '\n';
      return false;
    }
  }
  std::ranges::stable_sort(intersections,
    [](ReferenceIntersection const &left, ReferenceIntersection const &right) -> bool {
      return left.tile == right.tile ? left.depth_bits < right.depth_bits : left.tile < right.tile;
    });
  for (size_t index = 0U; index < intersections.size(); ++index) {
    auto const &entry = intersections.at(index);
    if (snapshot.sorted_key_words.at(index * 2U) != entry.depth_bits
        || snapshot.sorted_key_words.at((index * 2U) + 1U) != entry.tile
        || snapshot.sorted_indices.at(index) != entry.gaussian) {
      std::cerr << "stable radix order differs at " << index << '\n';
      return false;
    }
    size_t const range_base = static_cast<size_t>(entry.tile) * 2U;
    if (index == 0U || intersections.at(index - 1U).tile != entry.tile) {
      ranges.at(range_base) = static_cast<vkgsplat::u32>(index);
    }
    ranges.at(range_base + 1U) = static_cast<vkgsplat::u32>(index + 1U);
  }
  if (ranges != snapshot.range_words) { std::cerr << "half-open tile ranges differ\n"; return false; }
  return true;
}

[[nodiscard]] auto ReferencePixelColor(vkgsplat::Renderer::TiledDebugSnapshot const &snapshot,
  std::vector<ReferenceIntersection> const &intersections, std::vector<vkgsplat::u32> const &ranges,
  vkgsplat::u32 pixel_x, vkgsplat::u32 pixel_y) -> std::array<float, kColorChannels>
{
  vkgsplat::u32 const tile = ((pixel_y / kTileSize) * snapshot.tiles_x) + (pixel_x / kTileSize);
  size_t const range_base = static_cast<size_t>(tile) * 2U;
  std::array<float, kColorChannels> color{};
  float transmittance = 1.0F;
  for (vkgsplat::u32 index = ranges.at(range_base); index < ranges.at(range_base + 1U); ++index) {
    size_t const base = static_cast<size_t>(intersections.at(index).gaussian) * kProjectedStride;
    float const delta_x = snapshot.projected.at(base) - (static_cast<float>(pixel_x) + 0.5F);
    float const delta_y = snapshot.projected.at(base + 1U) - (static_cast<float>(pixel_y) + 0.5F);
    float const radius = snapshot.projected.at(base + 6U);
    if ((delta_x * delta_x) + (delta_y * delta_y) > radius * radius) { continue; }
    float const power = (-0.5F * ((snapshot.projected.at(base + 2U) * delta_x * delta_x)
      + (snapshot.projected.at(base + 4U) * delta_y * delta_y)))
      - (snapshot.projected.at(base + 3U) * delta_x * delta_y);
    if (power > 0.0F) { continue; }
    float const alpha = std::min(kAlphaMaximum, snapshot.projected.at(base + 10U) * std::exp(power));
    if (alpha < kAlphaMinimum) { continue; }
    for (vkgsplat::u32 channel = 0U; channel < kColorChannels; ++channel) {
      color.at(channel) += alpha * transmittance * snapshot.projected.at(base + kProjectedColorOffset + channel);
    }
    transmittance *= 1.0F - alpha;
    if (transmittance < kTransmittanceMinimum) { break; }
  }
  return color;
}

[[nodiscard]] auto ValidatePixels(vkgsplat::Renderer::TiledDebugSnapshot const &snapshot,
  vkgsplat::RgbaFrame const &frame, std::vector<ReferenceIntersection> const &intersections,
  std::vector<vkgsplat::u32> const &ranges) -> bool
{
  for (vkgsplat::u32 pixel_y = 0U; pixel_y < frame.height; ++pixel_y) {
    for (vkgsplat::u32 pixel_x = 0U; pixel_x < frame.width; ++pixel_x) {
      auto const color = ReferencePixelColor(snapshot, intersections, ranges, pixel_x, pixel_y);
      for (vkgsplat::u32 channel = 0U; channel < kColorChannels; ++channel) {
        auto const expected = static_cast<int>(std::lround(std::clamp(color.at(channel), 0.0F, 1.0F)
          * static_cast<float>(kChannelMaximum)));
        auto const actual = static_cast<int>(frame.pixels.at((((static_cast<size_t>(pixel_y) * frame.width) + pixel_x)
          * kChannels) + channel));
        if (std::abs(expected - actual) > static_cast<int>(kReferenceColorTolerance)) {
          std::cerr << "CPU compositor differs at pixel " << pixel_x << ',' << pixel_y << '\n';
          return false;
        }
      }
    }
  }
  return true;
}

[[nodiscard]] auto ValidateSnapshot(vkgsplat::Renderer::TiledDebugSnapshot const &snapshot,
  vkgsplat::RgbaFrame const &frame) -> bool
{
  if (snapshot.projected.size() != snapshot.counts.size() * kProjectedStride
      || snapshot.offsets.size() != snapshot.counts.size() + 1U) { return false; }
  std::vector<ReferenceIntersection> intersections;
  if (!BuildReferenceIntersections(snapshot, frame, intersections)) { return false; }
  std::vector<vkgsplat::u32> ranges(static_cast<size_t>(snapshot.tiles_x) * snapshot.tiles_y * 2U);
  if (!ValidateIntersectionOrder(snapshot, intersections, ranges)) { return false; }
  return ValidatePixels(snapshot, frame, intersections, ranges);
}

[[nodiscard]] auto WriteScene(std::filesystem::path const &path, vkgsplat::f32 log_scale) -> bool
{
  vkgsplat::SplatCpuData scene;
  for (auto const position_x : { -kOffset, kOffset }) {
    for (auto const position_y : { -kOffset, kOffset }) {
      scene.geometries.push_back(vkgsplat::gs::GaussianGeometry{
        .position = { position_x, position_y, 0.0F },
        .scale = { log_scale, log_scale, log_scale },
        .opacity = kOpacityLogit,
      });
      scene.appearances.push_back(vkgsplat::gs::GaussianAppearance{
        .f_dc = { kShDc, kShDc, kShDc },
      });
    }
  }
  scene.geometries.push_back(vkgsplat::gs::GaussianGeometry{
    .position = { 0.0F, 0.0F, 0.0F },
    .scale = { log_scale, log_scale, log_scale },
    .opacity = kOpacityLogit,
  });
  scene.appearances.push_back(vkgsplat::gs::GaussianAppearance{
    .f_dc = { kShDc, kShDc, kShDc },
  });
  auto written = vkgsplat::benchmark::WritePly(path, scene);
  if (!written) { std::cerr << written.error().message() << '\n'; }
  return written.has_value();
}

[[nodiscard]] auto WriteEmptyIntersectionScene(std::filesystem::path const &path) -> bool
{
  vkgsplat::SplatCpuData scene;
  scene.geometries.push_back(vkgsplat::gs::GaussianGeometry{
    .position = { 0.0F, 0.0F, kBehindCameraZ },
    .scale = { kLogScale, kLogScale, kLogScale },
    .opacity = kOpacityLogit,
  });
  scene.appearances.push_back(vkgsplat::gs::GaussianAppearance{
    .f_dc = { kShDc, kShDc, kShDc },
  });
  auto written = vkgsplat::benchmark::WritePly(path, scene);
  if (!written) { std::cerr << written.error().message() << '\n'; }
  return written.has_value();
}

[[nodiscard]] auto HasScenePixels(vkgsplat::RgbaFrame const &frame) -> bool
{
  for (size_t pixel = 0U; pixel < frame.pixels.size() / kChannels; ++pixel) {
    for (vkgsplat::u32 channel = 0U; channel < kColorChannels; ++channel) {
      if (frame.pixels.at((pixel * kChannels) + channel) != 0U) { return true; }
    }
  }
  return false;
}

[[nodiscard]] auto CompareFrames(vkgsplat::RgbaFrame const &graphics,
  vkgsplat::RgbaFrame const &compute,
  std::filesystem::path const &heatmap_path) -> bool
{
  if (graphics.width != compute.width || graphics.height != compute.height
      || graphics.pixels.size() != compute.pixels.size()) { return false; }
  std::ofstream heatmap(heatmap_path, std::ios::binary);
  if (!heatmap) { return false; }
  heatmap << "P6\n" << graphics.width << ' ' << graphics.height << "\n" << kChannelMaximum << '\n';
  double squared_error = 0.0;
  double foreground_squared_error = 0.0;
  size_t foreground_samples = 0U;
  std::array<size_t, kChannelMaximum + 1U> error_histogram{};
  vkgsplat::u32 maximum_error = 0U;
  for (size_t pixel = 0U; pixel < graphics.pixels.size() / kChannels; ++pixel) {
    vkgsplat::u32 pixel_error = 0U;
    for (vkgsplat::u32 channel = 0U; channel < kColorChannels; ++channel) {
      size_t const index = (pixel * kChannels) + channel;
      auto const difference = static_cast<int>(graphics.pixels.at(index))
                            - static_cast<int>(compute.pixels.at(index));
      auto const magnitude = static_cast<vkgsplat::u32>(std::abs(difference));
      pixel_error = std::max(pixel_error, magnitude);
      maximum_error = std::max(maximum_error, magnitude);
      squared_error += static_cast<double>(difference * difference);
      ++error_histogram.at(magnitude);
      if (graphics.pixels.at(index) > kForegroundThreshold
          || compute.pixels.at(index) > kForegroundThreshold) {
        foreground_squared_error += static_cast<double>(difference * difference);
        ++foreground_samples;
      }
    }
    heatmap.put(static_cast<char>(pixel_error));
    heatmap.put(0);
    heatmap.put(0);
  }
  auto const sample_count = (static_cast<double>(graphics.pixels.size()) / kChannels) * kColorChannels;
  double const mse = squared_error / sample_count;
  double const psnr = mse == 0.0 ? std::numeric_limits<double>::infinity()
                                 : 10.0 * std::log10(static_cast<double>(kChannelMaximum * kChannelMaximum) / mse);
  double const foreground_mse = foreground_samples == 0U ? 0.0
    : foreground_squared_error / static_cast<double>(foreground_samples);
  double const foreground_psnr = foreground_mse == 0.0 ? std::numeric_limits<double>::infinity()
    : 10.0 * std::log10(static_cast<double>(kChannelMaximum * kChannelMaximum) / foreground_mse);
  size_t const percentile_rank = ((static_cast<size_t>(sample_count) * kPercentileNumerator)
    + kPercentileDenominator - 1U) / kPercentileDenominator;
  size_t accumulated = 0U;
  vkgsplat::u32 percentile_error = 0U;
  for (vkgsplat::u32 error = 0U; error <= kChannelMaximum; ++error) {
    accumulated += error_histogram.at(error);
    if (accumulated >= percentile_rank) { percentile_error = error; break; }
  }
  std::cout << "graphics/compute PSNR=" << psnr << " dB, foreground PSNR=" << foreground_psnr
            << " dB, p99 channel error=" << percentile_error
            << ", maximum channel error=" << maximum_error << '\n';
  return HasScenePixels(graphics) && HasScenePixels(compute) && foreground_samples > 0U
    && psnr >= kMinimumPsnrDb && foreground_psnr >= kMinimumForegroundPsnrDb && heatmap.good();
}

[[nodiscard]] auto RunExtentComparisons(std::filesystem::path const &scene_path,
  std::filesystem::path const &small_scene_path, std::filesystem::path const &heatmap_path,
  vkgsplat::Camera const &camera, vkgsplat::RendererConfig config) -> bool
{
  constexpr std::array<std::array<vkgsplat::u32, 2>, 4> kExtents{
    { { kBoundaryWidth, kBoundaryWidth }, { kHdWidth, kHdHeight },
      { kFullHdWidth, kFullHdHeight }, { kUhdWidth, kUhdHeight } }
  };
  config.rasterization_mode = vkgsplat::RasterizationMode::kGraphicsPipeline;
  config.enable_tiled_debug_readback = false;
  for (auto const &extent : kExtents) {
    config.ply_path = extent.at(0) == kBoundaryWidth ? scene_path.string() : small_scene_path.string();
    auto sized_platform = vkgsplat::OffscreenPlatform::create({ .width = extent.at(0), .height = extent.at(1) });
    if (!sized_platform) { std::cerr << sized_platform.error().message() << '\n'; return false; }
    auto sized_graphics = vkgsplat::Renderer::create(config, *sized_platform);
    if (!sized_graphics) { std::cerr << sized_graphics.error().message() << '\n'; return false; }
    auto sized_graphics_frame = sized_graphics->render_frame(camera);
    if (!sized_graphics_frame) {
      std::cerr << sized_graphics_frame.error().message() << '\n';
      return false;
    }
    config.rasterization_mode = vkgsplat::RasterizationMode::kComputeTiled;
    auto sized_compute = vkgsplat::Renderer::create(config, *sized_platform);
    if (!sized_compute) { std::cerr << sized_compute.error().message() << '\n'; return false; }
    auto sized_compute_frame = sized_compute->render_frame(camera);
    if (!sized_compute_frame) { std::cerr << sized_compute_frame.error().message() << '\n'; return false; }
    auto const sized_heatmap = heatmap_path.parent_path()
      / (heatmap_path.stem().string() + "-" + std::to_string(extent.at(0)) + "x"
         + std::to_string(extent.at(1)) + ".ppm");
    if (!CompareFrames(*sized_graphics_frame, *sized_compute_frame, sized_heatmap)) { return false; }
    config.rasterization_mode = vkgsplat::RasterizationMode::kGraphicsPipeline;
  }
  return true;
}

[[nodiscard]] auto RunEdgeCases(std::filesystem::path const &scene_path,
  std::filesystem::path const &empty_scene_path, vkgsplat::Camera const &camera,
  vkgsplat::OffscreenPlatform &platform, vkgsplat::RendererConfig config) -> bool
{
  config.rasterization_mode = vkgsplat::RasterizationMode::kComputeTiled;
  config.ply_path = scene_path.string();
  config.enable_tiled_debug_readback = true;
  auto broad_platform = vkgsplat::OffscreenPlatform::create({ .width = kFullHdWidth, .height = kFullHdHeight });
  if (!broad_platform) { std::cerr << broad_platform.error().message() << '\n'; return false; }
  auto broad_compute = vkgsplat::Renderer::create(config, *broad_platform);
  if (!broad_compute) { std::cerr << broad_compute.error().message() << '\n'; return false; }
  auto broad_frame = broad_compute->render_frame(camera);
  if (!broad_frame) { std::cerr << broad_frame.error().message() << '\n'; return false; }
  auto broad_snapshot = broad_compute->tiled_debug_snapshot();
  if (!broad_snapshot || !ValidateSnapshot(*broad_snapshot, *broad_frame)) { return false; }
  config.ply_path = empty_scene_path.string();
  auto empty_compute = vkgsplat::Renderer::create(config, platform);
  if (!empty_compute) { std::cerr << empty_compute.error().message() << '\n'; return false; }
  auto empty_frame = empty_compute->render_frame(camera);
  if (!empty_frame) { std::cerr << empty_frame.error().message() << '\n'; return false; }
  auto empty_snapshot = empty_compute->tiled_debug_snapshot();
  if (!empty_snapshot || empty_snapshot->intersections != 0U
      || !ValidateSnapshot(*empty_snapshot, *empty_frame)) { return false; }
  config.enable_tiled_debug_readback = false;
  config.ply_path = scene_path.string();
  config.tiled_intersection_budget_bytes = kOverflowBudgetBytes;
  auto overflow_renderer = vkgsplat::Renderer::create(config, platform);
  if (!overflow_renderer) { std::cerr << overflow_renderer.error().message() << '\n'; return false; }
  if (overflow_renderer->render_frame(camera)) {
    std::cerr << "truncated tiled frame was reported as successful\n";
    return false;
  }
  config.tiled_intersection_budget_bytes = vkgsplat::kDefaultTiledIntersectionBudgetBytes;
  config.rasterization_mode = vkgsplat::RasterizationMode::kGraphicsPipeline;
  auto shared_engine = vkgsplat::Engine::create({}, platform);
  if (!shared_engine) { std::cerr << shared_engine.error().message() << '\n'; return false; }
  auto shared_graphics = vkgsplat::Renderer::create(config, *shared_engine);
  if (!shared_graphics || !shared_graphics->render_frame(camera)) {
    std::cerr << "graphics renderer failed with a shared engine\n";
    return false;
  }
  config.rasterization_mode = vkgsplat::RasterizationMode::kComputeTiled;
  auto unsupported_compute = vkgsplat::Renderer::create(config, *shared_engine);
  if (unsupported_compute || unsupported_compute.error().code() != std::errc::operation_not_supported) {
    std::cerr << "compute renderer accepted a shared engine without shaderInt64\n";
    return false;
  }
  auto compute_engine = vkgsplat::Engine::create({ .require_shader_int64 = true }, platform);
  if (!compute_engine) { std::cerr << compute_engine.error().message() << '\n'; return false; }
  auto shared_compute = vkgsplat::Renderer::create(config, *compute_engine);
  if (!shared_compute || !shared_compute->render_frame(camera)) {
    std::cerr << "compute renderer failed with a shaderInt64 enabled shared engine\n";
    return false;
  }
  return true;
}

[[nodiscard]] auto Run(std::filesystem::path const &scene_path, std::filesystem::path const &heatmap_path) -> int
{
  if (!WriteScene(scene_path, kLogScale)) { return EXIT_FAILURE; }
  auto const small_scene_path = scene_path.parent_path() / (scene_path.stem().string() + "-small.ply");
  if (!WriteScene(small_scene_path, kSmallLogScale)) { return EXIT_FAILURE; }
  auto const empty_scene_path = scene_path.parent_path() / (scene_path.stem().string() + "-empty.ply");
  if (!WriteEmptyIntersectionScene(empty_scene_path)) { return EXIT_FAILURE; }
  auto platform = vkgsplat::OffscreenPlatform::create({ .width = kWidth, .height = kHeight });
  if (!platform) { std::cerr << platform.error().message() << '\n'; return EXIT_FAILURE; }
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{
    .position = { 0.0, 0.0, kCameraDistance },
    .target = { 0.0, 0.0, 0.0 },
    .orientation = std::nullopt,
  } };
  vkgsplat::RendererConfig config{
    .ply_path = scene_path.string(),
    .enable_validation = true,
    .enable_imgui = false,
    .submission_policy = vkgsplat::FrameSubmissionPolicy::kLossless,
  };
  auto graphics = vkgsplat::Renderer::create(config, *platform);
  if (!graphics) { std::cerr << graphics.error().message() << '\n'; return EXIT_FAILURE; }
  auto graphics_frame = graphics->render_frame(camera);
  if (!graphics_frame) { std::cerr << graphics_frame.error().message() << '\n'; return EXIT_FAILURE; }
  config.rasterization_mode = vkgsplat::RasterizationMode::kComputeTiled;
  config.enable_tiled_debug_readback = true;
  auto compute = vkgsplat::Renderer::create(config, *platform);
  if (!compute) { std::cerr << compute.error().message() << '\n'; return EXIT_FAILURE; }
  auto compute_frame = compute->render_frame(camera);
  if (!compute_frame) { std::cerr << compute_frame.error().message() << '\n'; return EXIT_FAILURE; }
  auto snapshot = compute->tiled_debug_snapshot();
  if (!snapshot || !ValidateSnapshot(*snapshot, *compute_frame)) { return EXIT_FAILURE; }
  if (!CompareFrames(*graphics_frame, *compute_frame, heatmap_path)) { return EXIT_FAILURE; }
  vkgsplat::Camera const side_camera{ vkgsplat::CameraConfig{
    .position = { kCameraSideOffset, 0.0, kCameraDistance },
    .target = { 0.0, 0.0, 0.0 },
    .orientation = std::nullopt,
  } };
  auto graphics_side = graphics->render_frame(side_camera);
  auto compute_side = compute->render_frame(side_camera);
  auto side_snapshot = compute->tiled_debug_snapshot();
  if (!side_snapshot || !compute_side || !ValidateSnapshot(*side_snapshot, *compute_side)) {
    return EXIT_FAILURE;
  }
  auto side_heatmap_path = heatmap_path;
  side_heatmap_path.replace_extension(".side.ppm");
  if (!graphics_side || !compute_side
      || !CompareFrames(*graphics_side, *compute_side, side_heatmap_path)) {
    std::cerr << "camera-change image comparison failed\n";
    return EXIT_FAILURE;
  }
  if (!RunExtentComparisons(scene_path, small_scene_path, heatmap_path, camera, config)) {
    return EXIT_FAILURE;
  }
  if (!RunEdgeCases(scene_path, empty_scene_path, camera, *platform, config)) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

}// namespace

auto main(int argc, char **argv) -> int
{
  constexpr size_t kProgramNameCount = 1U;
  constexpr size_t kPathCount = 2U;
  constexpr int kArgumentCount = static_cast<int>(kProgramNameCount + kPathCount);
  if (argc != kArgumentCount) { return EXIT_FAILURE; }
  std::span<char *> const arguments{ argv, static_cast<size_t>(argc) };
  auto const paths = arguments.subspan<kProgramNameCount, kPathCount>();
  return Run(paths.front(), paths.back());
}
