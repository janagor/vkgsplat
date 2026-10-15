# vkexec migration baseline

This document records the starting point for the vkexec migration on `stdexec`.
It separates repository configuration from results collected on one machine.
Unavailable results are marked explicitly; they must not be treated as successful
build, render, or validation evidence.

## Source revisions

| Item | Revision | Meaning |
| --- | --- | --- |
| vkgsplat baseline | `d371e36d5e4c871b0195c1eb34ea45f0d7275c61` | `stdexec` HEAD used for this baseline |
| vkexec in baseline | `73176a3e6a3df3ec45f42bb0a955e8317517de25` | Immutable source pin in `Dependencies.cmake` |
| vkexec migration target | `fa5e8e59923d2a1ba45747bf7d0ffca83dc414cd` | `vkexec/main` resolved with `git ls-remote` on 2026-10-15 |

The target SHA is recorded for comparison only. `Dependencies.cmake` remains
pinned to the baseline vkexec SHA. Source pins do not establish which versions
were used by an existing binary; the machine build details below come from the
existing Release CMake cache and its fetched dependency checkouts.

## Reconstructing the configured build

The project provides `unixlike-clang-debug` and `unixlike-clang-release` CMake
presets. Both use Ninja and `LLD`; Debug uses `CMAKE_BUILD_TYPE=Debug`, while
Release uses `RelWithDebInfo`. The project presets do not set additional
project-specific options for these Clang configurations. For a clean baseline,
use fresh build directories and keep the dependency pin unchanged:

```sh
cmake --preset unixlike-clang-debug
cmake --build out/build/unixlike-clang-debug --parallel
ctest --test-dir out/build/unixlike-clang-debug --output-on-failure

cmake --preset unixlike-clang-release
cmake --build out/build/unixlike-clang-release --parallel
```

These configure/build commands were not run for this record because compiling
was explicitly excluded. The existing Release build directory is not clean and
is not a substitute for those commands. CTest was run from that existing
directory; its result is recorded below.

Dependencies are configured in `Dependencies.cmake`. Notable baseline entries
include Vulkan-Headers `v1.4.352`, vk-bootstrap `v1.4.352`, VulkanMemoryAllocator
`v3.4.0`, GLFW `3.4`, GLM `1.0.3`, magic_enum `v0.9.8`, fmt `12.1.0`, spdlog
`1.17.0`, Catch2 `3.12.0`, CLI11 `2.6.1`, and the pinned vkexec SHA above. The
ImGui fork is the `VK_EXT_descriptor_heap` branch. These source declarations
are separate from the installed Vulkan loader and headers actually found by a
machine build.

## Machine-specific build and runtime record

Collected on 2026-10-15 (Asia/Seoul):

| Component | Observed value |
| --- | --- |
| OS/kernel | NixOS Linux, `x86_64`; kernel `6.18.52` |
| CMake | `4.3.4` |
| Clang | `22.1.8`, target `x86_64-unknown-linux-gnu` |
| Generator | Ninja |
| Existing build type | `RelWithDebInfo` in `out/build/unixlike-clang-release/CMakeCache.txt` |
| Existing compiler | `clang` / `clang++` in that cache |
| Vulkan loader | `1.4.350.0` from the Nix store |
| Vulkan headers | CPM Vulkan-Headers `1.4.352`; cached system header package `1.4.350.0` |
| Vulkan SDK | No complete SDK version was identified; the cache records the loader, headers, and `glslangValidator` separately |
| Mesa userspace | `26.2.0` paths appeared in Vulkan loader diagnostics |
| Physical GPU / driver | Not identified; physical-device enumeration failed |
| Display | No usable X server (`XCB failed to connect to the X server`) |

The existing Release cache fetched vkexec at
`73176a3e6a3df3ec45f42bb0a955e8317517de25`, matching the source pin. It also
records Vulkan-Headers `v1.4.352`, VulkanMemoryAllocator `v3.4.0`, and
vk-bootstrap `v1.4.352`. The machine did not provide a physical-device or
driver version to record. `vulkaninfo --summary` emitted duplicate-layer
warnings, skipped an unavailable Direct3D 12 ICD, reported failed device
enumeration for Mesa ICDs, and failed when creating its XCB surface.

The application requires Vulkan 1.4 and `VK_EXT_descriptor_heap`,
`VK_KHR_buffer_device_address`, and `VK_KHR_shader_untyped_pointers`, plus
descriptor heap, shader untyped pointer, buffer device address, timeline
semaphore, dynamic rendering, shader demote-to-helper, and maintenance4
features. Since no physical device was enumerated, support for these
requirements is unknown on this machine.

## Build and CTest evidence

No clean Debug or Release build was performed. Existing compiler warnings and
build failures therefore remain unverified for this record.

The following command was run against the pre-existing Release build directory
only:

```sh
ctest --test-dir out/build/unixlike-clang-release --output-on-failure
```

Result: 2 of 3 tests passed. The `fuzz_tester_run` test completed its fuzz run,
then failed with LeakSanitizer's fatal message that LeakSanitizer does not work
under ptrace. This is an observed failure of that existing test invocation; it
has not been established as a source regression or as a clean-build result.
Debug CTest was not run because no Debug build directory was available.

## Rendering fixture inputs

The fixture configs are in [`test/fixtures/rendering/`](../../test/fixtures/rendering/README.md).
They point at `resources/scene.ply`, which is a machine-local, untracked asset
in this checkout. Its SHA-256 is recorded in `test/fixtures/rendering/scene.sha256`.
No verified redistribution or download URL was available, so the scene is not
copied into this PR. A developer must obtain the exact asset and verify its
checksum before using these configs elsewhere.

The configs use the existing example camera values as initial settings. They
have not been visually validated against the scene and must not be treated as
approved reference camera poses. The current app creates a 1024 x 1024 window;
the quilt color target scales this to a 2048 x 2048 atlas for a 2 x 2 grid.
ImGui is disabled in both configs. Reference PNGs were not captured because no
usable Vulkan display/device was available. Their PNG checksums and visual
comparison are therefore pending.

## GPU timing and validation

No GPU timing samples were collected. The device could not be enumerated, and
the app's current timer output is surfaced through ImGui rather than a robust
automated benchmark runner. [`results/gpu-baseline.csv`](results/gpu-baseline.csv)
contains the requested schema and no fabricated measurements. Once a supported
GPU is available, use the Release preset, disable validation, warm up the
pipelines, record multiple samples per pass for mono and 2 x 2 quilt, and
report median and p95 along with device, driver, resolution, Gaussian count,
and settings. Until a repeatable sample procedure is available, label a small
manual capture preliminary.

Runtime validation is blocked. Neither mono nor quilt rendering, screenshot
capture, shutdown validation, or swapchain recreation could be exercised.
There is no evidence here of validation success or of the required device
capabilities.

## Migration-sensitive integration points

The following existing call sites should be compared during PRs 2–4:

- `src/vkgsplat_backend/vulkan/vulkan_driver.cpp`: queue selection and
  `vkexec::context::adopt`, including graphics, compute, and present queue
  ownership.
- `src/vkgsplat_backend/vulkan/vkexec_requirements.cpp` and
  `device.cpp`: Vulkan version, extension, and feature negotiation.
- `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp`: descriptor
  heap layouts, table lowering, heap binding, and the direct internal API use
  `vkexec::detail::heap_descriptor_backend::lower`.
- `src/vkgsplat_backend/vulkan/gs/load_heap_pipeline.hpp`,
  `graphics_pipeline.cpp`, and `gs/pipeline.cpp`: compute and graphics pipeline
  creation and descriptor-heap layouts.
- `src/vkgsplat_backend/vulkan/render_thread.cpp`, `frame_context.cpp`, and
  `gs/operations.cpp`: frame-ring submission, command recording, synchronization,
  barriers, and timestamp queries.
- `src/vkgsplat_backend/vulkan/gpu_buffers.hpp`, `mesh_gpu.cpp`, and
  `app_state.hpp`: vkexec buffer, image, tensor, frame-ring, and resource
  ownership lifetimes.
- `src/vkgsplat_backend/vulkan/imgui_overlay.cpp`: descriptor heap access from
  the ImGui integration, including descriptor writes and secondary command
  buffer recording.

An inventory of the direct `vkexec::detail::` use found one call site:
`descriptor_heap.cpp` uses `heap_descriptor_backend::lower`. The renderer also
depends on vkexec APIs for context adoption, resource creation, image barriers,
compute algorithms, graphics pipelines, heap resource binding, frame submission,
and frame-ring handling. No integration code or rendering behavior was changed
to establish this record.
