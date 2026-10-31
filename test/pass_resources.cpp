#include "vulkan/gs/pass_resources.hpp"
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <stdexcept>
#include <tuple>
using namespace vkgsplat::gs;
using A = vkexec::resource_access;
using U = vkexec::buffer_usage;
namespace {
PassResourceBindings Bindings()
{
  PassResourceBindings b;
  b.geometry = { reinterpret_cast<VkBuffer>(1ULL), 65536 };
  b.appearance = { reinterpret_cast<VkBuffer>(2ULL), 65536 };
  b.projected = { reinterpret_cast<VkBuffer>(3ULL), 65536 };
  b.projection_cameras = { reinterpret_cast<VkBuffer>(4ULL), 65536 };
  b.unsorted_keys = { reinterpret_cast<VkBuffer>(5ULL), 65536 };
  b.unsorted_values = { reinterpret_cast<VkBuffer>(6ULL), 65536 };
  b.instance_count = { reinterpret_cast<VkBuffer>(7ULL), 65536 };
  b.sorted_keys = { reinterpret_cast<VkBuffer>(8ULL), 65536 };
  b.sorted_values = { reinterpret_cast<VkBuffer>(9ULL), 65536 };
  b.sort_histogram = { reinterpret_cast<VkBuffer>(10ULL), 65536 };
  b.radix_dispatch = { reinterpret_cast<VkBuffer>(11ULL), 65536 };
  b.draw_indirect = { reinterpret_cast<VkBuffer>(12ULL), 65536 };
  b.tile_ranges = { reinterpret_cast<VkBuffer>(13ULL), 65536 };
  b.tiled_counts = { reinterpret_cast<VkBuffer>(14ULL), 65536 };
  b.tiled_offsets = { reinterpret_cast<VkBuffer>(15ULL), 65536 };
  b.tiled_total = { reinterpret_cast<VkBuffer>(16ULL), 65536 };
  b.tiled_overflow = { reinterpret_cast<VkBuffer>(17ULL), 65536 };
  b.tiled_radix_prefix = { reinterpret_cast<VkBuffer>(18ULL), 65536 };
  b.tiled_radix_group = { reinterpret_cast<VkBuffer>(19ULL), 65536 };
  b.tiled_debug_readback = { reinterpret_cast<VkBuffer>(20ULL), 65536 };
  b.color_target = { reinterpret_cast<VkImage>(99ULL),
    { VK_IMAGE_ASPECT_COLOR_BIT, 2, 1, 3, 2 },
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
  return b;
}
using Tuple = std::tuple<VkBuffer, VkDeviceSize, VkDeviceSize, U, A>;
void Expect(PassResources const &r, std::initializer_list<vkexec::buffer_use> expected)
{
  auto normalize = [](auto const &uses) {
    std::vector<Tuple> result;
    for (auto const &u : uses) result.emplace_back(u.buffer, u.offset, u.size, u.usage, u.access);
    std::sort(result.begin(), result.end());
    return result;
  };
  REQUIRE(normalize(r.buffers) == normalize(expected));
}
vkexec::buffer_use E(BufferBinding b, A a, U u = U::storage_compute, VkDeviceSize size = 65536)
{ return { b.handle, 0, size, u, a }; }
}// namespace
TEST_CASE("PR04 independently specified shader access manifest", "[pass_resources]")
{
  auto b = Bindings();
  Expect(ProjectionResources(b),
    { E(b.geometry, A::read, U::storage_compute, 65536),
      E(b.appearance, A::read, U::storage_compute, 65536),
      E(b.projected, A::write, U::storage_compute, 65536) });
  Expect(BinningResources(b),
    { E(b.projected, A::read, U::storage_compute, 65536),
      E(b.unsorted_keys, A::write, U::storage_compute, 65536),
      E(b.unsorted_values, A::write, U::storage_compute, 65536),
      E(b.instance_count, A::read_write, U::storage_compute, 4) });
  Expect(SortPreparationResources(b),
    { E(b.instance_count, A::read, U::storage_compute, 4),
      E(b.unsorted_keys, A::read, U::storage_compute, 65536),
      E(b.unsorted_values, A::read, U::storage_compute, 65536),
      E(b.sorted_keys, A::write, U::storage_compute, 65536),
      E(b.sorted_values, A::write, U::storage_compute, 65536),
      E(b.radix_dispatch, A::write, U::storage_compute, 12),
      E(b.draw_indirect, A::write, U::storage_compute, 16) });
  Expect(GraphicsRangeResources(b),
    { E(b.sorted_keys, A::read, U::storage_compute, 65536),
      E(b.instance_count, A::read, U::storage_compute, 4),
      E(b.tile_ranges, A::write, U::storage_compute, 65536) });
  Expect(tiled::CountResources(b),
    { E(b.projected, A::read, U::storage_compute, 65536), E(b.tiled_counts, A::write, U::storage_compute, 65536) });
  Expect(tiled::ScanResources(b, { 0 }),
    { E(b.tiled_counts, A::read, U::storage_compute, 65536),
      E(b.tiled_offsets, A::write, U::storage_compute, 65536),
      E(b.tiled_overflow, A::read_write, U::storage_compute, 4) });
  Expect(tiled::PrepareResources(b),
    { E(b.tiled_counts, A::read, U::storage_compute, 65536),
      E(b.tiled_offsets, A::read_write, U::storage_compute, 65536),
      E(b.tiled_total, A::write, U::storage_compute, 4),
      E(b.tiled_overflow, A::read_write, U::storage_compute, 4),
      E(b.radix_dispatch, A::write, U::storage_compute, 12) });
  Expect(tiled::EmitResources(b),
    { E(b.projected, A::read, U::storage_compute, 65536),
      E(b.tiled_counts, A::read, U::storage_compute, 65536),
      E(b.tiled_offsets, A::read, U::storage_compute, 65536),
      E(b.tiled_overflow, A::read_write, U::storage_compute, 4),
      E(b.sorted_keys, A::write, U::storage_compute, 65536),
      E(b.sorted_values, A::write, U::storage_compute, 65536) });
  Expect(tiled::RadixPrefixResources(b, PrefixPhase::Local),
    { E(b.tiled_total, A::read, U::storage_compute, 4),
      E(b.sort_histogram, A::read, U::storage_compute, 65536),
      E(b.tiled_radix_prefix, A::write, U::storage_compute, 65536),
      E(b.tiled_radix_group, A::write, U::storage_compute, 65536) });
  Expect(tiled::RadixPrefixResources(b, PrefixPhase::Groups),
    { E(b.tiled_total, A::read, U::storage_compute, 4),
      E(b.tiled_radix_group, A::read_write, U::storage_compute, 65536) });
  Expect(tiled::RadixPrefixResources(b, PrefixPhase::Add),
    { E(b.tiled_total, A::read, U::storage_compute, 4),
      E(b.tiled_radix_group, A::read, U::storage_compute, 65536),
      E(b.tiled_radix_prefix, A::read_write, U::storage_compute, 65536) });
  Expect(tiled::RangeResources(b),
    { E(b.sorted_keys, A::read, U::storage_compute, 65536),
      E(b.tiled_total, A::read, U::storage_compute, 4),
      E(b.tiled_overflow, A::read_write, U::storage_compute, 4),
      E(b.tile_ranges, A::write, U::storage_compute, 65536),
      E(b.radix_dispatch, A::read, U::indirect, 12) });
  Expect(LegacyTiledRasterizationResources(b),
    { E(b.projected, A::read, U::storage_compute, 65536),
      E(b.sorted_values, A::read, U::storage_compute, 65536),
      E(b.tile_ranges, A::read, U::storage_compute, 65536) });
  Expect(tiled::RasterizationResources(b),
    { E(b.projected, A::read, U::storage_compute, 65536),
      E(b.sorted_values, A::read, U::storage_compute, 65536),
      E(b.tile_ranges, A::read, U::storage_compute, 65536),
      E(b.tiled_overflow, A::read_write, U::storage_compute, 4) });
}
TEST_CASE("PR04 radix invocation manifest", "[pass_resources]")
{
  auto b = Bindings();
  // Explicit shader slot sequence for all eight bytes; graphics uses first four.
  std::array<BufferBinding, 8> keys{ b.sorted_keys,
    b.unsorted_keys,
    b.sorted_keys,
    b.unsorted_keys,
    b.sorted_keys,
    b.unsorted_keys,
    b.sorted_keys,
    b.unsorted_keys };
  std::array<BufferBinding, 8> values{ b.sorted_values,
    b.unsorted_values,
    b.sorted_values,
    b.unsorted_values,
    b.sorted_values,
    b.unsorted_values,
    b.sorted_values,
    b.unsorted_values };
  for (unsigned i = 0; i < 8; ++i) {
    auto hist = tiled::RadixHistogramResources(b, { i });
    Expect(hist,
      { E(keys[i], A::read),
        E(b.sort_histogram, A::write),
        E(b.tiled_total, A::read, U::storage_compute, 4),
        E(b.radix_dispatch, A::read, U::indirect, 12) });
    auto scatter = tiled::RadixScatterResources(b, { i });
    Expect(scatter,
      { E(keys[i], A::read),
        E(values[i], A::read),
        E(keys[i ^ 1U], A::write),
        E(values[i ^ 1U], A::write),
        E(b.tiled_radix_prefix, A::read),
        E(b.tiled_total, A::read, U::storage_compute, 4),
        E(b.radix_dispatch, A::read, U::indirect, 12) });
    if (i < 4) {
      Expect(RadixHistogramResources(b, { i }),
        { E(keys[i], A::read),
          E(b.sort_histogram, A::write),
          E(b.instance_count, A::read, U::storage_compute, 4),
          E(b.radix_dispatch, A::read, U::indirect, 12) });
      Expect(RadixScatterResources(b, { i }),
        { E(keys[i], A::read),
          E(values[i], A::read),
          E(keys[i ^ 1U], A::write),
          E(values[i ^ 1U], A::write),
          E(b.sort_histogram, A::read),
          E(b.instance_count, A::read, U::storage_compute, 4),
          E(b.radix_dispatch, A::read, U::indirect, 12) });
    }
  }
  REQUIRE_THROWS_AS(RadixHistogramResources(b, { 4 }), std::invalid_argument);
  REQUIRE_THROWS_AS(RadixScatterResources(b, { 4 }), std::invalid_argument);
  REQUIRE_THROWS_AS(tiled::RadixHistogramResources(b, { 8 }), std::invalid_argument);
  REQUIRE_THROWS_AS(tiled::RadixScatterResources(b, { 8 }), std::invalid_argument);
}
TEST_CASE("PR04 scan hierarchy selects only invoked level", "[pass_resources]")
{
  auto b = Bindings();
  b.scan_sums = { { reinterpret_cast<VkBuffer>(30ULL), 64 }, { reinterpret_cast<VkBuffer>(31ULL), 4 } };
  b.scan_offsets = { { reinterpret_cast<VkBuffer>(32ULL), 64 }, { reinterpret_cast<VkBuffer>(33ULL), 4 } };
  Expect(tiled::ScanResources(b, { 0 }),
    { E(b.tiled_counts, A::read),
      E(b.tiled_offsets, A::write),
      E(b.tiled_overflow, A::read_write, U::storage_compute, 4),
      E(b.scan_sums[0], A::write, U::storage_compute, 64) });
  Expect(tiled::ScanResources(b, { 1 }),
    { E(b.scan_sums[0], A::read, U::storage_compute, 64),
      E(b.scan_offsets[0], A::write, U::storage_compute, 64),
      E(b.tiled_overflow, A::read_write, U::storage_compute, 4),
      E(b.scan_sums[1], A::write, U::storage_compute, 4) });
  Expect(tiled::ScanResources(b, { 2 }),
    { E(b.scan_sums[1], A::read, U::storage_compute, 4),
      E(b.scan_offsets[1], A::write, U::storage_compute, 4),
      E(b.tiled_overflow, A::read_write, U::storage_compute, 4) });
  Expect(tiled::ScanAddResources(b, { 0 }),
    { E(b.tiled_offsets, A::read_write),
      E(b.scan_offsets[0], A::read, U::storage_compute, 64),
      E(b.tiled_overflow, A::read_write, U::storage_compute, 4) });
  Expect(tiled::ScanAddResources(b, { 1 }),
    { E(b.scan_offsets[0], A::read_write, U::storage_compute, 64),
      E(b.scan_offsets[1], A::read, U::storage_compute, 4),
      E(b.tiled_overflow, A::read_write, U::storage_compute, 4) });
  REQUIRE_THROWS_AS(tiled::ScanResources(b, { 3 }), std::invalid_argument);
  REQUIRE_THROWS_AS(tiled::ScanAddResources(b, { 2 }), std::invalid_argument);
  b.scan_offsets.pop_back();
  REQUIRE_THROWS_AS(tiled::ScanResources(b, { 0 }), std::invalid_argument);
}
TEST_CASE("PR04 transfer and batch ranges", "[pass_resources]")
{
  auto b = Bindings();
  Expect(CounterInitializationResources(b), { E(b.instance_count, A::write, U::transfer_destination, 4) });
  Expect(tiled::OverflowInitializationResources(b), { E(b.tiled_overflow, A::write, U::transfer_destination, 4) });
  Expect(tiled::RangeInitializationResources(b), { E(b.tile_ranges, A::write, U::transfer_destination) });
  Expect(CameraUploadResources(b, { 4, 10 }), { E(b.projection_cameras, A::write, U::transfer_destination, 640) });
  Expect(BatchedProjectionResources(b, { 4, 10 }),
    { E(b.geometry, A::read),
      E(b.appearance, A::read),
      E(b.projected, A::write, U::storage_compute, 1760),
      E(b.projection_cameras, A::read, U::storage_compute, 640) });
  Expect(CopyResources(b.sorted_keys, b.tiled_debug_readback, 12, 80, 32),
    { { b.sorted_keys.handle, 12, 32, U::transfer_source, A::read },
      { b.tiled_debug_readback.handle, 80, 32, U::transfer_destination, A::write } });
  REQUIRE_THROWS_AS(CopyResources(b.sorted_keys, b.tiled_debug_readback, 65530, 0, 32), std::invalid_argument);
  REQUIRE_THROWS_AS(FillResources(b.instance_count, 1, 4), std::invalid_argument);
  REQUIRE_THROWS_AS(BatchedProjectionResources(b, { 0, 10 }), std::invalid_argument);
  REQUIRE_THROWS_AS(BatchedProjectionResources(b, { 100, 100 }), std::invalid_argument);
}
TEST_CASE("PR04 image identity usage layouts and blending", "[pass_resources]")
{
  auto b = Bindings();
  for (bool retain : { false, true }) {
    auto r = GraphicsRasterizationResources(b, retain);
    Expect(r,
      { E(b.projected, A::read, U::storage_graphics),
        E(b.sorted_values, A::read, U::storage_graphics),
        E(b.draw_indirect, A::read, U::indirect, 16) });
    REQUIRE(r.images.size() == 1);
    auto u = r.images[0];
    REQUIRE(u.image == b.color_target.handle);
    REQUIRE(u.range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
    REQUIRE(u.range.baseMipLevel == 2);
    REQUIRE(u.range.levelCount == 1);
    REQUIRE(u.range.baseArrayLayer == 3);
    REQUIRE(u.range.layerCount == 2);
    REQUIRE(u.access == A::read_write);
    REQUIRE(u.usage == vkexec::image_usage::color_attachment);
    REQUIRE(u.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    REQUIRE(u.initial_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  }
  auto tiled = tiled::RasterizationResources(b).images[0];
  REQUIRE(tiled.usage == vkexec::image_usage::storage_compute);
  REQUIRE(tiled.access == A::write);
  REQUIRE(tiled.layout == VK_IMAGE_LAYOUT_GENERAL);
  auto dst = b.color_target;
  dst.handle = reinterpret_cast<VkImage>(100ULL);
  auto blit = BlitResources(b.color_target, dst);
  REQUIRE(blit.images[0].usage == vkexec::image_usage::transfer_source);
  REQUIRE(blit.images[1].image == dst.handle);
  REQUIRE(blit.images[1].usage == vkexec::image_usage::transfer_destination);
  auto readback = ImageReadbackResources(b.color_target, b.tiled_debug_readback, 100);
  Expect(readback, { E(b.tiled_debug_readback, A::write, U::transfer_destination, 100) });
  REQUIRE(readback.images[0].access == A::read);
  b.color_target.range.layerCount = 0;
  REQUIRE_THROWS_AS(GraphicsRasterizationResources(b, true), std::invalid_argument);
  b = Bindings();
  b.color_target.handle = VK_NULL_HANDLE;
  REQUIRE_THROWS_AS(tiled::RasterizationResources(b), std::invalid_argument);
}
TEST_CASE("PR04 rejects each required shader buffer and accepts selected frame handles", "[pass_resources]")
{
  auto b = Bindings();
  for (auto member :
    { &PassResourceBindings::geometry, &PassResourceBindings::appearance, &PassResourceBindings::projected }) {
    auto missing = b;
    missing.*member = {};
    REQUIRE_THROWS_AS(ProjectionResources(missing), std::invalid_argument);
    missing = b;
    (missing.*member).size = 0;
    REQUIRE_THROWS_AS(ProjectionResources(missing), std::invalid_argument);
  }
  b.geometry.handle = reinterpret_cast<VkBuffer>(999ULL);
  REQUIRE(ProjectionResources(b).buffers[0].buffer == b.geometry.handle);
  REQUIRE(ProjectionResources(b).buffers[0].access == A::read);
  REQUIRE(ProjectionResources(b).buffers[1].access == A::read);
}

TEST_CASE("PR04 every declared allocation is required and scene inputs stay immutable", "[pass_resources]")
{
  std::vector<std::function<PassResources(PassResourceBindings const &)>> builders{ ProjectionResources,
    BinningResources,
    SortPreparationResources,
    CounterInitializationResources,
    GraphicsRangeResources,
    LegacyTiledRasterizationResources,
    tiled::CountResources,
    tiled::PrepareResources,
    tiled::EmitResources,
    tiled::RangeResources,
    tiled::RasterizationResources,
    tiled::OverflowInitializationResources,
    tiled::RangeInitializationResources,
    [](auto const &b) { return CameraUploadResources(b, { 2, 10 }); },
    [](auto const &b) { return BatchedProjectionResources(b, { 2, 10 }); },
    [](auto const &b) { return GraphicsRasterizationResources(b, false); },
    [](auto const &b) { return tiled::ScanResources(b, { 0 }); },
    [](auto const &b) { return tiled::RadixPrefixResources(b, PrefixPhase::Local); },
    [](auto const &b) { return tiled::RadixPrefixResources(b, PrefixPhase::Groups); },
    [](auto const &b) { return tiled::RadixPrefixResources(b, PrefixPhase::Add); } };
  for (unsigned i = 0; i < 4; ++i) {
    builders.push_back([i](auto const &b) { return RadixHistogramResources(b, { i }); });
    builders.push_back([i](auto const &b) { return RadixScatterResources(b, { i }); });
  }
  for (unsigned i = 0; i < 8; ++i) {
    builders.push_back([i](auto const &b) { return tiled::RadixHistogramResources(b, { i }); });
    builders.push_back([i](auto const &b) { return tiled::RadixScatterResources(b, { i }); });
  }
  for (auto const &builder : builders) {
    auto b = Bindings();
    auto r = builder(b);
    for (auto const &u : r.buffers) {
      if (u.buffer == b.geometry.handle || u.buffer == b.appearance.handle) REQUIRE(u.access == A::read);
      for (auto member : { &PassResourceBindings::geometry,
             &PassResourceBindings::appearance,
             &PassResourceBindings::projected,
             &PassResourceBindings::projection_cameras,
             &PassResourceBindings::unsorted_keys,
             &PassResourceBindings::unsorted_values,
             &PassResourceBindings::instance_count,
             &PassResourceBindings::sorted_keys,
             &PassResourceBindings::sorted_values,
             &PassResourceBindings::sort_histogram,
             &PassResourceBindings::radix_dispatch,
             &PassResourceBindings::draw_indirect,
             &PassResourceBindings::tile_ranges,
             &PassResourceBindings::tiled_counts,
             &PassResourceBindings::tiled_offsets,
             &PassResourceBindings::tiled_total,
             &PassResourceBindings::tiled_overflow,
             &PassResourceBindings::tiled_radix_prefix,
             &PassResourceBindings::tiled_radix_group,
             &PassResourceBindings::tiled_debug_readback }) {
        if ((b.*member).handle != u.buffer) continue;
        auto missing = b;
        missing.*member = {};
        REQUIRE_THROWS_AS(builder(missing), std::invalid_argument);
        missing = b;
        (missing.*member).size = 0;
        REQUIRE_THROWS_AS(builder(missing), std::invalid_argument);
      }
    }
  }
  auto b = Bindings();
  REQUIRE_THROWS_AS(tiled::RadixPrefixResources(b, static_cast<PrefixPhase>(99)), std::invalid_argument);
  REQUIRE_THROWS_AS(tiled::ScanAddResources(b, { 0 }), std::invalid_argument);
  b.color_target.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  REQUIRE_THROWS_AS(GraphicsRasterizationResources(b, true), std::invalid_argument);
  REQUIRE_NOTHROW(GraphicsRasterizationResources(b, false));
  b = Bindings();
  b.projected.size = VK_WHOLE_SIZE;
  REQUIRE_THROWS_AS(ProjectionResources(b), std::invalid_argument);
}

TEST_CASE("PR04 preserved image reads reject undefined incoming layouts", "[pass_resources]")
{
  auto b = Bindings();
  auto source = b.color_target;
  source.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  REQUIRE_THROWS_AS(BlitResources(source, b.color_target), std::invalid_argument);
  REQUIRE_THROWS_AS(ImageReadbackResources(source, b.tiled_debug_readback, 100), std::invalid_argument);
  auto destination = source;
  destination.handle = reinterpret_cast<VkImage>(100ULL);
  auto blit = BlitResources(b.color_target, destination);
  REQUIRE(blit.images[0].initial_layout == b.color_target.initial_layout);
  REQUIRE(blit.images[1].initial_layout == VK_IMAGE_LAYOUT_UNDEFINED);
  REQUIRE(blit.images[1].access == A::write);
  b.color_target = source;
  REQUIRE_NOTHROW(GraphicsRasterizationResources(b, false));
  REQUIRE_THROWS_AS(GraphicsRasterizationResources(b, true), std::invalid_argument);
}
TEST_CASE("PR04 nonzero ranges and independent resource substitutions", "[pass_resources]")
{
  auto b = Bindings();
  REQUIRE_THROWS_AS(FillResources(b.instance_count, 65536, 4), std::invalid_argument);
  REQUIRE_THROWS_AS(FillResources(b.instance_count, 65532, 8), std::invalid_argument);
  REQUIRE_THROWS_AS(CopyResources(b.sorted_keys, b.tiled_debug_readback, 4, 65532, 8), std::invalid_argument);
  REQUIRE_THROWS_AS(CopyResources(b.sorted_keys, b.tiled_debug_readback, 65532, 4, 8), std::invalid_argument);
  auto selected = b;
  selected.sorted_keys.handle = reinterpret_cast<VkBuffer>(1001ULL);
  selected.unsorted_keys.handle = reinterpret_cast<VkBuffer>(1002ULL);
  selected.radix_dispatch.handle = reinterpret_cast<VkBuffer>(1003ULL);
  Expect(RadixHistogramResources(selected, { 0 }),
    { E(selected.sorted_keys, A::read),
      E(b.sort_histogram, A::write),
      E(b.instance_count, A::read, U::storage_compute, 4),
      E(selected.radix_dispatch, A::read, U::indirect, 12) });
  Expect(RadixHistogramResources(selected, { 1 }),
    { E(selected.unsorted_keys, A::read),
      E(b.sort_histogram, A::write),
      E(b.instance_count, A::read, U::storage_compute, 4),
      E(selected.radix_dispatch, A::read, U::indirect, 12) });
  selected.sorted_keys = b.sorted_keys;
  Expect(RadixHistogramResources(selected, { 0 }),
    { E(b.sorted_keys, A::read),
      E(b.sort_histogram, A::write),
      E(b.instance_count, A::read, U::storage_compute, 4),
      E(selected.radix_dispatch, A::read, U::indirect, 12) });
}
