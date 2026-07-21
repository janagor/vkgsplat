#pragma once

#include <expected>
#include <memory>
#include <string>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/error.hpp>
#include <vkgsplat/types.hpp>
#include <vkgsplat/vkgsplat_export.hpp>

namespace vkgsplat {

enum class SplatSource : u8 {
  Procedural,
  Ply,
};

struct RendererConfig
{
  SplatSource source = SplatSource::Procedural;
  u32 splat_count = 64;
  std::string ply_path;
};

class VKGSPLAT_EXPORT Renderer
{
public:
  [[nodiscard]] static auto create(RendererConfig const &config) -> std::expected<Renderer, Error>;

  Renderer(Renderer &&) noexcept;
  auto operator=(Renderer &&) noexcept -> Renderer &;
  ~Renderer();

  Renderer(Renderer const &) = delete;
  auto operator=(Renderer const &) -> Renderer & = delete;

  void poll_events() const;
  [[nodiscard]] auto should_close() const -> bool;
  [[nodiscard]] auto draw(Camera const &camera) -> std::expected<void, Error>;
  void wait_idle() const;

private:
  struct Impl;
  explicit Renderer(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}// namespace vkgsplat
