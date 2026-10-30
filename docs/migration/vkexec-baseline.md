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
