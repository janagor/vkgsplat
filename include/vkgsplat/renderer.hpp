#pragma once

#include <expected>
#include <memory>
#include <string>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/vkgsplat_export.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

class Window;

struct RendererConfig
{
  u32 splat_count = 64;
  std::string ply_path;
  bool enable_validation = false;
};

class VKGSPLAT_EXPORT Renderer
{
public:
  [[nodiscard]] static auto create(RendererConfig const &config, Window &window) -> std::expected<Renderer, Error>;

  Renderer(Renderer &&) noexcept;
  auto operator=(Renderer &&) noexcept -> Renderer &;
  ~Renderer();

  Renderer(Renderer const &) = delete;
  auto operator=(Renderer const &) -> Renderer & = delete;

  [[nodiscard]] auto draw(Camera const &camera) -> std::expected<void, Error>;
  void wait_idle() const;

private:
  struct Impl;
  explicit Renderer(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}// namespace vkgsplat
