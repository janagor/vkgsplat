# PR 05 synchronization audit

Base: `bdbf0f67047ad4d03e63f6faa8419963b377832c` (PR 04, local main).
Dependency: `10df03e03d06cc05813393dd66af1178df38b4b5`, verified in the build tree.
No submission, allocation, descriptor, algorithm, or resource-use metadata changes.

## Contracts and scope

All image barriers cover color aspect, mip 0, layer 0, one mip and one layer.
No queue-family ownership transfer is introduced. Buffer offsets below are zero
unless stated; global presets cover all memory in their stage/access scopes.
Unless explicitly stated, barriers are inside one command buffer and submission,
outside dynamic rendering. These explicit dependencies remain necessary.

| Recording site | Resource / range | Producer → consumer and hazard | Coverage / decision |
| --- | --- | --- | --- |
| operations::RecordProjection, projection::DispatchProjection | projected buffer, all views | compute storage write → compute/vertex shader read, RAW | retain compute_read; binning, tiled count and vertex consumers |
| operations::RecordBinning | unsorted keys/values and instance counter | compute write → sort preparation read, RAW | retain compute_read; counter uses BDA |
| operations::RecordSortPreparation | sorted keys/values, radix_dispatch_buffer, draw_indirect_buffer | compute write → compute read/write and DRAW_INDIRECT/INDIRECT_COMMAND_READ, RAW/WAW | retain compute_to_compute; first histogram uses radix_dispatch_buffer, graphics draw uses draw_indirect_buffer |
| operations::RecordRadixStep | both sorted key/value ping-pong buffers, histogram | compute write → next histogram/scatter read/write, RAW/WAW | retain compute_to_compute across four passes; final graphics barrier covers vertex consumption |
| operations::RecordPhaseACompute | instance_count_buffer, sizeof(u32) fill | CLEAR/TRANSFER_WRITE → compute storage read/write, RAW | retain transfer_to_compute; broader ALL_TRANSFER source is equivalent |
| projection::DispatchProjectionBatch before update | projection_cameras_buffer, whole allocation | compute storage read → CLEAR/TRANSFER_WRITE, WAR | migrated fourth legacy call to buffer_barrier; graphics_to_compute does not cover camera reads |
| projection::DispatchProjectionBatch after update | projection_cameras_buffer, cameras.size_bytes() updated | CLEAR/TRANSFER_WRITE → compute storage read, RAW | retain transfer_to_compute |
| pipeline::RecordQuiltCell | projected buffer reused by next view | vertex/fragment read → compute write, WAR | retain graphics_to_compute between views |
| pipeline::RecordQuiltBatchProjection | projected view slices and shared compute metadata | graphics read → compute write, WAR; compute write → compute read/write, RAW/WAW | retain both presets between batches; device-address resources and shared storage make global scopes appropriate |
| tiled::ComputeBarrier (all calls) | counts, scan levels/sums/offsets, total, overflow, keys/values ping-pong, histograms, prefixes, ranges, dispatch | compute write → compute read/write and indirect read, RAW/WAW | retain at each scan/prefix/radix/range boundary; exact shader selections in PR 04 resource table |
| tiled preparation after prepare dispatch | radix_dispatch_buffer, whole buffer | compute write → indirect histogram dispatch read, RAW | removed IndirectBarrier only: immediately preceding ComputeBarrier already covers both destination stage/access bits in pinned vkexec |
| tiled::DebugCopyBarrier after emission | sorted_keys_buffer, sorted_values_buffer, copied sizes | compute storage write → COPY/TRANSFER_READ, RAW | one global memory_barrier, avoids multiple API calls; compute_to_compute does not cover COPY |
| tiled::DebugCopyBarrier after ranges | projected, counts, offsets, sorted keys, sorted values, ranges, copied sizes | compute storage write → COPY/TRANSFER_READ, RAW | one global memory_barrier; CopyDebugSection retains source handles and per-section staging offsets/sizes |
| tiled::FinishDebugCopies | same sources plus staging destination | COPY reads → subsequent compute writes, WAR | retain transfer_to_compute: execution dependency protects source reuse even though staging is not read by compute |
| tiled fill barriers | tiled_overflow_buffer (sizeof(u32)), tile_ranges_buffer (tile_count * sizeof(TileRange)) | CLEAR/TRANSFER_WRITE → compute read/write, RAW | retain transfer_to_compute |
| BeginColorTargetFrame | data.color_image | discard UNDEFINED → COLOR_ATTACHMENT_OPTIMAL | NONE/NONE → attachment output/write; existing previous-frame completion protects reuse |
| DrawQuiltTile after first tile | data.color_image, whole color subresource | attachment write → attachment read/write, RAW/WAW; unchanged layout | direct synchronization2 image barrier preserves BY_REGION between dynamic rendering instances; every LOAD tile executes it |
| DrawQuiltTile projection synchronization | projected, sorted values, generated draw command | compute write → vertex/fragment read and indirect command read, RAW | retain compute_to_graphics; also protects draw_indirect_buffer consumed by cmdDrawIndirect |
| FinishColorTargetFrame | data.color_image | attachment write → BLIT or COPY read, RAW + layout | COLOR_ATTACHMENT_OPTIMAL → TRANSFER_SRC_OPTIMAL; COPY covers offscreen image-to-buffer consumers |
| tiled::RecordRasterization before dispatch | data.color_image | discard UNDEFINED → GENERAL | NONE/NONE → compute storage write; previous completion protects reuse |
| tiled::RecordRasterization after dispatch | data.color_image | compute storage write → BLIT or COPY read, RAW + layout | GENERAL → TRANSFER_SRC_OPTIMAL |
| BlitQuiltToSwapchain | acquired swapchain image at image_index | discard UNDEFINED → TRANSFER_DST_OPTIMAL | NONE/NONE → BLIT/TRANSFER_WRITE; acquire wait remains TRANSFER, which includes BLIT |
| RecordImguiOverlay fallback branches | same swapchain image | BLIT write → presentation | TRANSFER_DST_OPTIMAL → PRESENT_SRC_KHR, destination NONE/NONE |
| RecordImguiOverlay color | same swapchain image | BLIT write → attachment LOAD/blend read/write, RAW | TRANSFER_DST_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL; crosses into secondary-command rendering |
| RecordImguiOverlay present | same swapchain image | attachment write → presentation | COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR, destination NONE/NONE |
| capture::RecordReadbackCommands | supplied color_image, one color subresource | previous color/storage write and transfer read → COPY read | retain same TRANSFER_SRC_OPTIMAL layout conservatively; separate command buffer/submission after completion wait; includes compute producer now |

The broad presets above intentionally retain generic shader access flags from
vkexec: they cover storage accesses, including BDA loads, stores and atomics.
Narrowing those presets would require tracking multiple dynamically selected
resources and is deferred. PR 04 metadata still describes the same resources.

### Retained preset masks (pinned implementation)

| Preset | Source stage/access | Destination stage/access |
| --- | --- | --- |
| transfer_to_compute | ALL_TRANSFER / TRANSFER_WRITE | COMPUTE_SHADER / SHADER_READ + SHADER_WRITE |
| compute_to_compute | COMPUTE_SHADER / SHADER_WRITE | COMPUTE_SHADER + DRAW_INDIRECT / SHADER_READ + SHADER_WRITE + INDIRECT_COMMAND_READ |
| compute_read | COMPUTE_SHADER / SHADER_WRITE | COMPUTE_SHADER + VERTEX_SHADER / SHADER_READ |
| compute_to_graphics | COMPUTE_SHADER / SHADER_WRITE | DRAW_INDIRECT + VERTEX_SHADER + FRAGMENT_SHADER / INDIRECT_COMMAND_READ + SHADER_READ |
| graphics_to_compute | VERTEX_SHADER + FRAGMENT_SHADER / SHADER_READ | COMPUTE_SHADER / SHADER_WRITE |

All names in this table denote synchronization2 flags. `compute_read` is enough
for read-only consumers; write-after-read reuse is handled separately at quilt
view/batch boundaries. Debug COPY access is additional to the preceding compute
barrier, and its return execution dependency is additional to compute ordering.
The global tiled ComputeBarrier ranges are the live allocations selected by the
push constants/heap bindings, not staging allocations. Scan child/parent levels
and radix ping are selected per iteration; retaining the global scope covers
both without relying on validation recognizing physical storage addresses.

## Backend verification

Device selection already requires Vulkan 1.3 synchronization2 and enables it
in device.cpp. AdoptVkexecContext supplies api_version and synchronization2=true.
At the pinned revision context adoption selects the core synchronization2 backend
for Vulkan >=1.3 and verifies the barrier2 and submit2 procedure pointers.
Thus direct cmdPipelineBarrier2 in the quilt path uses an existing requirement;
there is no new device feature requirement or legacy-backend dependency.

## Reproduction and validation

PR 04 reference captures, comparison, validation logs and commands are retained
locally in out/pr04-validation (see reproduction.txt). Existing issue #14 reports
acquire/layout-transition WRITE_AFTER_READ; this is not a clean baseline.
PR 05 logs are in out/pr05-validation. Baseline was started before source edits
using the existing PR 04 binary. Validation needs the actual enabled Khronos
layer in addition to VK_LAYER_VALIDATE_SYNC=1; existing GPU test constructors
request it. The tiled test always enables validation.

Commands:

```sh
cmake --build out/build/pr03-clang-release -j 4
ctest --test-dir out/build/pr03-clang-release --output-on-failure --timeout 180
env VK_LAYER_VALIDATE_SYNC=1 ctest --test-dir out/build/pr03-clang-release -V --timeout 180 -L synchronization
```

The quilt parameter unit test checks actual handle propagation, preserved layout,
attachment masks, queue families and full subresource range without a device.
Existing tiled image/overflow tests exercise indirect dispatch, sort/debug copies;
offscreen sequence exercises quilt and readback; timing smoke exercises query paths.
Synchronization labels enable repeatable focused runs without suppressing baseline
validation diagnostics. Tiled and offscreen-sequence tests fail on validation
errors; headless/timing retain the documented baseline warnings. Test exit status
alone is not a clean validation result for those baseline-affected paths.

Interactive ImGui on/off, resize/reload, large-scene stress, video, full timing
on/off permutations and exact intermediate-buffer baseline comparisons remain
manual gates unless separately recorded below. No performance equivalence claim.

## Complete backend inventory

Search includes video synchronization2 calls, which are already modern and remain
unchanged: color_converter protects RGBA input, NV12 output and encode images;
video_encoder transitions upload/encode images across upload command recording.
Their encoding/conversion lifecycle is outside Gaussian barrier cleanup.

```text
src/vkgsplat_backend/vulkan/capture/readback.cpp:83:    auto barrier = vkexec::image_barrier(*context.vkexec_context,
src/vkgsplat_backend/vulkan/video/video_encoder.cpp:414:    VkImageMemoryBarrier2 to_copy{};
src/vkgsplat_backend/vulkan/video/video_encoder.cpp:430:    context.disp.cmdPipelineBarrier2(resources.upload_cmd, &dependency);
src/vkgsplat_backend/vulkan/video/video_encoder.cpp:452:    VkImageMemoryBarrier2 to_encode{};
src/vkgsplat_backend/vulkan/video/video_encoder.cpp:465:    context.disp.cmdPipelineBarrier2(resources.upload_cmd, &dependency);
src/vkgsplat_backend/vulkan/video/color_converter.cpp:405:  VkBufferMemoryBarrier2 rgba_barrier{};
src/vkgsplat_backend/vulkan/video/color_converter.cpp:419:  context_.disp.cmdPipelineBarrier2(command_, &dependency);
src/vkgsplat_backend/vulkan/video/color_converter.cpp:428:  VkBufferMemoryBarrier2 nv12_barrier{};
src/vkgsplat_backend/vulkan/video/color_converter.cpp:438:  VkImageMemoryBarrier2 image_barrier{};
src/vkgsplat_backend/vulkan/video/color_converter.cpp:455:  context_.disp.cmdPipelineBarrier2(command_, &dependency);
src/vkgsplat_backend/vulkan/video/color_converter.cpp:482:  context_.disp.cmdPipelineBarrier2(command_, &dependency);
src/vkgsplat_backend/vulkan/imgui_overlay.cpp:703:    auto barrier = vkexec::image_barrier(*context.vkexec_context,
src/vkgsplat_backend/vulkan/imgui_overlay.cpp:729:    auto barrier = vkexec::image_barrier(*context.vkexec_context,
src/vkgsplat_backend/vulkan/imgui_overlay.cpp:750:  auto color_barrier = vkexec::image_barrier(*context.vkexec_context,
src/vkgsplat_backend/vulkan/imgui_overlay.cpp:793:  auto present_barrier = vkexec::image_barrier(*context.vkexec_context,
src/vkgsplat_backend/vulkan/gs/quilt_attachment_barrier.hpp:8:[[nodiscard]] inline auto QuiltAttachmentBarrier(VkImage image) -> VkImageMemoryBarrier2
src/vkgsplat_backend/vulkan/gs/quilt_attachment_barrier.hpp:10:  return VkImageMemoryBarrier2{
src/vkgsplat_backend/vulkan/gs/pipeline.cpp:119:      auto barrier = vkexec::barrier::graphics_to_compute(*context.vkexec_context, command_buffer);
src/vkgsplat_backend/vulkan/gs/pipeline.cpp:185:      auto barrier = vkexec::barrier::graphics_to_compute(*context.vkexec_context, command_buffer);
src/vkgsplat_backend/vulkan/gs/pipeline.cpp:188:    if (auto barrier = vkexec::barrier::compute_to_compute(*context.vkexec_context, command_buffer); !barrier) {
src/vkgsplat_backend/vulkan/gs/rasterization.cpp:184:  auto color_barrier = vkexec::image_barrier(*context.vkexec_context,
src/vkgsplat_backend/vulkan/gs/rasterization.cpp:208:    auto barrier = vkexec::barrier::compute_to_graphics(*context.vkexec_context, command_buffer);
src/vkgsplat_backend/vulkan/gs/rasterization.cpp:225:    context.disp.cmdPipelineBarrier2(command_buffer, &dependency);
src/vkgsplat_backend/vulkan/gs/rasterization.cpp:253:  auto barrier = vkexec::image_barrier(*context.vkexec_context,
src/vkgsplat_backend/vulkan/gs/rasterization.cpp:277:  auto swapchain_barrier = vkexec::image_barrier(*context.vkexec_context,
src/vkgsplat_backend/vulkan/gs/projection.cpp:67:  auto barrier = vkexec::barrier::compute_read(*context.vkexec_context, command_buffer);
src/vkgsplat_backend/vulkan/gs/projection.cpp:90:  auto reuse_barrier = vkexec::buffer_barrier(*context.vkexec_context,
src/vkgsplat_backend/vulkan/gs/projection.cpp:102:  if (auto barrier = vkexec::barrier::transfer_to_compute(*context.vkexec_context, command_buffer); !barrier) {
src/vkgsplat_backend/vulkan/gs/operations.cpp:125:    auto barrier = vkexec::barrier::compute_read(*context.vkexec_context, command_buffer);
src/vkgsplat_backend/vulkan/gs/operations.cpp:146:    auto barrier = vkexec::barrier::compute_to_compute(*context.vkexec_context, command_buffer);
src/vkgsplat_backend/vulkan/gs/operations.cpp:160:    auto barrier = vkexec::barrier::compute_to_compute(*context.vkexec_context, command_buffer);
src/vkgsplat_backend/vulkan/gs/operations.cpp:222:  auto barrier = vkexec::barrier::compute_read(*context.vkexec_context, command_buffer);
src/vkgsplat_backend/vulkan/gs/operations.cpp:248:  if (auto barrier = vkexec::barrier::transfer_to_compute(*context.vkexec_context, command_buffer); !barrier) {
src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:138:    auto barrier = vkexec::barrier::compute_to_compute(*context.vkexec_context, command_buffer);
src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:234:    auto barrier = vkexec::memory_barrier(*context.vkexec_context,
src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:261:    auto barrier = vkexec::barrier::transfer_to_compute(*context.vkexec_context, command_buffer);
src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:359:    if (auto barrier = vkexec::barrier::transfer_to_compute(*context.vkexec_context, command_buffer); !barrier) {
src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:516:    if (auto barrier = vkexec::barrier::transfer_to_compute(*context.vkexec_context, command_buffer); !barrier) {
src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:593:  auto before = vkexec::image_barrier(*context.vkexec_context,
src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:613:  auto after = vkexec::image_barrier(*context.vkexec_context,
```

## Recorded results

- Release build: PASS, including the new quilt parameter test (15 assertions).
- Full host CTest after the expanded debug test: 141/141 PASS (16.17 s).
- First focused host validation run: 5/5 PASS including fixture setup. Eleven
  validation error reports, all SYNC-HAZARD-WRITE-AFTER-READ at acquisition/layout,
  matching the eleven PR 04 reports; no new error type. These warnings remain
  unresolved and prevent claiming clean synchronization validation.
- PR 04 capture comparison at 1920x1080, decoded RGB8: compute pixel-identical;
  graphics mean channel delta 0.0000260417/255, maximum 3/255, zero pixels above
  4/255. Tolerance is maximum channel delta <=4/255 for graphics; compute exact.
- The sandbox baseline attempt failed tiled/sequence tests with device access
  limitations (2 failures, timing skipped). Its log is retained, not treated as
  the host baseline. Host PR 04 captures and sync log remain the reference.
- Default 2x2 quilt runs use projection batching (default capacity four).
  The expanded sequence test also renders and repeats a 3x3 quilt (63x63 atlas),
  forcing three projection batches and checking every tile has scene pixels.
  Explicit batch-size-one remains a manual gate; there is no public batch-size
  configuration. The initial 64x64 test atlas was rejected correctly because
  its dimensions were not divisible by three; the test now uses 63x63.
- No CPU/GPU throughput benchmark comparison was run. Timing smoke is functional
  query-path coverage, not performance evidence. No merge-ready claim is made for
  the unexecuted matrix items above.

Local evidence: out/pr05-validation/{build.log,build-final.log,build-quilt.log,ctest.log,quilt-sync.log,
baseline-sync.log,post-sync.log,post-sync-final.log,reference-comparison.json,
reference-graphics.png,reference-compute.png}. The source audit and tests are
versioned; local build logs and captures follow the existing migration convention.

### Multi-batch regression follow-up

The new 3x3 test exposed four camera update WAW diagnostics in the initial
migration: vkCmdUpdateBuffer executes at CLEAR, not COPY. The buffer barrier now
uses CLEAR as its destination stage. The earlier update's transfer_to_compute
barrier supplies availability; the compute-to-CLEAR barrier orders the next
update after camera reads. The initial diagnostic log remains in quilt-sync-final.log.
See the [Vulkan clear-command specification](https://docs.vulkan.org/spec/latest/chapters/clears.html)
and [Khronos synchronization validation positive tests](https://fuchsia.googlesource.com/third_party/Vulkan-ValidationLayers/+/refs/heads/main/tests/unit/sync_val_positive.cpp)
for the update command's CLEAR classification. Final results are recorded below.

Final focused run after the CLEAR correction: **5/5 PASS**, 10.82 s,
`post-sync-corrected.log`. Tiled debug/overflow and 2x2/3x3 quilt/readback emit no
validation errors, with failure-on-diagnostic gates active. Remaining eleven
reports are exclusively the baseline acquisition WRITE_AFTER_READ in headless
and timing smoke. No WRITE_AFTER_WRITE reports remain.

Final full Release suite after all code and test-gate changes: **141/141 PASS**,
16.14 s (`ctest-final.log`). Final configure/build passed (`build-gates.log`),
and `git diff --check` passed. Repository-wide backend search finds no legacy
cmdPipelineBarrier() or non-2 memory/buffer/image barrier structures remaining.
The working branch is `refactor/vulkan-synchronization2`; changes are uncommitted.

## Implementation review follow-up

- Catch discovery now explicitly assigns CTest label `synchronization` to the
  quilt parameter case. `ctest -N -L synchronization` lists it plus the four GPU
  scenarios and required fixture setup (six entries; review-labels.log).
- The recorder uses `kQuiltAttachmentDependencyFlags` from the tested helper
  header. The parameter test now asserts it equals BY_REGION (16 assertions).
  This addresses flag regression coverage without a generic barrier framework.
- Reviewed focused suite: **6/6 PASS**, 9.78 s (review-sync.log). Tiled and quilt
  retain strict failure-on-validation-error gates. The same eleven baseline
  acquisition warnings remain; issue #14 is not changed by this follow-up.
- Single-view-per-batch fallback: **2/2 PASS**, including fixture setup
  (review-single-view.log). The generated projection_batch.comp.spv was moved
  aside temporarily, forcing InitProjection's existing fallback to batch size
  one; it was restored in a finally block. This exercises both 2x2 and 3x3 quilts
  without adding a production test setting. No validation errors were emitted.
- Larger scene: deterministic 524,288 Gaussians, FNV checksum db6088e8223cbe87,
  1920x1080, eight frames per mode, camera position (0,0,10). Both modes exit 0
  without validation errors (review-large-far.json and corresponding logs).
  At the original z=6 camera tiled rendering correctly reported intersection
  capacity exceeded under its unchanged budget; no budget/algorithm change was
  introduced. The farther-camera retry keeps the larger workload in capacity.

The prior unexecuted single-view and larger-scene gates are superseded by this
follow-up. Manual visual inspection, in-place scene reload, GPU pass-time/CPU
recording-time comparisons, and timing-on/off cross products remain separate
from the automated evidence above.

### Windowed paths and resize smoke

An isolated Xvfb server ran four windowed application instances: ImGui on/off
crossed with the canonical 262,144 and larger 524,288 scenes. Each instance
processed four X11 resize requests (800x600, 1024x768, 640x480, 1024x1024) and
closed through WM_DELETE_WINDOW, exit 0. Each produced five acquisition
WRITE_AFTER_READ diagnostics with the same vkAcquireNextImageKHR → layout
transition signature as issue #14. No other validation error was reported.
These are successful lifecycle smoke checks, **not clean synchronization
validation** or manual visual inspection. Fresh scene loads were exercised;
the current application/Renderer API has no in-place scene-reload operation.
That requested scenario is not claimed as covered by restarting the application.

Evidence and reproducible harness: review-window.json, review-window-driver.py,
review-window-driver.log, review-window-{True,False}-{0,1}.log. The harness owns
its isolated display and application windows and leaves the desktop untouched.

### Small PR 04 / PR 05 performance comparison

The five modified production .cpp files were temporarily restored from
bdbf0f67047ad4d03e63f6faa8419963b377832c to build a PR 04 benchmark with the same
build tree, compiler options, dependency and fixture. Their working contents
were restored in a finally block, then the PR 05 benchmark and focused tests
were rebuilt. No working changes or staged contents were discarded.

Filter: `^BM_RenderThroughput/baseline-v1/(graphics|compute)/262144/1920x1080/batch:4/`,
`--benchmark_min_time=0.2s --benchmark_repetitions=3`. Validation and GPU timing
are disabled by the throughput harness; its existing 64-frame warmup is retained.

| Mode | PR 04 median fps | PR 05 median fps | Change |
| --- | ---: | ---: | ---: |
| Graphics | 23.2289 | 23.0463 | -0.79% |
| Compute tiled | 9.5653 | 9.6318 | +0.70% |

These differences are within the observed repetition variation. This short
sequential sample, with CPU scaling enabled, is a throughput smoke comparison;
it does not establish performance equivalence or independently measure CPU
recording/GPU pass latency. An initial overly broad filter was stopped and is
not used for this comparison. Raw evidence is review-perf-small-{pr04,pr05}.json
and .log, review-perf-summary.json, and review-perf-{baseline,current}-build.log.

Issue #14 remains separate and unresolved. Remaining gates are physical-display
visual checks, any future supported in-place scene reload, full timer-on/off
matrix and controlled CPU recording/GPU pass timing measurements.

Final review verification after restoring the benchmark sources: focused suite
**6/6 PASS**, 10.18 s (review-final-sync.log), sixteen quilt assertions; eleven
baseline acquisition reports and no WAW diagnostics. Focused executables rebuilt
successfully (review-final-build.log); `git diff --check` passed. Review edits
are unstaged; the previously staged implementation was left intact.
