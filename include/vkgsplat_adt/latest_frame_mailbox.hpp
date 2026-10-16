#ifndef VKGSPLAT_ADT_LATEST_FRAME_MAILBOX_HPP
#define VKGSPLAT_ADT_LATEST_FRAME_MAILBOX_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace vkgsplat::adt {

// Caller synchronizes access. A pending value can be replaced until consumed.
template<typename T, size_t Capacity>
  requires(Capacity > 1U)
class LatestFrameMailbox
{
public:
  struct Frame
  {
    size_t slot{};
    T value{};
  };

  auto Reset() -> void
  {
    pending_.reset();
    owners_.fill(Owner::kFree);
  }

  [[nodiscard]] auto PrepareSlot() const -> std::optional<size_t>
  {
    if (pending_.has_value()) { return pending_.value().slot; }
    for (size_t slot = 0; slot < Capacity; ++slot) {
      if (owners_.at(slot) == Owner::kFree) { return slot; }
    }
    return std::nullopt;
  }

  auto Publish(size_t slot, T value) -> void
  {
    owners_.at(slot) = Owner::kPending;
    pending_ = Frame{ .slot = slot, .value = std::move(value) };
  }

  [[nodiscard]] auto Consume() -> std::optional<Frame>
  {
    if (!pending_.has_value()) { return std::nullopt; }
    auto frame = std::exchange(pending_, std::nullopt);
    if (!frame.has_value()) { return std::nullopt; }
    owners_.at(frame.value().slot) = Owner::kWorker;
    return frame;
  }

  auto Complete(size_t slot) -> void { owners_.at(slot) = Owner::kFree; }

  auto DiscardPending() -> void
  {
    if (!pending_.has_value()) { return; }
    owners_.at(pending_.value().slot) = Owner::kFree;
    pending_.reset();
  }

  [[nodiscard]] auto Empty() const noexcept -> bool { return !pending_.has_value(); }

private:
  enum class Owner : std::uint8_t { kFree, kPending, kWorker };
  std::array<Owner, Capacity> owners_{};
  std::optional<Frame> pending_;
};

}// namespace vkgsplat::adt

#endif// VKGSPLAT_ADT_LATEST_FRAME_MAILBOX_HPP
