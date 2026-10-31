#include "gs/pass_resources_detail.hpp"
namespace vkgsplat::gs::tiled {
using namespace resource_detail;
PassResources CountResources(PassResourceBindings const &b)
{ return { { Buffer(b.projected, A::read), Buffer(b.tiled_counts, A::write) }, {} }; }
namespace {
  void Hierarchy(PassResourceBindings const &b, ScanInvocation i, bool add)
  {
    if (b.scan_sums.size() != b.scan_offsets.size() || i.level > b.scan_sums.size()
        || (add && i.level == b.scan_sums.size()))
      throw std::invalid_argument("invalid scan hierarchy level");
  }
}// namespace
PassResources ScanResources(PassResourceBindings const &b, ScanInvocation i)
{
  Hierarchy(b, i, false);
  PassResources r{ { Buffer(i.level ? b.scan_sums.at(i.level - 1) : b.tiled_counts, A::read),
                     Buffer(i.level ? b.scan_offsets.at(i.level - 1) : b.tiled_offsets, A::write),
                     Buffer(b.tiled_overflow, A::read_write, U::storage_compute, 0, 4) },
    {} };
  if (i.level < b.scan_sums.size()) r.buffers.push_back(Buffer(b.scan_sums.at(i.level), A::write));
  return r;
}
PassResources ScanAddResources(PassResourceBindings const &b, ScanInvocation i)
{
  Hierarchy(b, i, true);
  return { { Buffer(i.level ? b.scan_offsets.at(i.level - 1) : b.tiled_offsets, A::read_write),
             Buffer(b.scan_offsets.at(i.level), A::read),
             Buffer(b.tiled_overflow, A::read_write, U::storage_compute, 0, 4) },
    {} };
}
PassResources PrepareResources(PassResourceBindings const &b)
{
  return { { Buffer(b.tiled_counts, A::read),
             Buffer(b.tiled_offsets, A::read_write),
             Buffer(b.tiled_total, A::write, U::storage_compute, 0, 4),
             Buffer(b.tiled_overflow, A::read_write, U::storage_compute, 0, 4),
             Buffer(b.radix_dispatch, A::write, U::storage_compute, 0, sizeof(VkDispatchIndirectCommand)) },
    {} };
}
PassResources EmitResources(PassResourceBindings const &b)
{
  return { { Buffer(b.projected, A::read),
             Buffer(b.tiled_counts, A::read),
             Buffer(b.tiled_offsets, A::read),
             Buffer(b.tiled_overflow, A::read_write, U::storage_compute, 0, 4),
             Buffer(b.sorted_keys, A::write),
             Buffer(b.sorted_values, A::write) },
    {} };
}
PassResources RadixHistogramResources(PassResourceBindings const &b, RadixInvocation i)
{
  Iteration(i, 8);
  return { { Buffer(i.iteration % 2 ? b.unsorted_keys : b.sorted_keys, A::read),
             Buffer(b.sort_histogram, A::write),
             Buffer(b.tiled_total, A::read, U::storage_compute, 0, 4),
             Indirect(b) },
    {} };
}
PassResources RadixPrefixResources(PassResourceBindings const &b, PrefixPhase phase)
{
  PassResources r{ { Buffer(b.tiled_total, A::read, U::storage_compute, 0, 4) }, {} };
  switch (phase) {
  case PrefixPhase::Local:
    r.buffers.push_back(Buffer(b.sort_histogram, A::read));
    r.buffers.push_back(Buffer(b.tiled_radix_prefix, A::write));
    r.buffers.push_back(Buffer(b.tiled_radix_group, A::write));
    break;
  case PrefixPhase::Groups:
    r.buffers.push_back(Buffer(b.tiled_radix_group, A::read_write));
    break;
  case PrefixPhase::Add:
    r.buffers.push_back(Buffer(b.tiled_radix_group, A::read));
    r.buffers.push_back(Buffer(b.tiled_radix_prefix, A::read_write));
    break;
  default:
    throw std::invalid_argument("invalid prefix phase");
  }
  return r;
}
PassResources RadixScatterResources(PassResourceBindings const &b, RadixInvocation i)
{
  Iteration(i, 8);
  return { { Buffer(i.iteration % 2 ? b.unsorted_keys : b.sorted_keys, A::read),
             Buffer(i.iteration % 2 ? b.unsorted_values : b.sorted_values, A::read),
             Buffer(i.iteration % 2 ? b.sorted_keys : b.unsorted_keys, A::write),
             Buffer(i.iteration % 2 ? b.sorted_values : b.unsorted_values, A::write),
             Buffer(b.tiled_radix_prefix, A::read),
             Buffer(b.tiled_total, A::read, U::storage_compute, 0, 4),
             Indirect(b) },
    {} };
}
PassResources RangeResources(PassResourceBindings const &b)
{
  return { { Buffer(b.sorted_keys, A::read),
             Buffer(b.tiled_total, A::read, U::storage_compute, 0, 4),
             Buffer(b.tiled_overflow, A::read_write, U::storage_compute, 0, 4),
             Buffer(b.tile_ranges, A::write),
             Indirect(b) },
    {} };
}
PassResources RasterizationResources(PassResourceBindings const &b)
{
  auto r = LegacyTiledRasterizationResources(b);
  r.buffers.push_back(Buffer(b.tiled_overflow, A::read_write, U::storage_compute, 0, 4));
  return r;
}
PassResources OverflowInitializationResources(PassResourceBindings const &b)
{ return FillResources(b.tiled_overflow, 0, 4); }
PassResources RangeInitializationResources(PassResourceBindings const &b)
{ return FillResources(b.tile_ranges, 0, b.tile_ranges.size); }
}// namespace vkgsplat::gs::tiled
