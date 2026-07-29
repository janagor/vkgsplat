#include <cstddef>
#include <cstdint>
#include <fmt/base.h>
#include <iterator>

[[nodiscard]] auto SumValues(const uint8_t *data, size_t size) -> int
{
  constexpr auto kScale = 1000;

  int value = 0;
  for (std::size_t offset = 0; offset < size; ++offset) {
    value += static_cast<int>(*std::next(data, static_cast<int64_t>(offset))) * kScale;
  }
  return value;
}

// Fuzzer that attempts to invoke undefined behavior for signed integer overflow
// cppcheck-suppress unusedFunction symbolName=LLVMFuzzerTestOneInput
extern "C" auto LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) -> int
{
  fmt::print("Value sum: {}, len{}\n", SumValues(data, size), size);
  return 0;
}
