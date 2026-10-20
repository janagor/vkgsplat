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

## Vulkan Video initialization

`EngineConfig::video_encode` (or `DriverConfig::video_encode`) controls optional H.264
capability setup. The default, `kDisabled`, keeps the existing device initialization.
`kOptional` probes the rendering device already selected by Vulkan and enables
encoding when it supports an 8-bit 4:2:0 H.264 profile, an encode queue, and
an encode-source image format. A zero-DPB all-intra session does not need a DPB
format. `kRequired` searches rendering-capable devices
for those requirements and returns an initialization error when none qualify.

After creation, `engine.video_encode_capabilities()` (or the driver equivalent)
reports whether the logical device has H.264 enabled, whether the known RGBA
conversion path and reference pictures are supported, its usable coded extent,
and a diagnostic string. This only reports initialization capability; encoding
and video output are not implemented yet. The Vulkan backend retains the complete
profile, format, limit, and queue data for the future encoder.
`capture_conversion_supported` reports compatible image usages, not a completed
color-conversion pipeline.
The public maximum extent and both support flags describe one selected profile
and encode-source format; the internal report retains alternatives for later
encoder configurations.

## Screenshot API

Include `<vkgsplat_capture/screenshot.hpp>` and link `vkgsplat::vkgsplat_capture`.
To save the latest completed scene frame, use `vkgsplat::capture::save_png(renderer, "scene.png")`.
This replaces `renderer.save_frame_png("scene.png")`.
For a frame returned by `renderer.render_frame(camera)`, use
`vkgsplat::capture::write_png(*frame, "scene.png")` after checking the result.
`render_frame()` renders the supplied camera in offscreen mode;
`capture_latest_frame()` reads an already completed frame.

## PNG sequence capture

Include `<vkgsplat_capture/frame_sequence.hpp>` and link `vkgsplat::vkgsplat_capture`.
Create a `capture::FrameSequence` with an offscreen `Renderer`, call `record_frame(camera)`
for each requested camera, then call `finish()`. The renderer must outlive the sequence.
The writer accepts owned RGBA frames into a bounded queue and blocks when full.
`max_pending_frames` counts queued frames; one more frame can be in the writer during encoding.
With the default capacity, a full queue can hold three frames, the writer can hold one,
and a blocked producer can hold another. At 4K RGBA these five CPU frames use about
158 MiB, excluding renderer buffers and other allocations. The queue limit bounds pending
work, not total capture memory. A successful `record_frame()` means the frame was accepted;
only a successful `finish()` confirms that every PNG was written. The output directory must
be empty and dedicated to one session at a time. Files start at `frame_000000.png` by
default and are numbered consecutively. `start_index` changes file names only. Presentation
timestamps are determined by the zero-based sequence index and rational frame rate:
`index * denominator / numerator` seconds. PNG files do not carry these timestamps.
The output filesystem must support hard links for atomic publication without overwriting
existing frames.

For a circular orbit from the headless app:

```sh
vkgsplat_headless --ply scene.ply --target offscreen --sequence-dir output/sequence \
  --width 1920 --height 1080 --frames 120 --fps 60 --orbit-degrees 360
```

The `--output` option continues to save one screenshot and cannot be combined with
`--sequence-dir`. Sequence mode uses an offscreen target. Orbit positions are calculated
from each frame index, so capture speed does not affect the requested camera state.
