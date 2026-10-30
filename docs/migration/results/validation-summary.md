# PR 01 execution and validation summary

Executed 2026-10-31 (KST), source `6cc0716cf630be8bb955673721164c326c8dffee`,
vkexec `bc45759eaf73d87ec6348152441cbe868f71de30`. No production source or pin
changes. This supersedes the earlier user-reported-only execution record.

## Build and test results

| Invocation | Result | Scope / evidence |
| --- | --- | --- |
| Clean configure | PASS | Frozen detached worktree, fresh `pr01-performance` binary directory, Release, sanitizers off, IPO on |
| Initial sandbox build | Environmental failure | ccache could not write outside sandbox: `Read-only file system` |
| Host build retry | PASS | Entire build completed; no compiler warnings found in successful log |
| Clean CTest | PASS: 131/131, 18.11 seconds | Includes headless/offscreen, tiled image/overflow, sequence/camera path, quilt, timing and fixture rendering checks |
| Existing sanitized CTest | 136 passed, 14 skipped, 2 failed / 152 total | Existing Release build, ASan/UBSan on, video tests enabled; full log retained |
| Performance | PASS | Both modes, both benchmark families, 5 repetitions each, 256 measured frames in every repetition |
| Canonical PNG capture | PASS | Both mono modes, 1920×1080, decoded and visually inspected |
| Clean synchronization-validation run | Test exits PASS; validation FAIL | 6 tests including fixture setup; existing headless acquire/transition hazard |

The full clean suite does not enable optional Vulkan Video GPU tests or fuzzing;
the existing sanitized suite enables both. Totals differ intentionally and no
failures were excluded from the reported sanitized run. The sanitized fuzz test
passed (11.22 seconds); the historical ptrace failure did not recur outside the
sandbox.

The two failing sanitized tests are `headless.gpu.video.h264` and
`headless.gpu.video.mp4`. Both report no device supporting the required 8-bit
4:2:0 H.264 configuration, with missing `VK_KHR_video_queue`,
`VK_KHR_video_encode_queue`, and `VK_KHR_video_encode_h264` on Intel and llvmpipe.
Fourteen other video tests skip for unavailable support. Classification:
environmental capability limitation; the two wrappers fail rather than skip.
These are actual CTest failures, not source regressions established by this run.

## Synchronization validation

A separate run used layer settings `validate_sync=true`, info/warn/error logging,
and `VK_LAYER_VALIDATE_SYNC=1`. Startup diagnostics explicitly state:

```
Khronos Validation Layer Active:
Current Enables: VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT.
```

| Test | Validation finding |
| --- | --- |
| `headless.gpu.smoke` | One `SYNC-HAZARD-WRITE-AFTER-READ` report |
| `benchmark.gpu.timing.smoke` | Ten reports of the same hazard |
| `offscreen.gpu.smoke` | No validation errors in retained log |
| `tiled.gpu.image_and_overflow` | No validation errors; test exercises graphics and tiled paths |
| `offscreen.gpu.sequence` | No validation errors; includes compact-scene quilt checks |

The hazard reports `vkQueueSubmit2()` executing a `vkCmdPipelineBarrier2` image
layout transition that writes an image previously read by
`vkAcquireNextImageKHR`. The diagnostic says the destination stage mask does not
synchronize the layout transition. It was observed first with existing sanitized
binaries and reproduced with clean binaries. Classification: **existing baseline
validation issue**. Fixing it is outside this documentation PR. Tracked in
[issue #14: headless acquire/image transition synchronization](https://github.com/janagor/vkgsplat/issues/14),
with exact diagnostics, reproduction and acceptance checks.

CTest reports 6/6 passed because these tests do not consistently fail on printed
layer errors; that is a test-infrastructure limitation, not validation success.
The first diagnostic run mixed deprecated `VK_LAYER_ENABLES` with new settings
and logged a settings warning. The clean confirmation uses only the new settings
and confirms synchronization activation. Duplicate-layer/unavailable-ICD loader
warnings are environmental diagnostics, separate from the synchronization error.

## Rendering coverage and remaining gaps

Both canonical mono reference images are retained next to the clean benchmark
JSON, with hashes in the manifest. Camera/scene metadata and cross-mode pixel
metrics are in [the baseline](../vkexec-baseline.md). The offscreen CLI captures
are validation-disabled; the separate tests above provide validation coverage.
Cross-mode images exceed the proposed 0.1% changed-pixel review threshold, so
compare future output against the reference for the same mode.

Headless and offscreen checks executed successfully on the host Intel GPU;
interactive display availability is not a blocker for those checks. Interactive
ImGui, window resize, interactive scene reload, and presentation after window
resize remain **NOT EXECUTED**. No interactive display/surface probe was completed.
The clean offscreen sequence test covers a compact-fixture 2×2 quilt, not a
canonical-scene quilt capture. Canonical quilt references and larger-scene stress
coverage remain **NOT EXECUTED**. PR 02's equivalence gate remains open for them.

## Exact configuration and execution

From the frozen detached checkout, the actual configuration used:

```sh
cmake --preset unixlike-clang-release -B out/build/pr01-performance \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -Dvkgsplat_BUILD_BENCHMARKS=ON \
  -Dvkgsplat_ENABLE_SANITIZER_ADDRESS=OFF \
  -Dvkgsplat_ENABLE_SANITIZER_UNDEFINED=OFF \
  -Dvkgsplat_ENABLE_CLANG_TIDY=OFF -Dvkgsplat_ENABLE_CPPCHECK=OFF \
  -Dvkgsplat_ENABLE_HEADLESS_GPU_TESTS=ON \
  -Dvkgsplat_HEADLESS_GPU_TEST_VALIDATION=ON \
  -Dvkgsplat_HEADLESS_TEST_PLY=/path/to/verified/baseline-v1.ply
cmake --build out/build/pr01-performance --parallel 4
ctest --test-dir out/build/pr01-performance --output-on-failure --timeout 180
```

The PLY supplied at configure time was the already verified canonical fixture
from the original Release build; the new build generates and verifies its own
identical fixture too. The actual command also set `FETCHCONTENT_BASE_DIR` to
the earlier detached Release `_deps` directory to reuse fetched dependency
sources. No prior application build objects were copied. Both absolute paths are
recorded in archived `reproduction.txt` and cache; use local equivalent paths
when reproducing. Benchmark metadata was configured from the clean worktree.
IPO defaults ON; specify `-Dvkgsplat_ENABLE_IPO=ON` when reproducing explicitly.

Performance commands are in the baseline; both use `--benchmark_min_time=4x`,
`--benchmark_repetitions=5` and `batch:64`. GPU timestamps and throughput were
measured sequentially, with validation disabled, before the separate clean
synchronization run. Verify each iteration's actual sample count; aggregate rows
are not additional samples. The binary hash and full compiler/link commands are
retained alongside the source/dependency revisions.

For synchronization validation, write a `vk_layer_settings.txt` file containing:

```text
khronos_validation.validate_sync = true
khronos_validation.report_flags = info,warn,error
khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG
khronos_validation.log_filename = stdout
```

Then run (with `VK_LAYER_ENABLES` unset):

```sh
env VK_LAYER_SETTINGS_PATH=/path/to/settings VK_LAYER_VALIDATE_SYNC=1 \
  ctest --test-dir out/build/pr01-performance -V --timeout 180 \
  -R '^(headless.gpu.smoke|offscreen.gpu.smoke|tiled.gpu.image_and_overflow|offscreen.gpu.sequence|benchmark.gpu.timing.smoke)$'
```

Canonical references were captured with the clean CLI for `MODE=graphics` and
`MODE=compute`, using this same command in each case:

```sh
out/build/pr01-performance/src/vkgsplat_headless_app/vkgsplat_headless \
  --ply out/build/pr01-performance/benchmark/baseline-v1.ply \
  --target offscreen --width 1920 --height 1080 \
  --camera-position 0 0 6 --camera-target 0 0 0 \
  --rasterizer "$MODE" --output "reference-$MODE.png"
```

## Portable evidence

[Committed evidence archive](2026-10-31-clean/evidence.tar.gz) contains complete
configure/build/CTest/synchronization logs, the clean CMake cache, compiler commands,
source status, binary SHA-256, dependency manifest/patches and reproduction commands.
Raw benchmark JSON and reference PNGs are committed adjacent to the archive.
[Hash manifest](evidence-sha256.txt) uses repository-relative paths; verify with
`sha256sum -c docs/migration/results/evidence-sha256.txt` from the repository root.
Extract the archive anywhere with `tar -xzf evidence.tar.gz`.

Local collection paths `/tmp/vkgsplat-pr01-evidence` and
`out/pr01-baseline/2026-10-31` are supplementary; the portable archive is the
reviewable source of execution evidence. Earlier dirty/sanitized benchmark JSON
under `2026-10-31/` is historical, superseded by `2026-10-31-clean/`. CSVs now
refer only to the clean runs. The pin and all production rendering, test and
benchmark source files remain unchanged.
