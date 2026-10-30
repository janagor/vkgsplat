# vkexec upgrade — PR 02

Validated 2026-10-31 (Asia/Seoul), against the [PR 01 baseline](vkexec-baseline.md).
Only the dependency pin changes in production code. No C++ API adaptations,
renderer algorithms, descriptor ownership, barriers, frame waits, submission,
or GPU profiling changes are included. PR 03 compatibility cleanup is deferred.

## Revision and upstream review

| Item | Revision |
| --- | --- |
| Old vkexec | `bc45759eaf73d87ec6348152441cbe868f71de30` |
| New vkexec | `fb6b1f71d719b8943ccde48d237103bb3b305d75` |
| NVIDIA/stdexec, old and new | `ead186b1d8db3ebe37a946ff84a6ce08bf795153` |
| vkgsplat parent | `398da805e390dc0fd04005115995c3ee2accf9a8` |

Resolved from a fresh clone of `https://github.com/janagor/vkexec.git`,
`origin/main`; upstream commit date 2026-10-29, merge of PR #14
(graph execution optimization). Ancestry from the old pin passes.
The full comparison spans 44 commits, 110 files, 11,937 insertions and 7,011
deletions. Reproduce with:

```sh
git clone --no-checkout https://github.com/janagor/vkexec.git vkexec-review
git -C vkexec-review fetch origin main
OLD=bc45759eaf73d87ec6348152441cbe868f71de30
NEW=fb6b1f71d719b8943ccde48d237103bb3b305d75
git -C vkexec-review merge-base --is-ancestor "$OLD" "$NEW"
git -C vkexec-review log --oneline "$OLD..$NEW"
git -C vkexec-review diff --stat "$OLD" "$NEW"
git -C vkexec-review diff --name-status "$OLD" "$NEW"
```

Review focused on the [consumed API inventory](vkexec-api-inventory.md):

| Subsystem | Old/new finding |
| --- | --- |
| Adopted context | Public `context.hpp` unchanged. `init_adopted`, `lock_queue`, `submit(queue_submit const&)` and context-state destructor match ignoring whitespace after implementation extraction into `context_runtime.cpp`. Ownership defaults remain false; borrowed instance/device/queues remain borrowed. |
| Recording | `pass.hpp` splits into internal pass types, steps, execution and graph submission headers; queue/submission recording metadata and optional profiling are added. Direct/indirect `record_pass` still issue the same bind/push/dispatch commands. Heap recording bodies are unchanged; their includes are reorganized. Renderer stays on `record_pass`. |
| Compute and descriptors | `compute_pipeline.hpp`, `resource_table.hpp`, heap public headers (including `algorithm.hpp`), descriptor schema and binding backend are unchanged. No descriptor index, push-constant, storage-image or allocation adaptation. |
| Synchronization | `queue_submit.hpp`, submission implementation, timeline/frame-ring headers and implementation unchanged. Context submission/locking implementation preserved. Graph batch synchronization optimization is upstream-only; this renderer does not adopt it. `submit_scope.hpp` moves completion errors into owned locals before callback completion. |
| VMA/resources | Public VMA headers and implementation unchanged; target alias `vkexec::vma` retained. |
| CMake/transitive dependencies | New optional profiling target defaults OFF. Existing target aliases remain; `Dependencies.cmake` is unchanged, including stdexec. Existing parent Vulkan targets are reused. Parent manual `Vulkan::Headers` links remain for PR 03. |

Both factory headers still have all three unqualified lookups.
The existing patch is retained byte-for-byte, as are its CMake detection logic,
examples OFF and VMA ON settings. Each fresh Clang build fetches the pinned revision;
its only vkexec source modifications are the three namespace qualifications.
`git apply --reverse --check` verifies the applied patch in both Clang trees.
No manual edits to fetched sources were made.

## Build and runtime results

Clang Debug and Release builds pass without compiler warnings. Debug CTest
passes **121/121** (14.26 s), including ASan/UBSan and the fuzz test (11.27 s);
the historical LeakSanitizer/ptrace failure does not recur on the host.
Release CTest passes **131/131** (37.10 s), including
headless/offscreen, both rasterizers, compact-scene 2×2 quilt sequences,
GPU timestamps and canonical-fixture rendering. Test timings during concurrent
builds are not performance measurements.

Canonical 1920×1080 offscreen captures use PR 01's `baseline-v1` scene
(FNV-1a64 `ad124868b0d228b3`), camera (0,0,6) → (0,0,0), and host Intel
ADL GT2/Mesa 26.2.0. Decoded RGB8 differences against each mode's PR 01 PNG:

| Mode | Mean channel error /255 | Maximum /255 | Pixels with any channel error >4/255 |
| --- | --- | --- | --- |
| Graphics | 0.0000565844 | 2 | 0% |
| Compute tiled | 0 | 0 | 0% |

Graphics capture was visually inspected; framing and blending match the reference.
Compute is pixel-identical to its mode-specific reference. This supports mono
output equivalence on this host; it does not establish equivalence on other drivers.
The legacy scene checksum also passes (`resources/scene.ply`), but its windowed
fixture configurations were not used for these captures.

Focused synchronization-validation CTest passes 6/6, but layer validation
**still fails with the existing baseline issue**: one headless smoke and ten
GPU timing smoke `SYNC-HAZARD-WRITE-AFTER-READ` reports, exactly matching PR 01.
The issue is the acquire/image-layout transition described in
[issue #14](https://github.com/janagor/vkgsplat/issues/14).
Offscreen smoke, tiled image/overflow and offscreen sequence logs contain no
validation errors. Synchronization validation activation is confirmed in the log.
No new diagnostic category or count was observed; this is not a claim of clean
headless synchronization. Frame/barrier fixes remain outside PR 02.

All validation uses dedicated `out/build/pr02-*` trees; PR 01 trees and outputs
are preserved. Each vkexec checkout is freshly CPM-fetched and patched during
configuration. Unchanged dependencies reuse PR 01 source directories through
an initial cache (`FETCHCONTENT_SOURCE_DIR_<NAME>`); no prior build-tree
objects are copied. DOWNLOAD_ONLY dependencies get fresh checkouts. Their revisions match
PR 01, including the mutable ImGui fork; tools uses the same reused source.
The normal sandbox configure failed downloading CPM (DNS); the host retry
succeeded. The sandbox build failed writing ccache; the host retry succeeded.
These are environment failures, also observed in PR 01.

## Performance comparison

Same host, canonical scene/camera, 1920×1080 mono, Release/IPO ON,
ASan/UBSan OFF, validation/ImGui OFF. Profiling and throughput execute
sequentially after compilation and tests, five repetitions each, 64 warmup
frames and exactly 256 measured frames in every repetition. Per-pass sample
counts are checked too. CPU frequency scaling remains enabled, as in PR 01;
small timing changes are not evidence of an optimization or regression.

GPU medians and p95s below are medians across repetitions, not pooled samples.

| Mode | PR 01 median (ms) | PR 02 median (ms) | Change | PR 01 p95 (ms) | PR 02 p95 (ms) |
| --- | --- | --- | --- | --- | --- |
| Graphics | 48.658 | 48.715 | +0.12% | 49.637 | 49.539 |
| Compute tiled | 122.278 | 122.849 | +0.47% | 132.225 | 131.961 |

| Mode | PR 01 FPS | PR 02 FPS | Change | PR 01 benchmark-thread CPU ms/frame | PR 02 benchmark-thread CPU ms/frame |
| --- | --- | --- | --- | --- | --- |
| Graphics | 19.982 | 19.936 | −0.23% | 0.0376 | 0.0374 |
| Compute tiled | 8.060 | 8.027 | −0.41% | 0.0384 | 0.0362 |

Benchmark-thread CPU time is Google Benchmark's batch `cpu_time / 64`; it does
not measure CPU work on the render thread. It is reported for protocol continuity,
not as a complete application CPU frame-time measurement. All applicable per-pass
median/p95 differences are within 1.7%; raw values are retained. No substantial
throughput regression is observed in these runs.

No substantial overall GPU frame-time regression is observed. The new benchmark
metadata records `git_dirty=true` for the intentional pin edit; unlike PR 01,
this is not a frozen clean source baseline. The archived production diff records
the exact input, and binary hashes identify the measured executables.

## Reproduction

Use fresh binary directories. Omit the local initial-cache override to fetch
all dependencies normally; it only saves downloading unchanged sources.
Both Clang configurations disable clang-tidy/cppcheck for this integration check.

```sh
cmake --preset unixlike-clang-debug -B out/build/pr02-clang-debug \
  -DBUILD_TESTING=ON -Dvkgsplat_ENABLE_CLANG_TIDY=OFF -Dvkgsplat_ENABLE_CPPCHECK=OFF
cmake --build out/build/pr02-clang-debug --parallel 2
ctest --test-dir out/build/pr02-clang-debug --output-on-failure --timeout 180

cmake --preset unixlike-clang-release -B out/build/pr02-clang-release \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -Dvkgsplat_BUILD_BENCHMARKS=ON \
  -Dvkgsplat_ENABLE_SANITIZER_ADDRESS=OFF -Dvkgsplat_ENABLE_SANITIZER_UNDEFINED=OFF \
  -Dvkgsplat_ENABLE_IPO=ON -Dvkgsplat_ENABLE_CLANG_TIDY=OFF -Dvkgsplat_ENABLE_CPPCHECK=OFF \
  -Dvkgsplat_ENABLE_HEADLESS_GPU_TESTS=ON -Dvkgsplat_HEADLESS_GPU_TEST_VALIDATION=ON \
  -Dvkgsplat_HEADLESS_TEST_PLY=/absolute/path/to/verified/baseline-v1.ply
cmake --build out/build/pr02-clang-release --parallel 4
ctest --test-dir out/build/pr02-clang-release --output-on-failure --timeout 180
```

Generate the canonical PLY with the existing fixture generator first, or supply
PR 01's verified local copy as above. Capture and benchmark commands follow
[PR 01's protocol](vkexec-baseline.md#reproduction-and-portable-evidence), using
`pr02-clang-release` as the build directory. Exact executed commands, source
revisions, CMake caches and logs accompany the upgrade evidence.

## Remaining validation gates

Interactive ImGui on/off, window resize and interactive scene reload were not
executed. Compact-scene quilt tests pass, but canonical 2×2 quilt captures and
larger-scene stress remain unverified. Existing Linux/macOS/Windows GitHub Actions
were not triggered from this local branch; their status must be checked when the
PR runs. There are no workflow changes. These gaps and baseline issue #14 prevent
claiming the entire runtime/CI merge checklist is complete.

## Evidence

[Portable evidence archive](results/pr02/evidence.tar.gz) includes Clang configure/build logs,
CTest summaries and the full Release test log, synchronization diagnostics, upstream commit/stat/file
lists, dependency revisions, the exact production diff, factory patch, build
commands/caches, raw benchmark JSON, comparison calculations, artifact hashes
and exact executed reproduction commands. Extract it and run
`sha256sum -c evidence-sha256.txt` inside the extracted directory.
PNG captures remain locally under `out/pr02-validation/reference-*.png`; their
hashes and decoded comparison results are in the archive. Prior reference PNGs
remain in the committed PR 01 evidence. No PR 01 outputs were overwritten.

Archive SHA-256: `68cf914e08299056f29b4e7a6742459dd0e3e9e57faab7a1a347f6bf845a096e`.
