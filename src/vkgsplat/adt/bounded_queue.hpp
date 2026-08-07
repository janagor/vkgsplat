#ifndef VKGSPLAT_ADT_BOUNDED_QUEUE_HPP
#define VKGSPLAT_ADT_BOUNDED_QUEUE_HPP

#include <array>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

namespace vkgsplat::adt {

// Fixed-capacity FIFO queue for producer/consumer pipelines (thread-safe).
template<typename T, size_t Capacity>
  requires(Capacity > 0U)
class BoundedQueue
{
public:
  static constexpr size_t kCapacity = Capacity;

  BoundedQueue() = default;
  ~BoundedQueue() = default;

  BoundedQueue(BoundedQueue const &) = delete;
  auto operator=(BoundedQueue const &) -> BoundedQueue & = delete;
  BoundedQueue(BoundedQueue &&) = delete;
  auto operator=(BoundedQueue &&) -> BoundedQueue & = delete;

  [[nodiscard]] auto Empty() const -> bool
  {
    std::scoped_lock const lock{ mutex_ };
    return count_ == 0U;
  }

  [[nodiscard]] auto Full() const -> bool
  {
    std::scoped_lock const lock{ mutex_ };
    return count_ >= kCapacity;
  }

  [[nodiscard]] auto Size() const -> size_t
  {
    std::scoped_lock const lock{ mutex_ };
    return count_;
  }

  void WaitNotFull() const
  {
    std::unique_lock lock{ mutex_ };
    not_full_.wait(lock, [this]() -> bool { return count_ < kCapacity; });
  }

  void WaitEmpty() const
  {
    std::unique_lock lock{ mutex_ };
    not_empty_.wait(lock, [this]() -> bool { return count_ == 0U; });
  }

  void Push(T value)
  {
    std::unique_lock lock{ mutex_ };
    not_full_.wait(lock, [this]() -> bool { return count_ < kCapacity; });
    PushUnlocked(std::move(value));
    not_empty_.notify_one();
  }

  [[nodiscard]] auto TryPush(T value) -> bool
  {
    std::scoped_lock const lock{ mutex_ };
    if (count_ >= kCapacity) { return false; }
    PushUnlocked(std::move(value));
    not_empty_.notify_one();
    return true;
  }

  auto Pop() -> T
  {
    std::unique_lock lock{ mutex_ };
    not_empty_.wait(lock, [this]() -> bool { return count_ > 0U; });
    T value = PopUnlocked();
    not_full_.notify_one();
    return value;
  }

  [[nodiscard]] auto TryPop(T &out) -> bool
  {
    std::scoped_lock const lock{ mutex_ };
    if (count_ == 0U) { return false; }
    out = PopUnlocked();
    not_full_.notify_one();
    return true;
  }

  [[nodiscard]] auto TryPop() -> std::optional<T>
  {
    T value{};
    if (!TryPop(value)) { return std::nullopt; }
    return value;
  }

  void Clear()
  {
    std::scoped_lock const lock{ mutex_ };
    read_ = 0U;
    write_ = 0U;
    count_ = 0U;
    not_full_.notify_all();
  }

private:
  [[nodiscard]] static constexpr auto NextIndex(size_t index) noexcept -> size_t { return (index + 1U) % kCapacity; }

  void PushUnlocked(T value)
  {
    ring_.at(write_) = std::move(value);
    write_ = NextIndex(write_);
    ++count_;
  }

  auto PopUnlocked() -> T
  {
    T value = std::move(ring_.at(read_));
    read_ = NextIndex(read_);
    --count_;
    return value;
  }

  mutable std::mutex mutex_;
  mutable std::condition_variable not_empty_;
  mutable std::condition_variable not_full_;

  std::array<T, Capacity> ring_{};
  size_t read_{ 0 };
  size_t write_{ 0 };
  size_t count_{ 0 };
};

}// namespace vkgsplat::adt

#endif// VKGSPLAT_ADT_BOUNDED_QUEUE_HPP
