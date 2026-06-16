#include <vkgsplat/sample_library.hpp>

int factorial(int input) noexcept //NOLINT
{
  int result = 1;

  while (input > 0) {
    result *= input;
    --input;
  }

  return result;
}
