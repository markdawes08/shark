# Direct3D 12 Presentation and Frame-Resource Contract

- **Completed through:** `S-002`
- **Last verified:** July 16, 2026

G-002 produced Shark's first pixels through a resize-safe Direct3D 12
flip-model swap chain. G-003 replaces its wait-after-every-present path with
three fence-gated reusable frame contexts and bounded transient staging. G-004
adds the first build-time HLSL pipeline. G-005 preserves those lifecycles while
adding the engine camera, one indexed textured cube, the first shader-visible
SRV binding, and a resize-safe reversed-Z depth target. G-006 preserves the
same visible frame while declaring its back-buffer/depth use and centralizing
its attachment transitions through a frame-local render graph. G-007 adds
named PIX command-list events and fence-delayed direct-queue timestamps without
changing the visible frame or adding a normal-frame wait. S-001 preserves that
frame contract while loading and uploading the first file-backed DDS cubemap
and reserving its persistent SRV. S-002 renders that resource through a second
named graph pass with rotation-only camera constants and read-only depth.

## Public boundary and ownership

`shark::rhi::d3d12::Presentation` is a move-only PIMPL. Its public header uses
an opaque native-window pointer and engine-owned extent, color,
`PresentationFrameData`, status, and statistics records plus pointer/size
shader and generic texture-cube upload views; it exposes no Win32, DXGI,
D3D12, WRL, or COM types. `PresentationFrameData` contains finite engine-owned
row-major `view_projection` and `sky_view_projection` matrices, which the
per-frame call borrows while recording the cube-and-sky frame.

The internal `detail::DeviceAccess` bridge borrows the authoritative D3D12
device and DXGI factory from `Device`. It does not transfer ownership. The
required lifetime order is:

```text
construct: Device -> Application/HWND -> Presentation
destroy:   Presentation -> Application/HWND -> Device
```

The sandbox calls the result-returning, idempotent `Presentation::shutdown`
before destroying the HWND. The destructor is a fallback that logs a cleanup
failure but cannot replace explicit validation.

## Swap chain and physical extent

The presentation object owns:

- one direct command queue;
- one 18-entry timestamp query heap and one persistently mapped 144-byte
  readback buffer partitioned into three fixed context slices;
- one three-buffer `DXGI_SWAP_EFFECT_FLIP_DISCARD` swap chain;
- three `DXGI_FORMAT_R8G8B8A8_UNORM` back-buffer references;
- one CPU-only RTV heap with three descriptors;
- one CPU-only DSV heap with writable and read-only descriptors plus one
  `DXGI_FORMAT_D32_FLOAT` depth texture matching the current physical extent;
- one reusable graphics command list;
- one resource-bound root signature and immutable cube and skybox graphics PSOs;
- one committed default-heap cube vertex buffer containing 24 position/UV
  vertices and one committed default-heap index buffer containing 36
  `uint16_t` indices;
- one committed default-heap `8x8` one-mip
  `DXGI_FORMAT_R8G8B8A8_UNORM` procedural checker texture;
- one committed default-heap `8x8`, one-mip, six-face
  `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` startup cubemap;
- one persistent two-slot shader-visible CBV/SRV/UAV heap holding the checker
  2D SRV at slot 0 and cubemap SRV at slot 1;
- three frame contexts, each containing one direct command allocator, one
  persistently mapped 64 KiB committed upload buffer, one 256-byte default-heap
  upload-probe destination, one CPU-only 64-slot CBV/SRV/UAV staging heap, a
  checked upload cursor, a checked descriptor cursor, a checked six-query
  timestamp cursor, a generation, one direct-fence completion value, and
  pending-result metadata; and
- one fence plus one auto-reset Windows event.

The DXGI factory is associated with the HWND using `DXGI_MWA_NO_ALT_ENTER`.
Every significant D3D12 object is named. The platform manifests establish
Per-Monitor DPI Awareness v2 before HWND creation, so the initial and resized
swap-chain extents are the same physical client-pixel dimensions published by
`Application`. Presentation rejects zero extents and dimensions above the D3D12
2D-resource limit before they can reach swap-chain or viewport state.

DXGI's current back-buffer index selects the context; the CPU frame count is
never used as a substitute. One monotonic fence timeline covers all direct-queue
submissions. Fence value zero means that a context has never been submitted or
has already retired.

## Frame acquisition, staging, and present

Presentation creation records one direct-queue static upload submission for the
cube vertex/index data, checker, and every cubemap face/mip. It transitions the
immutable resources to their final shader/input states, signals the normal
monotonic fence, performs one bounded startup wait, and releases the temporary
upload resources before the first frame. No static geometry or texture upload
occurs in the frame loop.

One frame performs the following work:

1. read DXGI's current back-buffer index and select that context;
2. inspect its preceding completion value and wait only when that context is
   still in flight;
3. consume its pending GPU timing sample only if the preceding completion
   fence is complete, then retire the submission and reset its allocator-facing
   upload, descriptor, and timestamp cursors;
4. reserve one 256-byte frame record, write the 64-byte `view_projection` and
   64-byte `sky_view_projection` followed by the retained diagnostic probe,
   create one CBV in the
   context's CPU-only staging heap, and point the root CBV at the same GPU
   upload address;
5. reserve the context's exact six-query timestamp slice;
6. build and compile one frame-local graph that imports back buffer, depth,
   checker, and cubemap; declares exact checker/cubemap pixel-shader reads; and
   orders `TexturedCube` before the depth-reading `Skybox` pass;
7. reset that context's command allocator and the shared command list with the
   immutable cube PSO;
8. begin the `Frame` PIX event, write the frame-begin timestamp, and copy the
   256-byte frame record from the upload heap to the context's
   default-heap probe destination on the direct queue, outside the graph;
9. execute the graph, which records `PRESENT -> RENDER_TARGET`, invokes
   `TexturedCube`, transitions depth `DEPTH_WRITE -> DEPTH_READ`, invokes
   `Skybox` with the read-only DSV, then restores color/depth final states;
10. write the frame-end timestamp, resolve all six query values to the
    context's readback slice, end `Frame`, then close and execute the command
    list;
11. immediately signal the next monotonic fence value, store it on the
   context; and
12. mark that context's timing results pending and call `Present` without an
    unconditional post-frame fence wait.

The root CBV makes upload-memory reuse genuinely GPU-fence-sensitive, and the
copy retains the G-003 default-heap probe. Probe content is not read back. The
staged CBV continues to exercise bounded CPU descriptor addressing and
exhaustion policy, but the shader binds the same record through the root CBV;
the staged descriptor is not copied into the persistent texture heap. Its
cursor follows the same conservative frame retirement boundary.

Each probe destination is created in `D3D12_RESOURCE_STATE_COMMON`.
`CopyBufferRegion` uses Direct3D 12 buffer promotion to `COPY_DEST`, and the
buffer decays to `COMMON` after the command-list execution boundary. The
per-context fence completes before either side is reused.

Interactive presentation uses `Present(1, 0)` to synchronize to vertical
refresh. The bounded smoke uses `Present(0, 0)` and requests no tearing flag.
Only `S_OK` increments `presented_frames`; `DXGI_STATUS_OCCLUDED` is reported
separately and never satisfies the run's successful-present gate. An occluded
attempt still has a signaled context checkpoint because its command list was
submitted.
Vertical synchronization may block inside `Present`, and context reuse may
perform a bounded fence wait; G-003 removes only the unconditional queue drain
after every frame.

The generated shader byte arrays and CPU cubemap subresource views are borrowed
only during synchronous creation. Presentation owns neither source code,
shader artifacts, nor caller CPU pixels afterward. The root signature, PSOs,
cube buffers, checker, cubemap, and persistent texture heap remain valid across
swap-chain resize and are released only after shutdown drains and retires every
submitted frame.

G-006's graph executor owns the per-frame whole-resource legacy transition
barriers. It resolves the current imported resources immediately before graph
execution and verifies that graph, executor, and recorder transition counts
agree. Static-upload barriers remain in the focused startup upload path, and
the diagnostic probe continues to use D3D12 buffer promotion/decay. Enhanced
barriers remain a later capability-gated graph backend.

## PIX events and GPU timestamps

G-007 privately links the pinned WinPixEventRuntime and enables its retail event
path in both Debug and Release. The command list contains four balanced,
stable names:

```text
StaticCubeUpload
Frame
  TexturedCube
  Skybox
```

`StaticCubeUpload` surrounds the one-time vertex, index, checker, and six
cubemap copies plus four initialization barriers. Each submitted `Frame` begins
after command-list
reset and encloses the diagnostic probe copy, graph barriers, both passes, and
timestamp resolve. Each nested marker contains only its callback's bind/draw
work; `TexturedCube` also owns the attachment clears.

The direct queue's GPU tick frequency is queried once. The global query/readback
allocation is split exactly:

```text
context index       0       1       2
query base          0       6      12
readback bytes      0      48      96
```

Each six-query slice stores frame begin, cube begin/end, sky begin/end, and
frame end. Frame duration includes the probe copy, all four graph barriers, and
both passes, but excludes `ResolveQueryData`. Pass durations exclude graph
barriers. Resolved samples must preserve that exact nested/sequential order;
equal timestamps are valid.

The persistently mapped readback memory is never inspected immediately after
submission. A context is marked pending only after its direct-queue submission
is fence tracked. Its results are consumed immediately before normal context
retirement/reset, either when that context is reused after its existing
completion check or when resize/shutdown drains the queue. No timing-only wait,
drain, readback allocation, or resource barrier is added.

## Resize, minimize, and shutdown

`WindowResizedEvent` carries the latest usable nonzero client extent. If it
differs from the swap chain, presentation:

1. drains its direct queue;
2. consumes every fence-complete pending GPU timing sample, then retires every
   context's completed frame submission;
3. releases all three back-buffer references and the old depth texture;
4. calls `ResizeBuffers` with the new physical extent; and
5. reacquires, renames, and recreates the three RTVs, then creates and names a
   matching `D32_FLOAT` depth texture plus writable and read-only DSVs.

A minimized window publishes no zero render extent. The sandbox stops
updating or presenting the camera while minimized and resumes from the restored
resize event. A duplicate restore extent is a no-op. Shutdown drains the queue,
consumes and retires every outstanding frame submission, verifies every
submission retired, releases every back buffer, the depth texture, cube
resources, checker/cubemap texture heap, pipeline objects, timestamp
query/readback storage, and all context-owned resources, closes the fence
event, and remains safe to call again. The fixed smoke then verifies that every
submission produced one timing sample.

A successful fixed smoke has one bounded static-upload startup wait plus one
full-queue drain per effective resize and one at shutdown. Normal frames still
perform no unconditional wait. The startup fence wait is not a
`full_queue_drains` event.

## Failure and diagnostics contract

Every result-returning D3D12, DXGI, fence, event, resize, and present operation
is checked. Fence waiting is bounded and periodically queries the device-removal
reason instead of waiting forever. A removal-class failure logs the triggering
HRESULT and removal reason, then emits bounded DRED automatic-breadcrumb nodes,
breadcrumb contexts, page-fault address, existing allocations, and recently
freed allocations.

Upload, descriptor, and timestamp allocation is linear and fixed-capacity.
Zero-size, invalid-alignment, overflow, and exhaustion requests return
structured errors without advancing a cursor. Timestamp decoding rejects an
incomplete or non-nested sample, accepts zero-length intervals, and checks
aggregate overflow before mutation. Unit tests also prove an incomplete
context refuses retirement or reset and that the fence timeline fails before
wraparound.

After explicit presentation shutdown, `Device::validate_debug_state` reports
live D3D12 device children and inspects D3D12 and DXGI info-queue messages added
since their preceding cursors. Corruption, errors, discarded bounded-queue
messages, or live presentation children fail the process. Warnings remain
visible and are not reclassified as errors.

## Fixed presentation smoke

The noninteractive contract is:

```powershell
.\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke
.\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp
.\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp --gpu-validation
```

The frame count is selected by validation mode and is not user-configurable.
Hardware and normal packaged WARP require 1,000 successful presents. Focused
packaged WARP with GPU-based validation requires 120, has a 180-second internal
deadline, and is registered with a 240-second CTest timeout. A successful run:

- shows a real nonactivating window;
- reaches its quarter checkpoint (250 normal or 30 focused), changes the
  physical client area to `960x600`, verifies the presentation and depth
  extents follow it, and proves the projection aspect changed from the initial
  `1280x720`;
- on the 1,000-frame paths, reaches frame 500, minimizes, proves the count does
  not advance, and observes restore followed by its resize event; the focused
  120-frame path intentionally skips this already-covered interval;
- changes camera yaw at its three-quarter checkpoint (750 normal or 90
  focused);
- reaches its exact successful-present target (1,000 normal or 120 focused);
- proves all three DXGI-selected contexts were acquired and reused;
- matches `frame_context_acquisitions`, `camera_constant_updates`,
  `upload_allocations`, `descriptor_allocations`, and fence-tracked
  `frame_submissions` to successful plus occluded present attempts;
- proves one cube and one skybox draw, 36 indices for each, one depth clear,
  and two texture bindings per frame submission;
- proves one graph compilation/execution, four imports, two pass executions,
  one dependency, four recorded transitions, and six elided transitions per
  frame submission;
- proves one `StaticCubeUpload` PIX event, one `Frame` event per submission,
  plus one `TexturedCube` and one `Skybox` event per submission;
- proves global timestamp capacity is 18, per-context high-water is six,
  exactly six queries and one resolve are recorded per submission, and
  shutdown consumes one frame/cube/sky timing sample per retired
  submission;
- reports average and maximum frame/cube/sky milliseconds using the direct queue's
  nonzero GPU timestamp frequency without imposing a duration threshold;
- retires every submission by explicit shutdown;
- verifies one 256-byte GPU-consumed upload and one CPU staging descriptor per
  attempt, with high-water marks of 256 bytes and one descriptor;
- proves `static_upload_submissions == 1`,
  `geometry_buffer_creations == 2`, `checker_texture_creations == 1`, and
  `cubemap_texture_creations == 1`;
- proves six faces, one mip, six subresources, and 1,536 source bytes were
  uploaded, with `texture_srv_creations == 2`,
  `cubemap_srv_creations == 1`, and two persistent texture descriptors; the
  static upload completes its bounded wait before the first frame;
- proves both `depth_resource_creations` and `depth_read_view_creations` equal
  `resize_count + 1`, and
  `full_queue_drains == resize_count + 1` for effective resizes plus shutdown,
  while reporting the hardware-dependent context-reuse wait count without
  constraining it;
- proves scene and sky matrix-change counts are each at least three for the
  initial frame, aspect-changing resize, and scripted yaw at the
  three-quarter checkpoint;
- on paths that exercise minimization, verifies no cube/sky draw, texture
  binding, camera upload, depth clear, PIX frame/pass event, timestamp
  write/resolve, or timing-sample consumption occurs while minimized;
- explicitly shuts down and validates presentation children;
- posts and accepts the native close request; and
- observes final HWND destruction.

Early close, dropped lifecycle events, occlusion that prevents progress,
deadline expiry, a dimension mismatch, any graphics failure, incomplete close,
or a final count other than the selected 1,000/120 target returns a nonzero
process exit code.
The smoke validates compilation, resource binding, indexed draw and depth
submission, aspect propagation, lifetime, and diagnostics; it does not read
back or compare the final pixels.

CTest runs hardware, packaged WARP, and packaged WARP with GPU-based validation
as separate serial processes. The focused validation test's 240-second CTest
timeout deliberately exceeds its 180-second application deadline. Debug and
Release must both pass those checks in addition to the device-only,
platform-only, frame-resource, render-graph planner, and D3D12 graph-executor
unit tests.

## Explicit non-goals

S-002 does not expose a public/general upload allocator, global upload ring,
persistent descriptor allocator, generic deferred-destruction system, or
readback/image validation. Its timestamp query heap and readback buffer are a
fixed presentation diagnostic, not a general query allocator or asynchronous
readback service. Its two-slot shader-visible heap, root signature, geometry,
checker, visible cubemap SRV, and two PSOs remain deliberately specific to this
proof, not a general mesh manager, material layout, pipeline cache, or
hot-reload boundary.

The graph remains frame-local, direct-queue, serial, and limited to imported
whole resources. It adds no graph-owned transients, lifetime/aliasing analysis,
subresource tracking, automatic attachment binding, pass culling/merging,
parallel recording, copy queue, asynchronous compute, enhanced barriers,
fullscreen policy, tearing mode, general texture/material loading, mip
generation, streaming, or scene/ECS layer.

G-007 also adds no live HUD, Dear ImGui, capture automation, graph-owned
automatic pass profiling, CPU/GPU clock calibration, stable-power-state
control, pipeline statistics, occlusion queries, multi-queue timing, async
compute, performance thresholds, or timing for the static upload.

See [the HLSL pipeline contract](GRAPHICS_PIPELINE.md) for compilation,
generated artifacts, root-signature/PSO state, and the indexed draw contract.
See [the camera and textured-cube contract](CAMERA_AND_CUBE.md) for coordinate,
input, depth, geometry, texture, and acceptance rules. See
[the minimal render-graph contract](RENDER_GRAPH.md) for declaration,
compilation, execution, barrier, and accounting rules. See
[the GPU diagnostics contract](GPU_DIAGNOSTICS.md) for marker and timing
details plus manual PIX acceptance. See
[the DDS cubemap contract](DDS_CUBEMAP.md) for asset validation, orientation,
deployment, upload, and persistent-SRV rules, and
[the skybox contract](SKYBOX.md) for the visible background acceptance.
