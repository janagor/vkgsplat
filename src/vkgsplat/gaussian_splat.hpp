#pragma once

#include <array>

#include <vkgsplat/types.hpp>

namespace vkgsplat {


// :q
class MockGaussianSplat
{
public:
  std::array<f32, 3> position{ 0.0F, 0.0F, 0.0F };
  std::array<f32, 3> color{ 1.0F, 1.0F, 1.0F };
  std::array<f32, 4> rotation{ 0.0F, 0.0F, 0.0F, 1.0F };
};

}// namespace vkgsplat
