#include "benchmark/fixture/profile.hpp"

namespace vkgsplat::benchmark {

auto BaselineProfile() -> BenchmarkProfile { return {}; }

auto IsCanonical(BenchmarkSceneSpec const &spec) -> bool
{
  return spec.gaussian_count == kBaselineCount && spec.seed == kBaselineSeed
         && spec.generator_version == kGeneratorVersion;
}

}// namespace vkgsplat::benchmark
