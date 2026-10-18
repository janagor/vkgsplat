#ifndef VKGSPLAT_BENCHMARK_BATCH_ACCOUNTING_HPP
#define VKGSPLAT_BENCHMARK_BATCH_ACCOUNTING_HPP

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <expected>
#include <system_error>

namespace vkgsplat::benchmark {

[[nodiscard]] inline auto ValidateBatchAccounting(u64 requested,
  u64 completed_before,
  u64 completed_after,
  u64 recreated_before,
  u64 recreated_after) -> std::expected<void, Error>
{
  if (completed_after < completed_before || completed_after - completed_before != requested
      || recreated_after != recreated_before) {
    return std::unexpected{ MakeError(
      std::errc::state_not_recoverable, "benchmark batch contained a skipped, replaced, or recreated frame") };
  }
  return {};
}

}// namespace vkgsplat::benchmark

#endif// VKGSPLAT_BENCHMARK_BATCH_ACCOUNTING_HPP
