# vkgsplat

[![ci](https://github.com/janagor/vkgsplat/actions/workflows/ci.yml/badge.svg)](https://github.com/janagor/vkgsplat/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/janagor/vkgsplat/branch/main/graph/badge.svg)](https://codecov.io/gh/janagor/vkgsplat)
[![CodeQL](https://github.com/janagor/vkgsplat/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/janagor/vkgsplat/actions/workflows/codeql-analysis.yml)

## About vkgsplat
Vulkan-based rendering engine for 3D Gaussian Splatting.

## WebAssembly Demo

Try the live WebAssembly demo:
- Main: [https://janagor.github.io/vkgsplat/](https://janagor.github.io/vkgsplat/)
- Develop: [https://janagor.github.io/vkgsplat/develop/](https://janagor.github.io/vkgsplat/develop/)

The `main` branch deploys to the root, `develop` to `/develop/`, and tags to `/tagname/`.


## More Details

 * [Dependency Setup](README_dependencies.md)
 * [Building Details](README_building.md)
 * [Troubleshooting](README_troubleshooting.md)
 * [Docker](README_docker.md)
 * [vkexec migration baseline](docs/migration/vkexec-baseline.md)

## Screenshot API

Include `<vkgsplat_capture/screenshot.hpp>` and link `vkgsplat::vkgsplat_capture`.
To save the latest completed scene frame, use `vkgsplat::capture::save_png(renderer, "scene.png")`.
This replaces `renderer.save_frame_png("scene.png")`.
For a frame returned by `renderer.render_frame(camera)`, use
`vkgsplat::capture::write_png(*frame, "scene.png")` after checking the result.
`render_frame()` renders the supplied camera in offscreen mode;
`capture_latest_frame()` reads an already completed frame.
