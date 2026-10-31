# vkexec integration baseline — PR 01

Verified 2026-10-31 (Asia/Seoul). This is the authoritative PR 02 comparison
baseline: clean frozen source, Release, ASan/UBSan off, IPO on. The dependency
pin, compatibility patches and renderer behavior are unchanged.

## Revisions and build configuration

| Field | Verified value |
| --- | --- |
| vkgsplat source | `6cc0716cf630be8bb955673721164c326c8dffee` |
| vkexec pin and fetched HEAD | `bc45759eaf73d87ec6348152441cbe868f71de30` |
| Candidate only, not installed | `fb6b1f71d719b8943ccde48d237103bb3b305d75` resolved from vkexec/main |
| Source checkout | Clean detached worktree; benchmark JSON `git_dirty=false` |
| Performance build | `Release`, Clang 22.1.8, Ninja 1.13.2, CMake 4.3.4, LLD |
| Sanitizers / analysis | ASan and UBSan OFF; clang-tidy and cppcheck OFF |
| Optimization | `-O3 -DNDEBUG`, thin LTO / IPO ON; full commands archived |
| Host | NixOS x86_64, Linux 6.18.52, Intel Core i5-12450H |
| GPU / driver | Intel(R) Graphics (ADL GT2), Mesa 26.2.0, raw driver 109060096 |
| Vulkan API / device IDs | 1.4.354, vendor 0x8086, device 0x46a3 |
| Renderer timestamp capabilities | 52.083332 ns period, 36 valid bits |
| Validation layer | Khronos 1.4.341; synchronization activation confirmed in logs |

The host GPU requires execution outside the restricted sandbox. The earlier
sandbox-only llvmpipe observation is not the hardware measurement environment.
CPU frequency scaling remains enabled; load averages are retained in raw JSON.
For PR 02 rerun before/after measurements under comparable host load and GPU
clock conditions; do not attribute cross-environment differences to the upgrade.
No other benchmark or validation workload was launched concurrently with the
performance runs. The binary SHA-256 is recorded in
[the binary manifest](results/2026-10-31-clean/binary-sha256.txt).

[API inventory](vkexec-api-inventory.md) enumerates all direct imports and
qualified references, queue/ownership contracts and Vulkan feature enablement.
There are no direct renderer `vkexec::detail` dependencies in this revision.
The factory namespace patch and `Vulkan::Headers` linkage remain required
compatibility mechanisms; their removal conditions are documented there.
[Dependency revisions](results/2026-10-31-clean/dependency-revisions.csv) records fetched SHAs,
including mutable ImGui/tools refs. vkexec's modified source is exactly the
existing factory patch; stdexec patches icm's build-error matching script.
Both dependency patches are retained in the evidence archive.

## Results

| Check | Result |
| --- | --- |
| Clean Release configure/build | PASS; logs, cache, compiler commands and binary hash archived |
| Clean CTest | PASS: 131/131, 18.11 s, headless GPU tests enabled |
| Existing sanitized suite | 136 passed, 14 skipped, 2 failed; video-extension limitations documented separately |
| Graphics/tiled mono benchmark rendering | PASS; five repetitions each |
| Reference PNGs, offscreen readback | Captured, decoded and visually inspected for both mono modes |
| Existing quilt checks | PASS in offscreen sequence test using its compact fixture; not canonical quilt references |
| Synchronization validation | FAIL: existing headless acquire/transition hazard reproduced; offscreen tests had no validation errors |
| Interactive ImGui / window resize / interactive reload | NOT EXECUTED; usable interactive display not established |
| Larger-scene stress / canonical 2×2 references | NOT EXECUTED |

The clean suite has video GPU tests disabled (the default); the existing
sanitized build enables them. Therefore their test totals differ. See
[validation summary](results/validation-summary.md) for exact findings and commands.
Passing CTest does not override validation errors printed by the layer.

## Performance baseline

Scene `baseline-v1`, 262,144 Gaussians, 1920×1080 mono, batch 64, 64 warmup frames.
Both benchmark families execute exactly four measured batches per repetition,
five repetitions per mode: **256 measured frames in every repetition**.
Validation and ImGui are disabled for performance.

| Mode | Median of frame medians (ms) | Median of frame p95s (ms) | Median throughput (FPS) | Frames/repetition |
| --- | --- | --- | --- | --- |
| graphics | 48.658 | 49.637 | 19.982 | 256 |
| compute | 122.278 | 132.225 | 8.060 | 256 |

These are medians across repetitions, not pooled percentiles. Full per-pass and
per-repetition statistics, tiled intersection/scratch metrics and metadata are in
[GPU JSON](results/2026-10-31-clean/gpu.json),
[throughput JSON](results/2026-10-31-clean/throughput.json),
[GPU CSV](results/gpu-baseline.csv) and [throughput CSV](results/throughput-baseline.csv).
Mono multiview projection is not applicable. GPU timing scope is `gs_pipeline`;
throughput includes CPU recording, synchronization and headless presentation.

Earlier dirty, sanitized, IPO-off measurements are retained under
[`results/2026-10-31/`](results/2026-10-31/gpu.json) for provenance only. They
reported 42.533/102.455 ms and 22.916/9.638 FPS; throughput repetitions had
192/64 frames. They are superseded, not combined with this clean baseline.
Differences between these runs do not measure a vkexec change.

## Rendering reference contract

Canonical generator version 1, distribution `volume-cloud-v1`, seed `0x5EED2026`,
PLY FNV-1a64 `ad124868b0d228b3`. Camera: position (0,0,6), target (0,0,0),
up (0,1,0), FOV 45°, near/far 0.1/100, aspect 16:9.
Captured with the clean headless CLI, `--target offscreen`, validation off,
scene-only PNG, 1920×1080, on the Intel GPU above:
[graphics reference](results/2026-10-31-clean/reference-graphics.png),
[compute reference](results/2026-10-31-clean/reference-compute.png).
Image hashes are in the evidence manifest. Both show the same cloud framing
against a black background; these are mode-specific references.

Decoded RGB8 cross-mode difference: mean channel error 0.409/255, maximum 43/255,
0.3756% pixels with any channel error >4/255. This exceeds the proposed 0.1%
pixel threshold; do not claim pixel equivalence between rasterizers.
For PR 02 compare each mode against its own reference: report mean/max channel
error and fraction beyond 4/255; initially flag mean >1/255 or fraction >0.1%,
then visually inspect discrepancies. These thresholds are review criteria, not
cross-driver guarantees. PNG file hashes identify artifacts only.
Legacy `test/fixtures/rendering/` configs use a separate local scene and cameras.

## Reproduction and portable evidence

Use a fresh detached checkout of the source SHA above, not a documentation-dirty
checkout. From its root, configure exactly this performance configuration:

```sh
cmake --preset unixlike-clang-release -B out/build/pr01-performance \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -Dvkgsplat_BUILD_BENCHMARKS=ON \
  -Dvkgsplat_ENABLE_SANITIZER_ADDRESS=OFF \
  -Dvkgsplat_ENABLE_SANITIZER_UNDEFINED=OFF \
  -Dvkgsplat_ENABLE_IPO=ON \
  -Dvkgsplat_ENABLE_CLANG_TIDY=OFF -Dvkgsplat_ENABLE_CPPCHECK=OFF
cmake --build out/build/pr01-performance --parallel 4
ctest --test-dir out/build/pr01-performance --output-on-failure
```

For the executed GPU-test options and fetched-source reuse override, see the
validation summary and archived `reproduction.txt`. Verify the generated scene
with `benchmark/fixture/vkgsplat_fixture_gen --verify benchmark/baseline-v1.ply`
using paths under the build directory. Run GPU timings and throughput separately:

```sh
mkdir -p baseline-results
out/build/pr01-performance/benchmark/vkgsplat_bench \
  --benchmark_filter='BM_GpuPassTimings/baseline-v1/(graphics|compute)/262144/1920x1080/batch:64' \
  --benchmark_min_time=4x --benchmark_repetitions=5 \
  --benchmark_out=baseline-results/gpu.json --benchmark_out_format=json
out/build/pr01-performance/benchmark/vkgsplat_bench \
  --benchmark_filter='BM_RenderThroughput/baseline-v1/(graphics|compute)/262144/1920x1080/batch:64' \
  --benchmark_min_time=4x --benchmark_repetitions=5 \
  --benchmark_out=baseline-results/throughput.json --benchmark_out_format=json
```

Check every iteration result has `iterations × 64 >= 256`; additionally check
`gpu.samples` and all applicable pass sample counts. Never infer sample counts
from repetitions or elapsed-time settings alone.

The committed [portable evidence archive](results/2026-10-31-clean/evidence.tar.gz)
contains complete build/CTest/validation logs, CMake cache, compiler commands,
source status, dependency patches and reproduction commands. Raw benchmark JSON
and PNGs are adjacent. Verify hashes from the repository root:

```sh
sha256sum -c docs/migration/results/evidence-sha256.txt
```

## Historical context and PR 02 gate

The October 15 baseline used source `d371e36d5e4c871b0195c1eb34ea45f0d7275c61`,
vkexec `73176a3e6a3df3ec45f42bb0a955e8317517de25`, candidate
`fa5e8e59923d2a1ba45747bf7d0ffca83dc414cd`. It performed no clean build or GPU
rendering run. Existing-directory CTest passed 2/3; fuzzing ended in the
LeakSanitizer ptrace failure. That failure did not recur in the host sanitized
run here. Full old text remains available via
`git show 6cc0716:docs/migration/vkexec-baseline.md`.

PR 02 should change only the vkexec pin and minimal compilation compatibility.
Compare identical scenes, cameras, modes, dimensions, hardware and clean build
settings. Preserve and compare the known synchronization hazard explicitly;
PR 01 does not repair it or certify clean headless synchronization. Interactive
coverage and canonical quilt references remain open runtime-equivalence gates.

## PR 03 compatibility audit (2026-10-31)

### Current implementation and review status

The upstream factory prerequisite is resolved by
[`10df03e03d06cc05813393dd66af1178df38b4b5`](https://github.com/janagor/vkexec/commit/10df03e03d06cc05813393dd66af1178df38b4b5),
now pinned in Dependencies.cmake. The upstream commit includes the three factory
namespace fixes and regression coverage. The local factory patch, configure-time
header repair and both parent-side Vulkan header links are removed. WP-A's code
changes are complete.

The user confirmed final-revision validation during PR review. This confirmation
is the basis for closing the final-revision validation gate; the agent did not
rerun configuration, builds or tests. No detailed final-revision test totals,
clean-checkout log or capture metrics were supplied, so none are inferred here.
The earlier measurements below are explicitly historical and are not attributed
to the final pin. GCC, Debug, Windows, full performance repetitions and exhaustive
interactive checks are outside the agreed final gate. The baseline synchronization
hazard remains a separate issue. No additional implementation blocker is identified
in this review; normal PR/CI approval remains separate.

### Historical factory investigation against the PR 02 pin

This section preserves the investigation based on PR 02, vkgsplat `3fd2858`,
with vkexec `fb6b1f71d719b8943ccde48d237103bb3b305d75`. Agent validation used
**Clang Release** only.

A pristine local clone of that revision compiled the SPIR-V, GLSL and frame-ring
calls when their headers came first. Including public `vkexec/tensor.hpp` first
failed all three names: its nested `vkexec::factory::detail` shadowed
`vkexec::detail`. The same test passed with exactly the three namespace
qualifications. Thus a compute-first test alone would have falsely justified
removal of the workaround. This historical prerequisite is now resolved upstream.

The vkgsplat test build compiles the tensor-first public factory calls and a VMA
allocator factory in a mandatory object library linked only through upstream
public targets. It never executes GPU operations. See the
[compatibility notes](pr03/README.md) and adjacent compiler diagnostics.

### Dependency and public API contracts

Removed both parent-side mutations linking Vulkan::Headers into vkexec and
vkexec_vma. The pinned upstream Dependencies.cmake provides vkexec_vulkan,
which exposes Vulkan::Headers and a link-only Vulkan::Vulkan requirement;
src/vkexec links that target publicly for source builds. vkexec::vma links
vkexec::vkexec and GPUOpen::VulkanMemoryAllocator publicly. No fallback include
path or per-executable workaround was added. This validates source-build use,
not an installed-package export or an environment without system headers.

Repeated searches over src, include, test and benchmark find no direct
vkexec::detail/internal uses, private detail-header imports or fetched-source
includes. Adoption, compute/descriptor factories, VMA, record_pass, context
submission, frame_ring and lock_queue remain public interfaces. Their owning
public headers and the PR 02 inventory were reviewed; no renderer API rewrite
or ownership adapter is justified. The new compile regression exercises return
type deduction for both core compute overloads, frame_ring and VMA allocation.

### Ownership and queue synchronization

The native instance/device belong to VulkanDriver. The adopted context borrows
them and their queues; AdoptVkexecContext explicitly rejects unexpected owned
instance/device flags. The VMA allocator borrows this context. Renderer resources
(pipelines, heaps, tensors/images, frame_ring, command pools and swapchain views)
therefore must be released before allocator, context and native device.

Renderer destruction stops/joins the render worker before Cleanup. Cleanup
waits for device/present completion, resets frame_ring and offscreen sync,
readbacks and commands, shuts down ImGui before shared descriptors, and releases
pipelines and allocations. VulkanDriver then waits idle, resets swapchain,
allocator and vkexec context, destroys device, surface and instance in that order.
Renderer::Impl deliberately declares owned_engine before render_data and
render_thread, so reverse member destruction also preserves the owner lifetime.
The overload accepting an external Engine requires that Engine to outlive Renderer.

Driver creation uses a local unique_ptr: failures during native initialization,
adoption, allocator or swapchain creation unwind the successfully created owners.
Renderer resource creation failures explicitly call Cleanup before returning the
original built.error(); cleanup does not overwrite it. GPU error paths preserve
the first worker error. This is a source audit, not exhaustive injected-failure
coverage of every Vulkan allocation site.

PresentSwapchainImage's lock_queue guard is unchanged. Direct video encoder and
color converter submissions and queue waits also lock the specific queue. Normal
rendering and readback submit through context. Renderer recreation/cleanup and
driver idle calls rely on worker serialization or worker shutdown; they do not
make one queue's lock cover another queue. External users sharing an Engine must
serialize teardown/recreation against other queue users. No waits, frame-ring
behavior or barrier declarations changed here.

Interactive resize, scene reload and ImGui toggling still require runtime
coverage; the source audit does not certify them. The known baseline headless
synchronization hazard remains separate from PR 03. These interactive checks
are outside the agreed final validation gate.

### Historical Clang Release results against `fb6b1f7`

Fresh binary tree `out/build/pr03-clang-release`, Clang 22.1.8, Ninja,
`CMAKE_BUILD_TYPE=Release`, IPO ON, ASan/UBSan OFF, analysis tools OFF,
benchmarks and headless GPU tests ON. Unchanged dependency sources reuse the
PR 02 initial cache; vkexec was freshly fetched at `fb6b1f7`. First
configure and repeated configure pass on the host. Sandbox attempts hit the
previous CPM DNS and ccache write restrictions; host retries pass. The fetched
vkexec diff at that time was exactly the retained three-line factory repair,
confirmed by reverse patch check. This historical run did not establish the
final revision’s unmodified-source status.

| Check | Result |
| --- | --- |
| Fresh and repeated configure | PASS |
| Full Clang Release application/test build | PASS, including public factory/VMA compile target |
| Host CTest | PASS: 131/131, 19.39 s |
| Focused synchronization-validation CTest | PASS: 6/6; layer still reports baseline errors |
| Canonical graphics versus PR 02 | Mean channel difference 0.0000313465/255, max 2/255, 0% pixels beyond 4/255 |
| Canonical compute versus PR 02 | Pixel-identical decoded RGB8 |
| Internal namespace/private include search | No matches in renderer/test/benchmark/include sources |
| Patch removal / unmodified dependency | Not established by this historical run; upstream prerequisite subsequently resolved |
| Full performance repetitions | Not rerun; no throughput or CPU/GPU timing equivalence claim |
| Interactive resize, reload, ImGui toggle, Windows, GCC, Debug | Not executed |

The restricted-sandbox CTest run failed tiled image/overflow, offscreen sequence
and canonical fixture GPU smoke with DRM enumeration errors/fallback-driver
behavior. The host retry on the PR 02 Intel GPU passes all 131 tests. Preserve
both logs rather than interpreting the sandbox result as a dependency regression.

Focused validation matches PR 02: one headless and ten timing-smoke
SYNC-HAZARD-WRITE-AFTER-READ diagnostics (the identifier appears twice per report,
22 text matches in both logs). Other focused rendering logs have no validation
errors. This is the existing acquire/layout-transition issue #14, not clean
synchronization validation. Captures use PR 02's canonical scene and camera
at 1920×1080, comparing each renderer against its own PR 02 output.

The historical evidence archive contains configure/build/test and validation
logs, captures/comparison data, the fetched-header diff and reproduction settings.
It and its checksum are retained locally in
`out/pr03-validation/historical-evidence/`, outside the source documentation.
The include-order compiler diagnostics remain in `docs/migration/pr03/`.
The obsolete proposed upstream patch is removed; the committed fix is linked
above. Current implementation and user-reported final-revision validation status
are recorded separately at the start of this audit. No PR 04 resource-use or
PR 05 barrier work is included.
