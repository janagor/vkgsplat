#pragma once

#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::compute {

struct SortEntry
{
  f32 luminance{};
  u32 index{};
};

constexpr auto kSortEntrySize =  sizeof(f32) + sizeof(u32);
static_assert(sizeof(SortEntry) == sizeof(f32) + sizeof(u32));

}// namespace vkgsplat::compute
