#include "video_session.hpp"

#include "video_capabilities.hpp"
#include "video_device.hpp"
#include "vulkan_context.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <vk_video/vulkan_video_codec_h264std.h>
#include <vulkan/vulkan_core.h>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat::vulkan::video {
namespace {

  constexpr std::uint32_t kMacroblockSize = 16;
  constexpr std::uint32_t kParameterSetCount = 1;
  constexpr std::uint32_t kMaxQueryAttempts = 4;
  constexpr std::size_t kMaxParameterSetBytes = std::size_t{ 1024U } * 1024U;
  constexpr std::uint8_t kBt709 = 1;
  constexpr std::uint8_t kUnspecifiedVideoFormat = 5;
  constexpr std::uint8_t kParameterSetId = 0;
  constexpr std::uint8_t kMinimumFrameNumberBitsMinusFour = 0;
  constexpr std::uint8_t kMinimumPocBitsMinusFour = 0;
  constexpr std::uint32_t kH264TicksPerFrame = 2U;

  [[nodiscard]] auto Failure(char const *operation, VkResult result) -> Error
  {
    return MakeError(result == VK_ERROR_DEVICE_LOST ? std::errc::no_such_device : std::errc::io_error,
      std::string{ operation } + " failed (VkResult " + std::to_string(static_cast<int>(result)) + ")");
  }

  [[nodiscard]] auto CreateParameters(Context &context, VkVideoSessionKHR handle,
    StdVideoH264ProfileIdc profile, StdVideoH264LevelIdc level,
    VkExtent2D extent, std::uint32_t fps_numerator, std::uint32_t fps_denominator,
    bool fixed_frame_rate)
    -> std::expected<VkVideoSessionParametersKHR, Error>
  {
    StdVideoH264SequenceParameterSetVui vui{};
    vui.flags.video_signal_type_present_flag = 1;
    vui.flags.color_description_present_flag = 1;
    vui.flags.video_full_range_flag = 0;
    vui.flags.timing_info_present_flag = 1;
    vui.flags.fixed_frame_rate_flag = fixed_frame_rate;
    vui.num_units_in_tick = fps_denominator;
    vui.time_scale = fps_numerator * kH264TicksPerFrame;
    vui.video_format = kUnspecifiedVideoFormat;
    vui.colour_primaries = kBt709;
    // The conversion path must consume BT.709-encoded RGB and produce limited-range YCbCr.
    vui.transfer_characteristics = kBt709;
    vui.matrix_coefficients = kBt709;

    // Zero initialization is required for the C Vulkan Std parameter structure;
    // profile_idc is assigned a valid enumerator immediately afterwards.
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    StdVideoH264SequenceParameterSet sps{};
    sps.flags.frame_mbs_only_flag = 1;
    sps.flags.vui_parameters_present_flag = 1;
    sps.flags.constraint_set0_flag = profile == STD_VIDEO_H264_PROFILE_IDC_BASELINE;
    sps.profile_idc = profile;
    sps.level_idc = level;
    sps.chroma_format_idc = STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;
    sps.seq_parameter_set_id = kParameterSetId;
    sps.log2_max_frame_num_minus4 = kMinimumFrameNumberBitsMinusFour;
    sps.pic_order_cnt_type = STD_VIDEO_H264_POC_TYPE_0;
    sps.log2_max_pic_order_cnt_lsb_minus4 = kMinimumPocBitsMinusFour;
    sps.max_num_ref_frames = 0;
    sps.pic_width_in_mbs_minus1 = (extent.width / kMacroblockSize) - 1U;
    sps.pic_height_in_map_units_minus1 = (extent.height / kMacroblockSize) - 1U;
    sps.pSequenceParameterSetVui = &vui;

    StdVideoH264PictureParameterSet pps{};
    pps.seq_parameter_set_id = kParameterSetId;
    pps.pic_parameter_set_id = kParameterSetId;
    pps.flags.entropy_coding_mode_flag = 0; // CAVLC, including Baseline profile.
    pps.flags.transform_8x8_mode_flag = 0;
    pps.pic_init_qp_minus26 = 0;
    VkVideoEncodeH264SessionParametersAddInfoKHR const add{
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR,
      .pNext = nullptr,
      .stdSPSCount = kParameterSetCount,
      .pStdSPSs = &sps,
      .stdPPSCount = kParameterSetCount,
      .pStdPPSs = &pps,
    };
    VkVideoEncodeH264SessionParametersCreateInfoKHR const h264{
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR,
      .pNext = nullptr,
      .maxStdSPSCount = kParameterSetCount,
      .maxStdPPSCount = kParameterSetCount,
      .pParametersAddInfo = &add,
    };
    VkVideoSessionParametersCreateInfoKHR const create{
      .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR,
      .pNext = &h264,
      .flags = 0,
      .videoSessionParametersTemplate = VK_NULL_HANDLE,
      .videoSession = handle,
    };
    VkVideoSessionParametersKHR parameters = VK_NULL_HANDLE;
    auto const result = context.disp.createVideoSessionParametersKHR(&create, nullptr, &parameters);
    if (result != VK_SUCCESS) { return std::unexpected(Failure("create H.264 session parameters", result)); }
    return parameters;
  }

  [[nodiscard]] auto ValidateSessionConfiguration(EncodeDevice const &device, VkExtent2D extent,
    std::uint32_t fps_numerator, std::uint32_t fps_denominator) -> std::expected<void, Error>
  {
    auto const &selection = device.selection;
    if (fps_numerator == 0U || fps_denominator == 0U
        || fps_numerator > std::numeric_limits<std::uint32_t>::max() / kH264TicksPerFrame) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "invalid H.264 frame rate"));
    }
    if (selection.profile_index >= device.capabilities.profiles.size()) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "invalid H.264 profile selection"));
    }
    auto const &profile = device.capabilities.profiles.at(selection.profile_index);
    if (selection.profile != profile.profile || profile.h264.maxLevelIdc == STD_VIDEO_H264_LEVEL_IDC_INVALID) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "invalid H.264 profile or level selection"));
    }
    if (extent.width == 0 || extent.height == 0 || extent.width % kMacroblockSize != 0
        || extent.height % kMacroblockSize != 0 || extent.width > selection.max_extent.width
        || extent.height > selection.max_extent.height
        || extent.width < profile.capabilities.minCodedExtent.width
        || extent.height < profile.capabilities.minCodedExtent.height) {
      return std::unexpected(MakeError(std::errc::invalid_argument,
        "H.264 coded extent must be supported and divisible by 16"));
    }
    if (selection.source_format.format != VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
        || selection.upload_strategy != UploadStrategy::kTransferDestination) {
      return std::unexpected(MakeError(std::errc::function_not_supported,
        "all-intra H.264 requires an NV12 encode source with transfer-destination support"));
    }
    if ((profile.encode.rateControlModes & VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) == 0) {
      return std::unexpected(MakeError(std::errc::function_not_supported,
        "all-intra H.264 requires disabled rate control"));
    }
    constexpr VkVideoEncodeFeedbackFlagsKHR kFeedback =
      static_cast<VkVideoEncodeFeedbackFlagsKHR>(VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BUFFER_OFFSET_BIT_KHR)
      | static_cast<VkVideoEncodeFeedbackFlagsKHR>(VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR);
    if ((profile.encode.supportedEncodeFeedbackFlags & kFeedback) != kFeedback) {
      return std::unexpected(MakeError(std::errc::function_not_supported,
        "H.264 bitstream offset and byte-count feedback are unavailable"));
    }
    return {};
  }

  [[nodiscard]] auto QueryMemoryRequirements(Context &context, VkVideoSessionKHR handle)
    -> std::expected<std::vector<VkVideoSessionMemoryRequirementsKHR>, Error>
  {
    std::vector<VkVideoSessionMemoryRequirementsKHR> requirements;
    bool enumerated = false;
    for (std::uint32_t attempt = 0; attempt < kMaxQueryAttempts; ++attempt) {
      std::uint32_t count = 0;
      auto result = context.disp.getVideoSessionMemoryRequirementsKHR(handle, &count, nullptr);
      if (result == VK_INCOMPLETE) { continue; }
      if (result != VK_SUCCESS) { return std::unexpected(Failure("query video session memory", result)); }
      requirements.resize(count);
      for (auto &entry : requirements) { entry.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR; }
      if (count == 0) {
        enumerated = true;
        break;
      }
      result = context.disp.getVideoSessionMemoryRequirementsKHR(handle, &count, requirements.data());
      if (result == VK_INCOMPLETE || count > requirements.size()) { continue; }
      if (result != VK_SUCCESS) { return std::unexpected(Failure("query video session memory", result)); }
      requirements.resize(count);
      enumerated = true;
      break;
    }
    if (!enumerated) {
      return std::unexpected(MakeError(std::errc::io_error, "video session memory enumeration remained incomplete"));
    }
    return requirements;
  }

}// namespace

auto VideoSession::CreateHandle(VideoSession &owned, EncodeDevice const &device, VkExtent2D extent)
  -> std::expected<void, Error>
{
  auto const &selection = device.selection;
  auto const &profile = device.capabilities.profiles.at(selection.profile_index);
  VkVideoEncodeH264ProfileInfoKHR h264_profile{
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR,
    .pNext = nullptr,
    .stdProfileIdc = selection.profile,
  };
  VkVideoProfileInfoKHR const video_profile{
    .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
    .pNext = &h264_profile,
    .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR,
    .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
    .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
    .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
  };
  VkVideoSessionCreateInfoKHR const create{
    .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR,
    .pNext = nullptr,
    .queueFamilyIndex = device.queue_family_index,
    .flags = 0,
    .pVideoProfile = &video_profile,
    .pictureFormat = selection.source_format.format,
    .maxCodedExtent = extent,
    .referencePictureFormat = VK_FORMAT_UNDEFINED,
    .maxDpbSlots = 0,
    .maxActiveReferencePictures = 0,
    .pStdHeaderVersion = &profile.capabilities.stdHeaderVersion,
  };
  auto const result = owned.context_.disp.createVideoSessionKHR(&create, nullptr, &owned.session_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create H.264 video session", result)); }

  return {};
}

auto VideoSession::BindMemory(VideoSession &owned) -> std::expected<void, Error>
{
  auto requirements = QueryMemoryRequirements(owned.context_, owned.session_);
  if (!requirements) { return std::unexpected(requirements.error()); }
  VkPhysicalDeviceMemoryProperties memory_properties{};
  owned.context_.inst_disp.getPhysicalDeviceMemoryProperties(owned.context_.device.physical_device, &memory_properties);
  std::vector<VkBindVideoSessionMemoryInfoKHR> bindings;
  bindings.reserve(requirements->size());
  owned.memory_.reserve(requirements->size());
  for (auto const &requirement : *requirements) {
    std::uint32_t type = memory_properties.memoryTypeCount;
    for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
      if ((requirement.memoryRequirements.memoryTypeBits & (1U << index)) != 0) {
        type = index;
        break;
      }
    }
    if (type == memory_properties.memoryTypeCount) {
      return std::unexpected(MakeError(std::errc::not_enough_memory, "no video session memory type"));
    }
    VkMemoryAllocateInfo const allocation{
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = nullptr,
      .allocationSize = requirement.memoryRequirements.size,
      .memoryTypeIndex = type,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    auto result = owned.context_.disp.allocateMemory(&allocation, nullptr, &memory);
    if (result != VK_SUCCESS) { return std::unexpected(Failure("allocate video session memory", result)); }
    owned.memory_.push_back(memory);
    bindings.push_back(VkBindVideoSessionMemoryInfoKHR{
      .sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR,
      .pNext = nullptr,
      .memoryBindIndex = requirement.memoryBindIndex,
      .memory = memory,
      .memoryOffset = 0,
      .memorySize = requirement.memoryRequirements.size,
    });
  }
  if (!bindings.empty()) {
    auto result = owned.context_.disp.bindVideoSessionMemoryKHR(owned.session_,
      static_cast<std::uint32_t>(bindings.size()), bindings.data());
    if (result != VK_SUCCESS) { return std::unexpected(Failure("bind video session memory", result)); }
  }
  return {};
}

auto VideoSession::RetrieveParameterSets(VideoSession &owned) -> std::expected<void, Error>
{
  VkVideoEncodeH264SessionParametersGetInfoKHR const h264_get{
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_KHR,
    .pNext = nullptr,
    .writeStdSPS = VK_TRUE,
    .writeStdPPS = VK_TRUE,
    .stdSPSId = kParameterSetId,
    .stdPPSId = kParameterSetId,
  };
  VkVideoEncodeSessionParametersGetInfoKHR const get{
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_GET_INFO_KHR,
    .pNext = &h264_get,
    .videoSessionParameters = owned.parameters_,
  };
  bool retrieved = false;
  for (std::uint32_t attempt = 0; attempt < kMaxQueryAttempts; ++attempt) {
    std::size_t size = 0;
    auto result = owned.context_.disp.getEncodedVideoSessionParametersKHR(&get, nullptr, &size, nullptr);
    if (result == VK_INCOMPLETE) { continue; }
    if (result != VK_SUCCESS) { return std::unexpected(Failure("query encoded SPS/PPS size", result)); }
    if (size == 0 || size > kMaxParameterSetBytes) {
      return std::unexpected(MakeError(std::errc::value_too_large, "invalid encoded SPS/PPS size"));
    }
    owned.parameter_sets_.resize(size);
    result = owned.context_.disp.getEncodedVideoSessionParametersKHR(&get, nullptr, &size, owned.parameter_sets_.data());
    if (result == VK_INCOMPLETE || size > owned.parameter_sets_.size()) { continue; }
    if (result != VK_SUCCESS) { return std::unexpected(Failure("retrieve encoded SPS/PPS", result)); }
    if (size == 0) {
      return std::unexpected(MakeError(std::errc::io_error, "empty encoded SPS/PPS"));
    }
    owned.parameter_sets_.resize(size);
    retrieved = true;
    break;
  }
  if (!retrieved) {
    return std::unexpected(MakeError(std::errc::io_error, "encoded SPS/PPS retrieval remained incomplete"));
  }
  return {};
}

// This function assembles the Vulkan session and its owned memory and parameter sets.
auto VideoSession::Create(Context &context, EncodeDevice const &device, VkExtent2D extent,
  std::uint32_t fps_numerator, std::uint32_t fps_denominator, bool fixed_frame_rate)
  -> std::expected<std::unique_ptr<VideoSession>, Error>
{
  if (auto valid = ValidateSessionConfiguration(device, extent, fps_numerator, fps_denominator); !valid) {
    return std::unexpected(valid.error());
  }
  auto owned = std::unique_ptr<VideoSession>{ new VideoSession{ context } };
  if (auto created = CreateHandle(*owned, device, extent); !created) { return std::unexpected(created.error()); }
  if (auto bound = BindMemory(*owned); !bound) { return std::unexpected(bound.error()); }
  auto const &selection = device.selection;
  auto const &profile = device.capabilities.profiles.at(selection.profile_index);
  // PR4 uses the device's maximum level until frame-rate-aware level selection is introduced.
  auto parameters = CreateParameters(context, owned->session_, selection.profile,
    profile.h264.maxLevelIdc, extent, fps_numerator, fps_denominator, fixed_frame_rate);
  if (!parameters) { return std::unexpected(parameters.error()); }
  owned->parameters_ = *parameters;

  if (auto retrieved = RetrieveParameterSets(*owned); !retrieved) { return std::unexpected(retrieved.error()); }
  return owned;
}

VideoSession::~VideoSession()
{
  if (parameters_ != VK_NULL_HANDLE) { context_.disp.destroyVideoSessionParametersKHR(parameters_, nullptr); }
  if (session_ != VK_NULL_HANDLE) { context_.disp.destroyVideoSessionKHR(session_, nullptr); }
  for (auto *memory : memory_) { context_.disp.freeMemory(memory, nullptr); }
}

}// namespace vkgsplat::vulkan::video
