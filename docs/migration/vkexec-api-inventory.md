# vkexec integration inventory

Inspected source: `6cc0716cf630be8bb955673721164c326c8dffee`, 2026-10-31.
Locations below refer to that immutable revision. This inventories direct imports
and qualified references in `src`, `include`, `test`, and `benchmark`, including
`vkexec_vma` (which the narrower historical search omitted). Public means a
non-detail exported header/API, not a promise of compatibility with the candidate.
Candidate compilation and behavioral changes remain untested until PR 02.

## Integration contracts and migration actions

| Group / principal source under `src/vkgsplat_backend/vulkan/` | Current contract and ownership | PR 02 / PR 03 action |
| --- | --- | --- |
| Context: `vulkan_driver.cpp`, `vulkan_context.hpp` | `factory::adopt_context` borrows vk-bootstrap instance/device/queues; checks `owns_instance()` and `owns_device()` are false. VMA allocator belongs to Context. | Check adoption signature and capability fields; preserve borrowed ownership. |
| Allocation: `gpu_buffers.hpp`, `app_state.hpp`, `sphere_setup.cpp`, `gs/rasterization.cpp` | VMA buffers, image, image view and tensors are RAII optionals. Host-visible storage uses BDA, device-local storage supports transfer/indirect, staging has no BDA. Mapped writes check size and flush. | Check allocator/resource factories, memory flags, map/flush and tensor accessors; preserve allocator lifetime. |
| Compute: `gs/load_heap_pipeline.hpp`, `gs/operations.cpp`, `gs/projection.cpp` | SPIR-V heap pipelines are created synchronously with `try_sync_wait_value`; algorithms and push-data layouts survive recording and completion. `record_pass` supports direct and indirect dispatch; renderer controls barriers. | Check factory/pass overloads and push-data ABI; do not introduce graphs here. |
| Tiled: `gs/tiled_compute/operations.cpp` | Shared count/scan/emit/radix/range/raster scratch; direct/indirect dispatch and explicit transfer/compute/image barriers. Overflow status is consumed after completion before overwrite. | Compare dispatch sizing, indirect visibility, scratch capacity and overflow behavior. |
| Graphics: `gs/graphics/graphics_pipeline.cpp`, `gs/rasterization.cpp` | Descriptor graphics pipeline uses premultiplied blending and owned pipeline/image views. Vulkan dynamic rendering/draw commands coexist with vkexec barriers and push data. | Verify blend, layout, SPIR-V and image-view lifetimes. |
| Descriptors: `descriptor/descriptor_heap.cpp`, `imgui_overlay.cpp` | Public schema/table/resource refs plus explicit device addresses feed public descriptor writers. Query stride/alignment/reserved ranges; bind resource and sampler heaps. Heap refresh waits device idle, reallocates if needed, replays ImGui descriptors and flushes both heaps. | Check descriptor sizes/ranges and binding signatures; preserve shared GS/ImGui slots and replay ordering. |
| Submission: `render_thread.cpp`, `renderer.cpp` | `factory::make_frame_ring`; wait current slot, previous slot (shared Gaussian buffers/color target), and acquired image before reuse. Allocate timeline value, build submit sync with TRANSFER acquisition wait stage, `context::submit`, then `mark_submitted`. Actual rendering submits to graphics queue even if a separate compute queue was adopted. | Preserve queue and wait semantics; audit timeline/error contracts before any synchronization rewrite. |
| Offscreen/readback: `render_thread.cpp`, `capture/readback.cpp` | Offscreen path waits its own timeline before reuse; queue submission signals completion. Readback uses image barriers and staging storage; completed data must outlive CPU PNG work. | Verify semaphore submit spans remain valid for submission and readback completes before mapping/reuse. |
| Teardown/reconfiguration: `vulkan_driver.cpp`, `render_thread.cpp` | Renderer cleanup releases GPU resources before driver; driver waits idle, resets swapchain, allocator, borrowed context, then destroys device/surface/instance. Reconfiguration waits GPU idle. | Retain destruction order and frame serialization; runtime resize/reload/shutdown validation required. |
| Errors: `vkexec_error.hpp`, `test/render_thread.cpp` | `error`, `vulkan_category`, `make_vk_error_code`; device loss maps into application error propagation. | Preserve device-loss classification and shutdown wakeups. |

## Internal API dependencies

No `vkexec::detail::` reference or `vkexec/detail/` import exists in the inspected
source directories. The October 15 historical `heap_descriptor_backend::lower`
call is absent in this revision. Public `make_resource_table`, `entries()`, and
descriptor writer helpers now implement this path. Do not carry the old internal
API finding forward as a current requirement.

The CMake factory patch does mention `::vkexec::detail` inside dependency headers;
this is a dependency repair, not a direct renderer internal API call.

## Compatibility workarounds (preserved)

`Dependencies.cmake` applies
[`vkexec-factory-detail-namespace.patch`](../../cmake/patches/vkexec-factory-detail-namespace.patch)
to fetched `include/vkexec/compute_pipeline.hpp` and
`include/vkexec_extensions/timeline_semaphore/frame_ring.hpp`. The three factory
lookups for SPIR-V, GLSL and frame-ring construction incorrectly resolve
`detail` within `vkexec::factory`; the patch qualifies `::vkexec::detail`.
Configuration recognizes the exact broken or fixed patterns and rejects unknown
header shapes. Removal condition: the selected upstream revision contains all
three fixes and a clean configure/build passes without this patch.

The same CMake file links `Vulkan::Headers` PUBLIC to `vkexec` and `vkexec_vma`
when those targets exist. Nested vkexec includes `<vulkan/...>` without linking
the parent headers target; this ensures the selected CPM Vulkan-Headers are used
rather than relying on system headers. Removal condition: upstream propagates
the intended Vulkan headers include path for both targets and a clean build with
system headers absent/older proves it. Neither workaround is removed by PR 01.

## Vulkan capability and queue contract

`vkexec_requirements.cpp` requests Vulkan 1.4 plus `VK_EXT_descriptor_heap`,
`VK_KHR_buffer_device_address`, and `VK_KHR_shader_untyped_pointers`.
`device.cpp` actually adds these required feature structures to the selector:

| Feature structure | Required members |
| --- | --- |
| `VkPhysicalDeviceDescriptorHeapFeaturesEXT` | `descriptorHeap` |
| `VkPhysicalDeviceShaderUntypedPointersFeaturesKHR` | `shaderUntypedPointers` |
| `VkPhysicalDeviceVulkan12Features` | `bufferDeviceAddress`, `timelineSemaphore` |
| `VkPhysicalDeviceVulkan13Features` | `synchronization2`, `dynamicRendering`, `shaderDemoteToHelperInvocation`, `maintenance4` |
| `VkPhysicalDeviceFeatures` (conditional) | `shaderInt64` when configured |

The selected physical device is passed to `vkb::DeviceBuilder`; extension names
alone do not enable these features. `adopt_context` receives matching capability
booleans (maintenance4/descriptor heap are not separate fields in that initializer).
The requirements object alone does not describe the full feature chain.

Surface modes differ: offscreen requires no present surface; headless requires
`VK_KHR_surface` and `VK_EXT_headless_surface`; window/headless select a present
capable device. Graphics queue is required; compute prefers a separate queue
with graphics fallback, present falls back to graphics. Allocator is made through
`vkexec::vma::factory::make_allocator` from the adopted context.
Optional present timing queries device features AND surface support for calibrated
timestamps, `VK_EXT_present_timing` and `VK_KHR_present_id2`; optional Vulkan Video
adds queue/encode/H.264 extensions following capability probing. These optional
paths are separate from the canonical benchmark settings. Runtime capabilities
and validation evidence are in [the baseline](vkexec-baseline.md).

## All imported headers

Each row includes every importing file and line. All are public entry points;
`vkexec_extensions` and `vkexec_vma` require their separate CMake module targets.
Header names indicate their purpose (context, errors, pipelines, recording,
synchronization, resource schema/table, heap descriptors, frame ring or allocation).

| Public header | Purpose | Import locations |
| --- | --- | --- |
| `vkexec/barrier.hpp` | Explicit memory and image synchronization | `src/vkgsplat_backend/vulkan/capture/readback.cpp:23`<br>`src/vkgsplat_backend/vulkan/gs/operations.cpp:21`<br>`src/vkgsplat_backend/vulkan/gs/pipeline.cpp:11`<br>`src/vkgsplat_backend/vulkan/gs/projection.cpp:20`<br>`src/vkgsplat_backend/vulkan/gs/rasterization.cpp:7`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:24`<br>`src/vkgsplat_backend/vulkan/imgui_overlay.cpp:36` |
| `vkexec/context.hpp` | Borrowed Vulkan context adoption and access | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:19`<br>`src/vkgsplat_backend/vulkan/imgui_overlay.cpp:37`<br>`src/vkgsplat_backend/vulkan/vulkan_context.hpp:11`<br>`src/vkgsplat_backend/vulkan/vulkan_driver.cpp:23` |
| `vkexec/descriptor_schema.hpp` | Typed shader resource schema | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:20` |
| `vkexec/device_capabilities.hpp` | Capabilities supplied at context adoption | `src/vkgsplat_backend/vulkan/vulkan_driver.cpp:24` |
| `vkexec/error.hpp` | Vulkan error type/category | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:7`<br>`src/vkgsplat_backend/vulkan/vkexec_error.hpp:6`<br>`test/render_thread.cpp:13` |
| `vkexec/image_view.hpp` | Owned color image view | `src/vkgsplat_backend/vulkan/app_state.hpp:26`<br>`src/vkgsplat_backend/vulkan/gs/rasterization.cpp:24` |
| `vkexec/pass.hpp` | Pass recording and dispatch | `src/vkgsplat_backend/vulkan/gs/operations.cpp:22`<br>`src/vkgsplat_backend/vulkan/gs/projection.cpp:21`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:25` |
| `vkexec/pipeline.hpp` | Pipeline layout declarations | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:21`<br>`src/vkgsplat_backend/vulkan/gs/load_heap_pipeline.hpp:7` |
| `vkexec/queue_submit.hpp` | Command buffer and semaphore submission descriptors | `src/vkgsplat_backend/vulkan/capture/readback.cpp:24`<br>`src/vkgsplat_backend/vulkan/render_thread.cpp:44` |
| `vkexec/resource_table.hpp` | Resource references and typed table construction | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:22` |
| `vkexec/sync_wait.hpp` | Synchronous factory result extraction | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:8`<br>`src/vkgsplat_backend/vulkan/gs/graphics/graphics_pipeline.cpp:16`<br>`src/vkgsplat_backend/vulkan/gs/load_heap_pipeline.hpp:8`<br>`src/vkgsplat_backend/vulkan/gs/rasterization.cpp:25`<br>`src/vkgsplat_backend/vulkan/renderer.cpp:29`<br>`src/vkgsplat_backend/vulkan/sphere_setup.cpp:7`<br>`src/vkgsplat_backend/vulkan/vulkan_driver.cpp:25` |
| `vkexec/tensor.hpp` | Typed GPU tensor storage | `src/vkgsplat_backend/vulkan/app_state.hpp:27`<br>`src/vkgsplat_backend/vulkan/sphere_setup.cpp:8` |
| `vkexec/vulkan_requirements.hpp` | Version/extension requirements | `src/vkgsplat_backend/vulkan/vkexec_requirements.cpp:3`<br>`src/vkgsplat_backend/vulkan/vkexec_requirements.hpp:4` |
| `vkexec_extensions/descriptor_heap/algorithm.hpp` | Compute algorithm/pipeline wrapper | `src/vkgsplat_backend/vulkan/app_state.hpp:28`<br>`src/vkgsplat_backend/vulkan/gs/load_heap_pipeline.hpp:9`<br>`src/vkgsplat_backend/vulkan/gs/operations.cpp:23`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:26` |
| `vkexec_extensions/descriptor_heap/buffer.hpp` | Aligned descriptor heap backing buffer | `src/vkgsplat_backend/vulkan/app_state.hpp:29`<br>`src/vkgsplat_backend/vulkan/gpu_buffers.hpp:9`<br>`src/vkgsplat_backend/vulkan/imgui_overlay.cpp:38` |
| `vkexec_extensions/descriptor_heap/descriptor_heap.hpp` | Heap layout queries, descriptor writers and heap binding | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:23`<br>`src/vkgsplat_backend/vulkan/imgui_overlay.cpp:39` |
| `vkexec_extensions/descriptor_heap/heap_graphics_pipeline.hpp` | Descriptor heap graphics pipeline factory and ownership | `src/vkgsplat_backend/vulkan/app_state.hpp:30`<br>`src/vkgsplat_backend/vulkan/gs/graphics/graphics_pipeline.cpp:17` |
| `vkexec_extensions/descriptor_heap/pass.hpp` | Pass recording and dispatch | `src/vkgsplat_backend/vulkan/gs/operations.cpp:24`<br>`src/vkgsplat_backend/vulkan/gs/projection.cpp:22`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:27` |
| `vkexec_extensions/descriptor_heap/push_data.hpp` | Heap pipeline push-data commands | `src/vkgsplat_backend/vulkan/gs/rasterization.cpp:26` |
| `vkexec_extensions/descriptor_heap/strategy.hpp` | Descriptor heap pipeline strategy | `src/vkgsplat_backend/vulkan/gs/load_heap_pipeline.hpp:10` |
| `vkexec_extensions/timeline_semaphore/frame_ring.hpp` | Timeline frame-slot and image completion tracking | `src/vkgsplat_backend/vulkan/app_state.hpp:31`<br>`src/vkgsplat_backend/vulkan/render_thread.cpp:45`<br>`src/vkgsplat_backend/vulkan/renderer.cpp:30` |
| `vkexec_vma/allocator.hpp` | VMA allocator creation and ownership | `src/vkgsplat_backend/vulkan/vulkan_context.hpp:12`<br>`src/vkgsplat_backend/vulkan/vulkan_driver.cpp:26` |
| `vkexec_vma/gpu_buffer.hpp` | VMA buffer allocation, mapping, flush and device address | `src/vkgsplat_backend/vulkan/capture/readback.cpp:25`<br>`src/vkgsplat_backend/vulkan/gpu_buffers.hpp:10` |
| `vkexec_vma/image.hpp` | VMA image allocation and ownership | `src/vkgsplat_backend/vulkan/app_state.hpp:32`<br>`src/vkgsplat_backend/vulkan/gs/rasterization.cpp:27` |

## Member calls not visible in the qualified-symbol search

| Call / source | Contract to preserve in PR 02 |
| --- | --- |
| `owns_instance()`, `owns_device()` — `vulkan_driver.cpp:82` | Adoption must not transfer raw handle ownership. |
| `handle()`, `size()`, `mapped()`, `flush()`, `device_address()` — `gpu_buffers.hpp`, `descriptor/descriptor_heap.cpp` | Vulkan handles and mapped spans borrow the owning buffer; flush before GPU reads; validate BDA and storage range. |
| `vk_buffer()`, `byte_size()`, `device()` — `descriptor/descriptor_heap.cpp:175` | Tensor's device allocation stays alive through descriptor use. |
| `entries()` — `descriptor/descriptor_heap.cpp:211` | Table iteration retains schema order matching physical heap indices/device addresses. |
| `wait_slot()` — `render_thread.cpp:129,134`; `wait_image()` — `render_thread.cpp:149` | Completion before current/previous slot and acquired image reuse. |
| `allocate_signal_value()`, `make_submit_sync()` — `render_thread.cpp:165` | Monotonic timeline allocation and correct acquire/presentation synchronization. |
| `submit(queue_submit)` — `render_thread.cpp:170,1176`, `capture/readback.cpp:173` | Borrowed command/wait/signal spans valid during submission; submit to intended graphics queue. |
| `acquire_semaphore()`, `render_finished_semaphore()` — `render_thread.cpp:1075,1098` | Binary acquire/present semaphore handles remain valid through presentation. |
| `mark_submitted()` — `render_thread.cpp:180` | Associate slot/image with completion value only after successful submit. |

## All qualified API references

This exhaustive index includes types, constants, factories and CMake aliases;
member calls and ownership requirements are described above. Use the source at
the frozen revision for complete multiline argument lists. Every symbol is a
PR 02 compile-compatibility checkpoint; behavioral checks are grouped above.

| Exact qualified symbol | Source locations |
| --- | --- |
| `vkexec::algorithm` | `src/vkgsplat_backend/vulkan/app_state.hpp:98`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:99`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:100`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:101`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:102`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:103`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:104`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:105`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:106`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:107`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:108`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:109`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:110`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:111`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:112`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:113`<br>`src/vkgsplat_backend/vulkan/app_state.hpp:114`<br>`src/vkgsplat_backend/vulkan/gs/load_heap_pipeline.hpp:25`<br>`src/vkgsplat_backend/vulkan/gs/operations.cpp:64`<br>`src/vkgsplat_backend/vulkan/gs/operations.cpp:81`<br>`src/vkgsplat_backend/vulkan/gs/operations.cpp:153`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:146`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:159`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:293` |
| `vkexec::barrier::compute_read` | `src/vkgsplat_backend/vulkan/gs/operations.cpp:125`<br>`src/vkgsplat_backend/vulkan/gs/operations.cpp:222`<br>`src/vkgsplat_backend/vulkan/gs/projection.cpp:67` |
| `vkexec::barrier::compute_to_compute` | `src/vkgsplat_backend/vulkan/gs/operations.cpp:146`<br>`src/vkgsplat_backend/vulkan/gs/operations.cpp:160`<br>`src/vkgsplat_backend/vulkan/gs/pipeline.cpp:188`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:138` |
| `vkexec::barrier::compute_to_graphics` | `src/vkgsplat_backend/vulkan/gs/rasterization.cpp:207` |
| `vkexec::barrier::graphics_to_compute` | `src/vkgsplat_backend/vulkan/gs/pipeline.cpp:119`<br>`src/vkgsplat_backend/vulkan/gs/pipeline.cpp:185` |
| `vkexec::barrier::transfer_to_compute` | `src/vkgsplat_backend/vulkan/gs/operations.cpp:248`<br>`src/vkgsplat_backend/vulkan/gs/projection.cpp:107`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:287`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:385`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:542` |
| `vkexec::blend_mode::premultiplied` | `src/vkgsplat_backend/vulkan/gs/graphics/graphics_pipeline.cpp:63` |
| `vkexec::buffer_resource` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:172`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:173`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:174`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:175`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:176`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:177`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:178`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:179`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:180`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:181`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:183`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:184` |
| `vkexec::cmd_bind_resource_heap` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:350` |
| `vkexec::cmd_bind_sampler_heap` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:359` |
| `vkexec::cmd_push_data` | `src/vkgsplat_backend/vulkan/gs/rasterization.cpp:110` |
| `vkexec::context` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:29`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:120`<br>`src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:155`<br>`src/vkgsplat_backend/vulkan/imgui_overlay.cpp:62`<br>`src/vkgsplat_backend/vulkan/vulkan_context.hpp:31` |
| `vkexec::descriptor_heap` | `src/vkgsplat_backend/vulkan/gs/load_heap_pipeline.hpp:48` |
| `vkexec::descriptor_heap_buffer` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:25` |
| `vkexec::descriptor_heap_byte_size` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:307` |
| `vkexec::descriptor_schema` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:40` |
| `vkexec::device_capabilities` | `src/vkgsplat_backend/vulkan/vulkan_driver.cpp:62` |
| `vkexec::dispatch` | `src/vkgsplat_backend/vulkan/gs/operations.cpp:95`<br>`src/vkgsplat_backend/vulkan/gs/operations.cpp:108`<br>`src/vkgsplat_backend/vulkan/gs/projection.cpp:115`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:148`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:195`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:221`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:400`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:425`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:447`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:510`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:635` |
| `vkexec::dispatch_groups_for` | `src/vkgsplat_backend/vulkan/gs/operations.cpp:100` |
| `vkexec::error` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:32`<br>`src/vkgsplat_backend/vulkan/vkexec_error.hpp:12`<br>`test/render_thread.cpp:683` |
| `vkexec::ext_descriptor_heap` | `src/vkgsplat_backend/CMakeLists.txt:83` |
| `vkexec::ext_timeline_semaphore` | `src/vkgsplat_backend/CMakeLists.txt:84` |
| `vkexec::factory::adopt_context` | `src/vkgsplat_backend/vulkan/vulkan_driver.cpp:57` |
| `vkexec::factory::make_compute_pipeline` | `src/vkgsplat_backend/vulkan/gs/load_heap_pipeline.hpp:48` |
| `vkexec::factory::make_descriptor_graphics_pipeline` | `src/vkgsplat_backend/vulkan/gs/graphics/graphics_pipeline.cpp:70` |
| `vkexec::factory::make_descriptor_heap_buffer` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:71` |
| `vkexec::factory::make_frame_ring` | `src/vkgsplat_backend/vulkan/renderer.cpp:94` |
| `vkexec::factory::make_tensor` | `src/vkgsplat_backend/vulkan/sphere_setup.cpp:30`<br>`src/vkgsplat_backend/vulkan/sphere_setup.cpp:32` |
| `vkexec::frame_ring_create_info` | `src/vkgsplat_backend/vulkan/renderer.cpp:95` |
| `vkexec::heap_graphics_layout_desc` | `src/vkgsplat_backend/vulkan/gs/graphics/graphics_pipeline.cpp:61` |
| `vkexec::heap_layout_desc` | `src/vkgsplat_backend/vulkan/gs/load_heap_pipeline.hpp:43` |
| `vkexec::image_barrier` | `src/vkgsplat_backend/vulkan/capture/readback.cpp:83`<br>`src/vkgsplat_backend/vulkan/gs/rasterization.cpp:183`<br>`src/vkgsplat_backend/vulkan/gs/rasterization.cpp:254`<br>`src/vkgsplat_backend/vulkan/gs/rasterization.cpp:278`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:619`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:639`<br>`src/vkgsplat_backend/vulkan/imgui_overlay.cpp:703`<br>`src/vkgsplat_backend/vulkan/imgui_overlay.cpp:729`<br>`src/vkgsplat_backend/vulkan/imgui_overlay.cpp:750`<br>`src/vkgsplat_backend/vulkan/imgui_overlay.cpp:793` |
| `vkexec::indirect_dispatch` | `src/vkgsplat_backend/vulkan/gs/operations.cpp:89`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:167` |
| `vkexec::k_default_local_size` | `src/vkgsplat_backend/vulkan/gs/load_heap_pipeline.hpp:25` |
| `vkexec::make_resource_table` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:171` |
| `vkexec::make_vk_error_code` | `test/render_thread.cpp:684` |
| `vkexec::owned::descriptor_graphics_pipeline` | `src/vkgsplat_backend/vulkan/app_state.hpp:93` |
| `vkexec::owned::frame_ring` | `src/vkgsplat_backend/vulkan/app_state.hpp:148`<br>`src/vkgsplat_backend/vulkan/render_thread.cpp:125`<br>`src/vkgsplat_backend/vulkan/render_thread.cpp:145` |
| `vkexec::owned::image_view` | `src/vkgsplat_backend/vulkan/app_state.hpp:96` |
| `vkexec::query_descriptor_heap_layout` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:290` |
| `vkexec::queue_submit` | `src/vkgsplat_backend/vulkan/capture/readback.cpp:173`<br>`src/vkgsplat_backend/vulkan/render_thread.cpp:170`<br>`src/vkgsplat_backend/vulkan/render_thread.cpp:1176` |
| `vkexec::record_pass` | `src/vkgsplat_backend/vulkan/gs/operations.cpp:69`<br>`src/vkgsplat_backend/vulkan/gs/operations.cpp:85`<br>`src/vkgsplat_backend/vulkan/gs/operations.cpp:140`<br>`src/vkgsplat_backend/vulkan/gs/projection.cpp:61`<br>`src/vkgsplat_backend/vulkan/gs/projection.cpp:111`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:150`<br>`src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.cpp:163` |
| `vkexec::resource_kind::storage_image` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:211` |
| `vkexec::resource_ref` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:124` |
| `vkexec::sampler_heap_byte_size` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:312` |
| `vkexec::semaphore_submit` | `src/vkgsplat_backend/vulkan/capture/readback.cpp:167`<br>`src/vkgsplat_backend/vulkan/render_thread.cpp:1170` |
| `vkexec::storage_buffer` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:38` |
| `vkexec::storage_image` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:50` |
| `vkexec::storage_image_resource` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:182` |
| `vkexec::tensor` | `src/vkgsplat_backend/vulkan/app_state.hpp:43` |
| `vkexec::try_sync_wait_value` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:40`<br>`src/vkgsplat_backend/vulkan/gpu_buffers.hpp:71`<br>`src/vkgsplat_backend/vulkan/gs/graphics/graphics_pipeline.cpp:69`<br>`src/vkgsplat_backend/vulkan/gs/load_heap_pipeline.hpp:47`<br>`src/vkgsplat_backend/vulkan/gs/rasterization.cpp:80`<br>`src/vkgsplat_backend/vulkan/gs/rasterization.cpp:95`<br>`src/vkgsplat_backend/vulkan/renderer.cpp:94`<br>`src/vkgsplat_backend/vulkan/sphere_setup.cpp:30`<br>`src/vkgsplat_backend/vulkan/sphere_setup.cpp:32`<br>`src/vkgsplat_backend/vulkan/vulkan_driver.cpp:57`<br>`src/vkgsplat_backend/vulkan/vulkan_driver.cpp:87` |
| `vkexec::vkexec` | `src/vkgsplat_backend/CMakeLists.txt:81` |
| `vkexec::vma` | `src/vkgsplat_backend/CMakeLists.txt:82` |
| `vkexec::vma::allocator` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:35`<br>`src/vkgsplat_backend/vulkan/gpu_buffers.hpp:53`<br>`src/vkgsplat_backend/vulkan/gpu_buffers.hpp:58`<br>`src/vkgsplat_backend/vulkan/gpu_buffers.hpp:63`<br>`src/vkgsplat_backend/vulkan/gpu_buffers.hpp:67`<br>`src/vkgsplat_backend/vulkan/vulkan_context.hpp:32`<br>`src/vkgsplat_backend/vulkan/vulkan_driver.cpp:91` |
| `vkexec::vma::factory::make_allocator` | `src/vkgsplat_backend/vulkan/vulkan_driver.cpp:87` |
| `vkexec::vma::factory::make_gpu_buffer` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:40` |
| `vkexec::vma::factory::make_image` | `src/vkgsplat_backend/vulkan/gs/rasterization.cpp:80` |
| `vkexec::vma::factory::make_image_view` | `src/vkgsplat_backend/vulkan/gs/rasterization.cpp:95` |
| `vkexec::vma::gpu_buffer` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:24` |
| `vkexec::vma::gpu_buffer_create_info` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:41` |
| `vkexec::vma::gpu_buffer_memory` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:37` |
| `vkexec::vma::gpu_buffer_memory::device_local` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:60` |
| `vkexec::vma::gpu_buffer_memory::host_visible` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:55` |
| `vkexec::vma::gpu_buffer_memory::staging` | `src/vkgsplat_backend/vulkan/gpu_buffers.hpp:65` |
| `vkexec::vma::image` | `src/vkgsplat_backend/vulkan/app_state.hpp:41` |
| `vkexec::vma::image_create_info` | `src/vkgsplat_backend/vulkan/gs/rasterization.cpp:81` |
| `vkexec::vma::image_usage::color_storage` | `src/vkgsplat_backend/vulkan/gs/rasterization.cpp:84` |
| `vkexec::vulkan_category` | `src/vkgsplat_backend/vulkan/vkexec_error.hpp:15` |
| `vkexec::vulkan_requirements` | `src/vkgsplat_backend/vulkan/vkexec_requirements.cpp:8`<br>`src/vkgsplat_backend/vulkan/vkexec_requirements.cpp:10`<br>`src/vkgsplat_backend/vulkan/vkexec_requirements.hpp:11` |
| `vkexec::write_sampled_image_descriptor` | `src/vkgsplat_backend/vulkan/imgui_overlay.cpp:125`<br>`src/vkgsplat_backend/vulkan/imgui_overlay.cpp:427` |
| `vkexec::write_sampler_descriptor` | `src/vkgsplat_backend/vulkan/imgui_overlay.cpp:159`<br>`src/vkgsplat_backend/vulkan/imgui_overlay.cpp:440` |
| `vkexec::write_storage_buffer_descriptor` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:147` |
| `vkexec::write_storage_image_descriptor` | `src/vkgsplat_backend/vulkan/descriptor/descriptor_heap.cpp:243` |
