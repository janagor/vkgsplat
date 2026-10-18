# Headless rendering

`vkgsplat_headless` renders one PLY scene frame through the normal Vulkan renderer and writes its color target to a PNG. It creates a `VK_EXT_headless_surface` and a presentable swapchain, so the Vulkan implementation must support that extension, a present-capable queue, the renderer's required device features, and a compatible swapchain. No display server or GLFW window is needed. The existing renderer still links GLFW, but headless execution does not initialize it.

## Build

Configure and build a native preset as described in [README_building.md](../README_building.md):

```sh
cmake --preset unixlike-clang-release
cmake --build out/build/unixlike-clang-release --target vkgsplat_headless
```

## Render

```sh
env -u DISPLAY -u WAYLAND_DISPLAY \
  out/build/unixlike-clang-release/src/vkgsplat_headless_app/vkgsplat_headless \
    --ply /path/to/scene.ply \
    --width 1280 --height 720 \
    --output /tmp/headless.png \
    --validation
```

`--ply` and `--output` are required. Width and height default to 1920 and 1080. The camera uses the renderer's default fixed look-at pose unless `--camera-position X Y Z` and `--camera-target X Y Z` are specified. Set these for the chosen scene so the saved frame contains visible splats. The program exits with an error if the requested frame is not completed or the PNG cannot be saved.

If `VK_EXT_headless_surface` is missing, startup reports that extension explicitly. A device without the renderer's required Vulkan features or a compatible presentable swapchain also fails. This mode does not provide swapchain-independent offscreen rendering. Uncapped frame-rate configuration avoids optional present-timing requests, but FIFO swapchain presentation can still pace frames.

## Tests

The `vkgsplat_headless_platform_test` Catch2 tests run without a GPU or display. A compatible GPU and PLY fixture are required for the opt-in integration test:

```sh
cmake --preset unixlike-clang-release \
  -Dvkgsplat_ENABLE_HEADLESS_GPU_TESTS=ON \
  -Dvkgsplat_HEADLESS_TEST_PLY=/path/to/scene.ply
cmake --build out/build/unixlike-clang-release --target vkgsplat_headless
ctest --test-dir out/build/unixlike-clang-release -R headless.gpu.smoke --output-on-failure
```

The GPU test verifies that a frame completes and the PNG has the requested dimensions. It removes any previous output before rendering. Validation layers are enabled by default for this test; set `-Dvkgsplat_HEADLESS_GPU_TEST_VALIDATION=OFF` if they are unavailable. Inspect the PNG for scene content using a camera pose that frames the fixture, and inspect validation output for Vulkan errors. The GPU test is disabled by default.
