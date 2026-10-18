# Rendering throughput benchmark

`vkgsplat_bench` measures sustained end-to-end headless swapchain rendering throughput. A successful batch accepts its requested number of lossless frames, waits for GPU idle once, and verifies that exactly that many frames completed without swapchain recreation. The interactive renderer still uses replaceable latest-frame submission by default.

The build generates the pinned `baseline-v1` fixture automatically. It contains 262,144 Gaussians from seed `0x5EED2026`, with FNV-1a 64 checksum `ad124868b0d228b3`. The fixture manifest pins the fixed camera, 45° field of view, 1920 × 1080 resolution, generator version, and scene checksum. The benchmark verifies the manifest and file before creating the renderer. Its fixture path is embedded at configure time, so the executable works from any current directory.

Build on a machine with Vulkan headless surface support:

```sh
cmake -S . -B out/build/benchmark -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -Dvkgsplat_BUILD_BENCHMARKS=ON \
  -Dvkgsplat_ENABLE_SANITIZER_ADDRESS=OFF \
  -Dvkgsplat_ENABLE_SANITIZER_UNDEFINED=OFF \
  -Dvkgsplat_ENABLE_CLANG_TIDY=OFF \
  -Dvkgsplat_ENABLE_CPPCHECK=OFF
cmake --build out/build/benchmark --target vkgsplat_bench -j
```

Run repeated measurements and write Google Benchmark JSON:

```sh
./out/build/benchmark/benchmark/vkgsplat_bench \
  --benchmark_filter='BM_RenderThroughput/.*/batch:64' \
  --benchmark_min_time=5s \
  --benchmark_repetitions=5 \
  --benchmark_out=results.json \
  --benchmark_out_format=json
```

Each Google Benchmark iteration is a frame batch. The registered `batch` arguments are 1, 2, 4, 8, 16, 32, 64, and 128 frames; 64 is the canonical baseline. Use `--benchmark_filter='batch:64'` to select it. `real_time` is wall-clock time per batch. `fps` and `items_per_second` count rendered frames per second. Each independent measurement invocation performs 64 warmup frames outside the timed loop. Scene loading, fixture verification, and renderer creation also happen before measurement. No per-frame device-idle wait is used; the device-idle wait at the end of each batch is part of the measured time.

The JSON context records the GPU name, Vulkan vendor/device IDs, raw driver and API versions, Git commit and dirty-working-tree status at configure time, build type, IPO setting, fixture checksum and seed, resolution, and render settings. The benchmark name records its batch argument. Git metadata is a configure-time snapshot: **reconfigure after any source change**, including uncommitted edits, before rebuilding and recording results. Compare results only with the same fixture, resolution, batch size, build settings, and comparable hardware. Compare the smaller batches with 64 frames to see where the batch-end idle wait stops affecting throughput.

This FPS includes CPU command recording, GPU rendering, synchronization, and headless presentation. It is not a GPU pass time or a GPU-only kernel throughput result. A skipped or recreated frame, render error, or failed fixture verification invalidates the run.

The CPU-only tests cover mailbox replacement and ownership, plus the production rendering thread's concurrent producers, error propagation, shutdown wakeup, and idle wait. The opt-in `benchmark.fixture.gpu.smoke` test checks a full headless batch with validation disabled, matching benchmark settings. Before accepting a performance baseline, separately run the validation-enabled `headless.gpu.smoke` test with the generated `benchmark/baseline-v1.ply` as `vkgsplat_HEADLESS_TEST_PLY`; see [headless rendering](../docs/headless.md) for its CMake options. GPU tests require compatible Vulkan hardware and validation layers.

## GPU pass timings

`BM_GpuPassTimings` uses Vulkan timestamps on the graphics queue. It creates a separate renderer with GPU timers and sample collection enabled, runs 64 warmup frames, waits for idle, and discards those warmup samples. Each measured iteration submits a lossless batch of 1, 2, 4, 8, 16, 32, 64, or 128 frames. After `wait_idle()`, it checks that every completed frame has one unique sample. The benchmark's manual time is the sum of GPU frame spans in that batch. Renderer setup, teardown, CPU recording, presentation, and batch idle time are outside this manual time.

```sh
./out/build/benchmark/benchmark/vkgsplat_bench \
  --benchmark_filter='BM_GpuPassTimings/.*/batch:64' \
  --benchmark_min_time=5s \
  --benchmark_repetitions=5 \
  --benchmark_out=gpu_results.json \
  --benchmark_out_format=json
```

The JSON result has `gpu.frame.*` and `gpu.<pass>.*` counters in milliseconds, plus `gpu.samples`. It does not report `items_per_second`: manual GPU time is the sum of frame spans, so a rate based on it would not represent application FPS. Each applicable pass reports mean, median, p95, population standard deviation, minimum, maximum, and sample count. p95 uses nearest rank. The mono baseline omits multiview projection counters because that pass does not execute. The GPU frame span brackets the GS pipeline before the ImGui overlay; it includes work outside named pass timers, so it is measured independently rather than computed by summing them. The JSON context records the timestamp period, valid bit count, timing schema version, and scope.

`BM_RenderThroughput` remains the uninstrumented wall-clock FPS measurement. Timestamp writes add GPU work, so compare GPU pass timings with other profiled runs. The renderer's `take_gpu_timings()` transfers collected samples and clears the collection; call `wait_idle()` first to obtain a complete batch. The opt-in `benchmark.gpu.timing.smoke` test runs the canonical scene with validation and checks batches of 1, 2, 3, and 64 frames, including repeated drains.
