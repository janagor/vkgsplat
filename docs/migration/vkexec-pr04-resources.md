# PR04: Gaussian resource declarations

The integration checkout pins vkexec to
`10df03e03d06cc05813393dd66af1178df38b4b5` in Dependencies.cmake.
The declarations use that dependency's `buffer_use`, `image_use`, usages and
access enum directly. No recording call consumes this metadata yet. No builder
runs during frame recording; there are no new per-frame allocations or barriers.

## Identity and range contract

`BindPassResources(RenderData const&)` snapshots non-owning buffer handles and
allocation sizes, scan hierarchy identities, and the underlying color VkImage.
It does not select a frame slot: these working allocations are currently shared.
Callers can substitute slot-specific bindings later. Missing optional allocations
remain absent until a builder that needs them rejects the invocation with
`std::invalid_argument`. Buffer extents must be finite and nonzero. Builders check
all used ranges against allocation bounds. Image ranges must be finite, nonempty
color subresources; callers supply a range within their actual image allocation.

Shader array indexing depends on live counts, capacities, selected view, tile ID,
and workgroup count. Such accesses conservatively cover the entire allocation.
Batch projection is tighter: views start at local index zero on every batch,
writing `view_count * splat_count * sizeof(GaussianProjected)` bytes. Camera
uploads and reads cover `view_count * sizeof(ProjectPushConstants)` bytes at zero;
there is no global quilt view offset in the batch shader. Scalar BDA counters,
status and total use four bytes. Dispatch arguments use exactly
`sizeof(VkDispatchIndirectCommand)` (12); draw arguments use
`sizeof(VkDrawIndirectCommand)` (16). Debug copies use the command's exact source
offset, destination section offset and byte count through `CopyResources`.

ImageBinding carries the incoming layout separately from the usage layout. The
binding snapshot defaults incoming layout to UNDEFINED because RenderData does
not track it. A caller must supply its known incoming layout before a read or a
retained draw; retained draws, blit sources and readback sources explicitly reject UNDEFINED. The metadata does not
claim that resetting a tracked image to UNDEFINED is always correct. Storage
output uses GENERAL, graphics uses COLOR_ATTACHMENT_OPTIMAL, and blits/readback
use TRANSFER_SRC/DST_OPTIMAL. Existing explicit transitions remain caller-owned.

## Shader manifest

R = read, W = write, RW = read/write (including global atomics). All shader buffer
uses are storage_compute unless marked graphics. Shared workgroup memory is not a
VkBuffer and requires no object declaration. A shader declaration alone does not
imply access: the table follows actual loads/stores.

| Shader / builder | Heap buffers | BDA buffers | Image |
| --- | --- | --- | --- |
| projection.comp / ProjectionResources | geometry R, appearance R, projected W | — | — |
| projection_batch.comp / BatchedProjectionResources | geometry R, appearance R, cameras R, projected W | — | — |
| graphics/binning.comp / BinningResources | projected R, unsorted keys/values W | instance count RW (`atomicAdd`) | — |
| graphics/prepare_sorting.comp / SortPreparationResources | unsorted keys/values R, sorted keys/values W | instance count R, dispatch W, draw W | — |
| multi_radixsort_histograms.comp / RadixHistogramResources | selected keys R, histogram W | instance count R | — |
| multi_radixsort.comp / RadixScatterResources | selected keys/values R, opposite keys/values W, histogram R | instance count R | — |
| graphics/sphere.vert / GraphicsRasterizationResources | projected R, sorted values R (storage_graphics) | — | color RW attachment |
| graphics/sphere.frag / same draw | — (interpolants only) | — | blended output |
| graphics/identify_ranges.comp / GraphicsRangeResources | sorted keys R, ranges W | instance count R | — |
| tiled_compute/rasterization.comp / LegacyTiledRasterizationResources | projected R, sorted values R, ranges R | — | color W storage |
| tiled_count.comp / tiled::CountResources | projected R | counts W | — |
| tiled_scan.comp / tiled::ScanResources | — | input counts/sums R, level output W, next sums W if present, overflow RW | — |
| tiled_scan.comp add / tiled::ScanAddResources | — | child output RW, parent offsets R, overflow RW | — |
| tiled_prepare.comp / tiled::PrepareResources | — | counts R, offsets RW, total W, overflow RW, dispatch W | — |
| tiled_emit.comp / tiled::EmitResources | projected R, sorted keys/values W | counts R, offsets R, overflow RW | — |
| tiled_radix_histograms.comp / tiled::RadixHistogramResources | selected keys R, histogram W | total R | — |
| tiled_radix_prefix.comp local / tiled::RadixPrefixResources(Local) | histogram R | total R, prefix W, group prefix W | — |
| tiled_radix_prefix.comp groups / tiled::RadixPrefixResources(Groups) | — | total R, group prefix RW | — |
| tiled_radix_prefix.comp add / tiled::RadixPrefixResources(Add) | — | total R, group prefix R, prefix RW | — |
| tiled_radix_scatter.comp / tiled::RadixScatterResources | selected keys/values R, opposite keys/values W | total R, prefix R | — |
| tiled_identify_ranges.comp / tiled::RangeResources | sorted keys R, ranges W | total R, overflow RW | — |
| tiled_rasterization.comp / tiled::RasterizationResources | projected R, sorted values R, ranges R | overflow RW | color W storage |

`graphics/identify_ranges.comp` and `tiled_compute/rasterization.comp` are compiled
legacy shaders, not dispatched by the current paths. Their builders document
potential use without inventing a runtime invocation. Geometry and appearance
are not read again by rasterization: projection has baked those values into
projected data. The offsets buffer in tiled_prepare is RW, because that shader
both loads the last prefix and writes the terminal offset, including error paths.

### Heap slot audit

The descriptor schema in descriptor/descriptor_heap.cpp maps slots 0 geometry,
1 appearance, 2 sorted_indices, 3 sort_entries, 4 projected, 5/6 unsorted
keys/values, 7/8 sorted keys/values, 9 ranges, 10 color, 11 histogram, and 20 cameras.
Slots 2 and 3 are registered compatibility allocations but no current Gaussian
shader loads or stores them. All potentially reached slots of each invocation
are listed above. Dynamic radix indexing selects exactly one pair using parity;
no shader accesses arbitrary heap slots. Descriptor/sampler heap storage itself
and descriptor updates remain part of the descriptor binding infrastructure,
not shader storage data accesses. They are unchanged and must stay stable during
recording/submission; future graph integration must preserve their lifetime and
extension-specific descriptor visibility requirements separately.

### Radix and hierarchy invocation audit

The compiled external algorithm is the pair of repository overlays in
cmake/overlays/vkradixsort, derived from MircoWerner/VkRadixSort revision
`029c351d24d9a6c6d680de272cd24d4448a34373`. The upstream source is downloaded
for provenance; the overlays are the actual shaders compiled by CMake. Their
histograms are globally written, then read by scatter; histogram atomics and
scatter ranking atomics operate on shared workgroup memory only.

Graphics performs FOUR byte iterations. Preparation copies unsorted into sorted.
The exact directions are sorted→unsorted, unsorted→sorted, sorted→unsorted,
unsorted→sorted. Tiled emit writes sorted first, then performs EIGHT byte
iterations over the 64-bit tile/depth key with the same alternating direction.
Both finish in sorted buffers. Histogram and scatter also declare the 12-byte
indirect argument read independently of their shader storage accesses. Tiled
prefix phases use direct dispatch and do not consume indirect commands.

Scan level zero reads counts and writes offsets. Level N>0 reads sums[N-1] and
writes offsets[N-1]. Each level except the terminal level writes sums[N]. Add at
child level zero updates main offsets using offsets[0]; add at child level N>0
updates offsets[N-1] using offsets[N]. No unused siblings are declared. Empty
hierarchy vectors represent a single direct scan. Mismatched vectors, out of
range levels, terminal add, invalid radix iterations and invalid prefix phases
are rejected.

## Command recording cross-reference

| Recording site / operation | Declaration or boundary |
| --- | --- |
| gs/operations.cpp RecordProjection; gs/projection.cpp DispatchProjection | ProjectionResources |
| gs/projection.cpp DispatchProjectionBatch cmdUpdateBuffer | CameraUploadResources, transfer destination at zero |
| same function record_pass | BatchedProjectionResources, cameras compute read after transfer |
| gs/operations.cpp RecordPhaseACompute cmdFillBuffer | CounterInitializationResources (4 bytes transfer write) |
| RecordBinning / RecordHeapDispatch | BinningResources |
| RecordSortPreparation / record_pass | SortPreparationResources; includes copied keys/values and both command producers |
| RecordRadixSort / RecordRadixStep / RecordHeapIndirect | RadixHistogramResources and RadixScatterResources per iteration, including indirect read |
| gs/rasterization.cpp DrawIntoColorTarget cmdDrawIndirect | GraphicsRasterizationResources; exact draw argument read |
| DrawQuiltTile clear / LOAD | same builder; blending needs attachment RW even after CLEAR; retain requires known incoming layout |
| gs/tiled_compute/operations.cpp RecordCountScanEmit fill | tiled::OverflowInitializationResources |
| RecordCountScanEmit Dispatch count / prepare / emit | tiled::CountResources / PrepareResources / EmitResources |
| RecordScan upward / downward Dispatch | tiled::ScanResources / ScanAddResources, actual child level |
| RecordRadixSort DispatchIndirect / Dispatch | tiled histogram/scatter builders per byte and prefix builder per phase |
| RecordRanges cmdFillBuffer | tiled::RangeInitializationResources; range allocation is tile_count * sizeof(TileRange) |
| RecordRanges DispatchIndirect | tiled::RangeResources includes indirect argument read |
| RecordRasterization Dispatch | tiled::RasterizationResources |
| CopyDebugSection cmdCopyBuffer | CopyResources(source, debug_readback, 0, tiled_debug_offsets[section], tiled_debug_sizes[section]) |
| gs/rasterization.cpp BlitQuiltToSwapchain cmdBlitImage | BlitResources(color, selected swapchain image), transfer source/destination |
| capture/readback.cpp cmdCopyImageToBuffer | ImageReadbackResources(color, staging, color_width * color_height * 4), offscreen or interactive |

Debug sources are emitted sorted keys/values immediately after emit; projected,
counts, offsets, final sorted keys/values and tile ranges after range building.
Each copy has both source and destination declarations with distinct destination
section offsets. There is no status copy: ReadFrameStatus invalidates/maps the
host-visible overflow allocation after completion. Host visibility barriers,
invalidation, waits and CPU interpretation remain caller-managed. A future graph
must preserve shader→host visibility as well as completion, beyond these GPU
resource-use types.

BeginColorTargetFrame, FinishColorTargetFrame, tiled output transitions,
swapchain transitions, acquisition and presentation, ImGui, capture submission,
query-pool timestamps, scene uploads, descriptor initialization and video encoding
remain caller-owned boundaries. ImGui consumes its own descriptors and may load
an existing output attachment; its complete resource set is outside the Gaussian
builders. No layout transition, synchronization, queue submission, profiling,
frame ownership, descriptor allocation or execution code changes in PR04.

## Validation

The Catch2 target `vkgsplat_pass_resources_test` uses synthetic handles and no
Vulkan device. Its independently written expected access tuples cover shader
manifests, all radix byte directions, scan levels/adds, prefix phases, indirect
producer/consumer usages, batch byte ranges, fills/copies, selected identities,
image layouts/subresources, invalid inputs and immutable scene reads.

GPU reference scenarios must still be run on a descriptor-heap-capable device:
graphics/tiled, mono/quilt, small/large, interactive/offscreen, ImGui on/off,
resize/reload, pass timing and tiled status/debug readback. Merely passing the
metadata unit tests does not establish rendering or validation equivalence.

### Results for this change (2026-10-31)

- Full Clang Release application/test build: passed in
  `out/build/pr03-clang-release`, using the verified dependency pin above.
- New device-independent tests: 506 assertions in seven cases, all passed.
- Full host CTest: 138/138 passed (17.77 seconds).
- Focused synchronization validation: 6/6 passed as test processes. The layer
  still reports **11 SYNC-HAZARD-WRITE-AFTER-READ errors**, identical in type and
  count to PR03's preserved log. Matching counts and types do not establish that
  the individual hazards are identical. This is not a clean validation result; the
  existing acquire/layout dependency issue remains for the synchronization PR.
- Canonical 1920×1080 offscreen comparison against PR03: compute RGB is identical;
  graphics mean absolute channel error is 0.0000282922/255, maximum 3/255, and
  no pixels exceed the existing 4/255 tolerance.
- Formatting and whitespace checks passed. Evidence, commands' build output,
  captures, comparison JSON and validation logs are in `out/pr04-validation/`.

The existing suite exercises compact and canonical fixtures, offscreen quilt
sequences, GPU timing and tiled overflow/debug behavior. Interactive resize,
scene reload, ImGui toggling, a new large-scene stress run, Windows and other
compiler configurations were not exercised. Those parts of the requested matrix
remain unverified; no runtime behavior or performance-equivalence claim is made
for them. Production command-recording sources are untouched.

### Review follow-up

Blit and readback sources now reject an undefined incoming layout before
constructing declarations. Transfer destinations and cleared attachments still
allow UNDEFINED. Tests cover both sides of this contract, nonzero-offset bounds,
and independently substituted sort/dispatch handles.

Cleared graphics attachments retain read/write access: the pinned vkexec API's
`include/vkexec_graphics/pass.hpp` explicitly states that attachment access
includes loadOp, depth testing and blending reads. Its resource-state tracker
maps that access to Vulkan access masks and requires a known first-read layout.
Thus CLEAR removes the need to preserve prior pixels, but does not remove the
blend operation's attachment reads. Before graph integration, the clear's layout
and initialization must be modeled consistently with that tracker contract;
changing the declaration to write alone would omit the blend read access mask.
The current clear-versus-LOAD tests preserve this distinction in validation:
CLEAR permits UNDEFINED, while LOAD rejects it.

A device-backed test of BindPassResources' live allocation mapping remains a
follow-up. Synthetic builder tests and handle-substitution tests do not validate
that conversion; no new device fixture was introduced for this review fix.
The existing synchronization diagnostics remain an open PR05 issue, not a
successful synchronization-validation result.

Review-fix validation: the Release metadata test target rebuilt successfully;
all 520 assertions in nine cases passed. `git diff --check` passed. The historical
full-suite and rendering results above were not rerun for this metadata-only fix.
