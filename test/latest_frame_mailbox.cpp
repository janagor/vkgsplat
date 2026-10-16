#include <vkgsplat_adt/latest_frame_mailbox.hpp>

#include <catch2/catch_test_macros.hpp>

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <thread>

namespace {

constexpr int kSnapshotCount = 1000;

[[nodiscard]] auto VerifyLatestFrameReplacement() -> bool
{
  vkgsplat::adt::LatestFrameMailbox<int, 2U> mailbox;
  mailbox.Reset();

  auto first_slot = mailbox.PrepareSlot();
  if (!first_slot.has_value()) { return false; }
  mailbox.Publish(first_slot.value(), 0);
  auto worker_frame = mailbox.Consume();
  if (!worker_frame.has_value()) { return false; }

  for (int snapshot = 1; snapshot <= kSnapshotCount; ++snapshot) {
    auto slot = mailbox.PrepareSlot();
    if (!slot.has_value() || slot.value() == worker_frame.value().slot) { return false; }
    mailbox.Publish(slot.value(), snapshot);
  }

  auto latest = mailbox.Consume();
  if (!latest.has_value() || latest.value().value != kSnapshotCount || mailbox.PrepareSlot().has_value()) {
    return false;
  }

  mailbox.Complete(worker_frame.value().slot);
  if (!mailbox.PrepareSlot().has_value()) { return false; }
  mailbox.Complete(latest.value().slot);
  return mailbox.Empty();
}

[[nodiscard]] auto VerifyConcurrentReplacement() -> bool
{
  vkgsplat::adt::LatestFrameMailbox<int, 2U> mailbox;
  mailbox.Reset();
  auto first_slot = mailbox.PrepareSlot();
  if (!first_slot.has_value()) { return false; }
  mailbox.Publish(first_slot.value(), 0);
  std::mutex mutex;
  std::condition_variable condition;
  bool worker_started = false;
  bool producer_done = false;
  bool slots_distinct = false;
  bool published_all = true;
  int latest_value = 0;

  std::jthread worker{ [&]() -> void {
    std::unique_lock lock{ mutex };
    auto first = mailbox.Consume();
    worker_started = true;
    condition.notify_one();
    condition.wait(lock, [&]() -> bool { return producer_done; });
    auto latest = mailbox.Consume();
    if (first.has_value() && latest.has_value()) {
      slots_distinct = first.value().slot != latest.value().slot;
      latest_value = latest.value().value;
      mailbox.Complete(first.value().slot);
      mailbox.Complete(latest.value().slot);
    }
  } };

  {
    std::unique_lock lock{ mutex };
    condition.wait(lock, [&]() -> bool { return worker_started; });
    for (int snapshot = 1; snapshot <= kSnapshotCount; ++snapshot) {
      auto slot = mailbox.PrepareSlot();
      if (!slot.has_value()) {
        published_all = false;
        break;
      }
      mailbox.Publish(slot.value(), snapshot);
    }
    producer_done = true;
  }
  condition.notify_one();
  worker.join();
  return published_all && slots_distinct && latest_value == kSnapshotCount && mailbox.Empty();
}

struct InterleavedState
{
  vkgsplat::adt::LatestFrameMailbox<int, 2U> mailbox;
  std::mutex mutex;
  std::condition_variable condition;
  std::optional<size_t> worker_slot;
  bool producer_done = false;
  bool ownership_safe = true;
  int latest_value = 0;
};

auto ConsumeInterleaved(InterleavedState &state) -> void
{
  for (;;) {
    std::unique_lock lock{ state.mutex };
    state.condition.wait(lock, [&]() -> bool { return state.producer_done || !state.mailbox.Empty(); });
    if (state.producer_done && state.mailbox.Empty()) { return; }
    auto frame = state.mailbox.Consume();
    if (!frame.has_value()) { continue; }
    state.worker_slot = frame.value().slot;
    lock.unlock();
    std::this_thread::yield();
    lock.lock();
    state.latest_value = frame.value().value;
    state.mailbox.Complete(frame.value().slot);
    state.worker_slot.reset();
    state.condition.notify_one();
  }
}

[[nodiscard]] auto VerifyInterleavedOwnership() -> bool
{
  InterleavedState state;
  state.mailbox.Reset();
  std::jthread worker{ [&]() -> void { ConsumeInterleaved(state); } };
  for (int snapshot = 1; snapshot <= kSnapshotCount; ++snapshot) {
    {
      std::scoped_lock const lock{ state.mutex };
      auto slot = state.mailbox.PrepareSlot();
      if (!slot.has_value() || (state.worker_slot.has_value() && slot.value() == state.worker_slot.value())) {
        state.ownership_safe = false;
        break;
      }
      state.mailbox.Publish(slot.value(), snapshot);
    }
    state.condition.notify_one();
    std::this_thread::yield();
  }
  {
    std::scoped_lock const lock{ state.mutex };
    state.producer_done = true;
  }
  state.condition.notify_one();
  worker.join();
  return state.ownership_safe && state.latest_value == kSnapshotCount && state.mailbox.Empty();
}

}// namespace

TEST_CASE("latest pending frame replaces stale camera state", "[render]")
{ REQUIRE(VerifyLatestFrameReplacement()); }

TEST_CASE("worker-owned frame survives concurrent replacement", "[render]")
{ REQUIRE(VerifyConcurrentReplacement()); }

TEST_CASE("producer and worker interleave slot ownership", "[render]")
{ REQUIRE(VerifyInterleavedOwnership()); }
