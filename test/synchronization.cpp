#include "src/vkgsplat_backend/vulkan/gs/quilt_attachment_barrier.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

TEST_CASE("Quilt LOAD dependency preserves the atlas and scopes attachment access", "[synchronization]")
{
  // A distinct synthetic handle must survive parameter construction unchanged.
  auto const image = reinterpret_cast<VkImage>(static_cast<uintptr_t>(0x1234));
  auto const barrier = vkgsplat::gs::QuiltAttachmentBarrier(image);
  REQUIRE(vkgsplat::gs::kQuiltAttachmentDependencyFlags == VK_DEPENDENCY_BY_REGION_BIT);
  REQUIRE(barrier.image == image);
  REQUIRE(barrier.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
  REQUIRE(barrier.srcStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
  REQUIRE(barrier.dstStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
  REQUIRE(barrier.srcAccessMask == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  REQUIRE(barrier.dstAccessMask == (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT));
  REQUIRE(barrier.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  REQUIRE(barrier.newLayout == barrier.oldLayout);
  REQUIRE(barrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
  REQUIRE(barrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
  REQUIRE(barrier.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
  REQUIRE(barrier.subresourceRange.baseMipLevel == 0);
  REQUIRE(barrier.subresourceRange.levelCount == 1);
  REQUIRE(barrier.subresourceRange.baseArrayLayer == 0);
  REQUIRE(barrier.subresourceRange.layerCount == 1);
}
