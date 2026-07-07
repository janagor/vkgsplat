#include "compute/param.hpp"

#include "app_state.hpp"
#include "compute/algorithm.hpp"
#include "descriptor/descriptor_heap.hpp"

#include <cstdint>
#include <vector>

namespace vkgsplat::compute {

auto ParamList::descriptor_mappings(RenderData const &data) const -> std::vector<DescriptorMapping>
{
  std::vector<DescriptorMapping> mappings;
  mappings.reserve(params_.size());

  for (uint32_t binding = 0; binding < params_.size(); ++binding) {
    mappings.push_back({
      .binding = binding,
      .heap_offset = heap_slot_byte_offset(data, params_.at(binding)->heap_slot()),
    });
  }

  return mappings;
}

}// namespace vkgsplat::compute
