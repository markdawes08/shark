# Renderer and Direct3D 12 Presentation/Frame-Resource Contract

- **Completed through:** `REN-001`
- **Last verified:** July 18, 2026

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
T-001 adds a deterministic height tile as the first opaque pass, a dedicated
terrain root signature, solid/wireframe pipelines, and a depth-tested
diagnostic bounds draw while preserving the cube and sky paths. S-002A changes
that sky to a procedural daylight model, shares its finite sun data with
terrain, and removes the dormant cubemap from the per-frame graph without
removing its startup asset proof. T-002 keeps every rendering resource and
pass boundary while appending a cyan line-list pin derived from the canonical
terrain surface query to the existing terrain buffers and `Terrain` callback.
REN-001 preserves those pixels and all accounting while moving the public
scene/pass boundary and production frame composer out of the D3D12 RHI.

## Public boundary and ownership

`shark::renderer::Renderer` is the move-only public PIMPL. Its public header
defines `RenderExtent`, `RendererConfig`, `RenderFrameData`, `RenderStatus`,
and `RendererStats`, plus COM-free shader and generic texture upload views.
`RendererConfig` carries the opaque native-window pointer, physical extent,
clear color, and borrowed shader-bytecode, cubemap, and terrain upload views,
plus vertical-synchronization policy. `Renderer::create` consumes every
pointer-based view synchronously.

`RenderFrameData` contains finite engine-owned row-major `view_projection` and
`sky_view_projection` matrices plus finite daylight direction, disk, gradient,
ambient, halo, and intensity values, which `Renderer::render_frame` borrows
while recording the terrain/cube/sky frame. It also selects the finite
`TerrainRenderMode` (`solid` or `wireframe`).
`TerrainMeshUploadView` lends interleaved position/normal vertices, triangle
indices, eight-vertex/24-index bounds lines, and six-vertex/six-index query
marker lines only during synchronous creation. The renderer receives
the already-derived marker geometry; it neither owns nor performs terrain
surface queries.

There is no public `Presentation` class. Presentation, resize, and swap-chain
operations still occur inside the private
`engine/renderer/src/d3d12/renderer.cpp` backend. At the sandbox composition
root, `Renderer::create` receives the authoritative
`shark::rhi::d3d12::Device` by reference. The backend uses the private
`rhi::d3d12::detail::DeviceAccess` bridge to borrow its native device and DXGI
factory without transferring ownership. The required lifetime order is:

```text
construct: Device -> Application/HWND -> Renderer
destroy:   Renderer -> Application/HWND -> Device
```

The sandbox calls the result-returning, idempotent `Renderer::shutdown`
before destroying the HWND. The destructor is a fallback that logs a cleanup
failure but cannot replace explicit validation.

REN-001 also makes the source boundary explicit:

- `engine/renderer/src/frame_pipeline.*` owns the production
  `compose_frame_pipeline` policy: seven semantic imports, three pass
  declarations, their access states, and `Terrain -> TexturedCube -> Skybox`;
- `engine/renderer/src/d3d12` privately owns the cube, daylight, skybox, and
  terrain scene helpers, scene-named timestamp layout/accumulator, and D3D12
  renderer backend; and
- `engine/rhi/d3d12/include` retains the public typed `Device`, while
  `engine/rhi/d3d12/src` privately retains the device-access bridge, generic
  frame-resource state machine, and legacy render-graph transition recorder.
  No public scene-pass API remains in the RHI.

The REN-001 acceptance baseline is intentionally exact: seven imports, three
passes, two dependencies, four transitions, 16 elisions, five indexed draws,
four geometry buffers, two textures, the existing PIX/timestamp hierarchy and
eight-query layout, the 256-byte constants/probe record, and every resize,
retirement, shutdown, and smoke count below are unchanged.

## Swap chain and physical extent

The private D3D12 renderer backend owns:

- one direct command queue;
- one 24-entry timestamp query heap and one persistently mapped 192-byte
  readback buffer partitioned into three fixed context slices;
- one three-buffer `DXGI_SWAP_EFFECT_FLIP_DISCARD` swap chain;
- three `DXGI_FORMAT_R8G8B8A8_UNORM` back-buffer references;
- one CPU-only RTV heap with three descriptors;
- one CPU-only DSV heap with writable and read-only descriptors plus one
  `DXGI_FORMAT_D32_FLOAT` depth texture matching the current physical extent;
- one reusable graphics command list;
- one resource-bound cube root signature, one b0-only terrain/sky root
  signature, immutable cube and skybox PSOs, immutable solid and wireframe
  terrain PSOs, and one terrain-bounds line PSO;
- one committed default-heap cube vertex buffer containing 24 position/UV
  vertices and one committed default-heap index buffer containing 36
  `uint16_t` indices;
- one committed default-heap terrain vertex buffer containing the 1,089
  position/normal surface vertices followed by eight bounds vertices and six
  query-marker vertices, and one terrain index buffer containing 6,144 triangle
  indices followed by 24 bounds-line indices and six marker-line indices;
- one committed default-heap `8x8` one-mip
  `DXGI_FORMAT_R8G8B8A8_UNORM` procedural checker texture;
- one committed default-heap `8x8`, one-mip, six-face
  `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` startup cubemap;
- one persistent two-slot shader-visible CBV/SRV/UAV heap holding the checker
  2D SRV at slot 0 and cubemap SRV at slot 1;
- three frame contexts, each containing one direct command allocator, one
  persistently mapped 64 KiB committed upload buffer, one 256-byte default-heap
  upload-probe destination, one CPU-only 64-slot CBV/SRV/UAV staging heap, a
  checked upload cursor, a checked descriptor cursor, a checked eight-query
  timestamp cursor, a generation, one direct-fence completion value, and
  pending-result metadata; and
- one fence plus one auto-reset Windows event.

The DXGI factory is associated with the HWND using `DXGI_MWA_NO_ALT_ENTER`.
Every significant D3D12 object is named. The platform manifests establish
Per-Monitor DPI Awareness v2 before HWND creation, so the initial and resized
swap-chain extents are the same physical client-pixel dimensions published by
`Application`. `Renderer` rejects zero extents and dimensions above the D3D12
2D-resource limit before they can reach swap-chain or viewport state.

DXGI's current back-buffer index selects the context; the CPU frame count is
never used as a substitute. One monotonic fence timeline covers all direct-queue
submissions. Fence value zero means that a context has never been submitted or
has already retired.

## Frame acquisition, staging, and present

Renderer creation records one direct-queue static upload submission for the
cube and terrain vertex/index data, checker, and every cubemap face/mip. Its six
initialization barriers transition the immutable resources to their final
shader/input states, then it signals the normal
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
4. reserve one 256-byte frame record, write the 64-byte `view_projection`,
   64-byte `sky_view_projection`, and six 16-byte daylight rows through byte
   223, write the retained diagnostic probe at byte 224, create one CBV in the
   context's CPU-only staging heap, and point the root CBV at the same GPU
   upload address;
5. reserve the context's exact eight-query timestamp slice;
6. ask the renderer-owned production `frame_pipeline` composer to build and
   compile one frame-local graph that imports back buffer, depth, checker, cube
   vertex/index buffers, and terrain vertex/index buffers; declares their
   exact shader/input reads; and orders `Terrain`, `TexturedCube`, then the
   depth-reading procedural `Skybox`;
7. reset that context's command allocator and the shared command list with the
   immutable solid-terrain PSO;
8. begin the `Frame` PIX event, write the frame-begin timestamp, and copy the
   256-byte frame record from the upload heap to the context's
   default-heap probe destination on the direct queue, outside the graph;
9. execute the graph, which records `PRESENT -> RENDER_TARGET`, invokes
   `Terrain` to clear and draw the selected surface, bounds, and query marker,
   invokes `TexturedCube` without clearing, transitions depth
   `DEPTH_WRITE -> DEPTH_READ`, invokes the b0-only procedural `Skybox` with the
   read-only DSV, then restores color/depth final states;
10. write the frame-end timestamp, resolve all eight query values to the
    context's readback slice, end `Frame`, then close and execute the command
    list;
11. immediately signal the next monotonic fence value, store it on the
    context; and
12. mark that context's timing results pending and call `Present` without an
    unconditional post-frame fence wait.

The root-CBV record has this exact layout:

```text
0..63     view_projection
64..127   sky_view_projection
128..143  direction_to_sun.xyz, sun_disk_outer_cosine
144..159  sun_color.xyz, sun_disk_inner_cosine
160..175  zenith_color.xyz, sky_gradient_exponent
176..191  horizon_color.xyz, ambient_strength
192..207  nadir_color.xyz, sun_halo_outer_cosine
208..223  sky_ambient_color.xyz, sun_intensity
224..255  FrameProbe
```

The root CBV exposes 224 bytes of matrices and daylight data, makes
upload-memory reuse genuinely GPU-fence-sensitive, and the copy retains the
G-003 default-heap probe at byte 224. Probe content is not read back. The
staged CBV continues to exercise bounded CPU descriptor addressing and
exhaustion policy, but the shader binds the same record through the root CBV;
the staged descriptor is not copied into the persistent texture heap. Its
cursor follows the same conservative frame retirement boundary. The historical
`camera_constant_updates` counter now accounts for this complete frame-constant
record.

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
only during synchronous creation. `Renderer` owns neither source code,
shader artifacts, nor caller CPU pixels afterward. The root signatures, PSOs,
cube/terrain buffers, checker, cubemap, and persistent texture heap remain valid
across swap-chain resize and are released only after shutdown drains and retires
every submitted frame.

The renderer-owned `compose_frame_pipeline` owns semantic imports, passes, and
access declarations. `shark::render_graph` compiles those declarations and
owns the derived transition records. The private D3D12 RHI recorder resolves
backend-supplied native bindings and emits the whole-resource legacy barriers;
the private renderer backend cross-checks compiled, executed, and recorded
counts. Static-upload barriers remain in the focused startup upload path, and
the diagnostic probe continues to use D3D12 buffer promotion/decay. Enhanced
barriers remain a later capability-gated graph backend.

## PIX events and GPU timestamps

G-007 privately links the pinned WinPixEventRuntime and enables its retail event
path in both Debug and Release. The command list contains five balanced,
stable names:

```text
StaticSceneUpload
Frame
  Terrain
  TexturedCube
  Skybox
```

`StaticSceneUpload` surrounds the one-time cube/terrain vertex/index, checker,
and six cubemap copies plus six initialization barriers. Each submitted
`Frame` begins after command-list reset and encloses the diagnostic probe copy,
graph barriers, all three passes, and timestamp resolve. Each nested marker
contains only its callback's bind/draw work; `Terrain` also owns the attachment
clears, bounds draw, and cyan query-marker draw.

The direct queue's GPU tick frequency is queried once. The global query/readback
allocation is split exactly:

```text
context index       0       1       2
query base          0       8      16
readback bytes      0      64     128
```

Each eight-query slice stores frame begin, terrain begin/end, cube begin/end,
sky begin/end, and frame end. Frame duration includes the probe copy, all four
graph barriers, and all three passes, but excludes `ResolveQueryData`. Pass
durations exclude graph barriers. Resolved samples must preserve that exact
nested/sequential order; equal timestamps are valid.

The persistently mapped readback memory is never inspected immediately after
submission. A context is marked pending only after its direct-queue submission
is fence tracked. Its results are consumed immediately before normal context
retirement/reset, either when that context is reused after its existing
completion check or when resize/shutdown drains the queue. No timing-only wait,
drain, readback allocation, or resource barrier is added.

## Resize, minimize, and shutdown

`WindowResizedEvent` carries the latest usable nonzero client extent. If it
differs from the swap chain, the renderer's D3D12 backend:

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

After explicit renderer shutdown, `Device::validate_debug_state` reports
live D3D12 device children and inspects D3D12 and DXGI info-queue messages added
since their preceding cursors. Corruption, errors, discarded bounded-queue
messages, or live renderer-owned D3D12 children fail the process. Warnings remain
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
  physical client area to `960x600`, verifies the renderer and depth
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
- proves five indexed draws per frame: one 6,144-index terrain surface draw,
  one 24-index terrain-bounds draw, one six-index terrain-query-marker draw,
  and one 36-index draw for each of the cube and skybox; it also proves one
  depth clear and one texture binding per frame submission;
- proves the static query marker contains exactly six vertices and six indices,
  and its draw count equals frame submissions with six submitted indices per
  draw;
- proves both terrain modes execute at least once and their counts sum to the
  terrain surface draw count;
- proves one graph compilation/execution, seven imports, three pass
  executions, two dependencies, four recorded transitions, and 16 elided
  transitions per frame submission;
- proves one `StaticSceneUpload` PIX event, one `Frame` event per submission,
  plus one `Terrain`, one `TexturedCube`, and one `Skybox` event per submission;
- proves global timestamp capacity is 24, per-context high-water is eight,
  exactly eight queries and one resolve are recorded per submission, and
  shutdown consumes one frame/terrain/cube/sky timing sample per retired
  submission;
- reports average and maximum frame/terrain/cube/sky milliseconds using the
  direct queue's nonzero GPU timestamp frequency without imposing a duration
  threshold;
- retires every submission by explicit shutdown;
- verifies one 256-byte GPU-consumed upload and one CPU staging descriptor per
  attempt, with high-water marks of 256 bytes and one descriptor;
- proves `static_upload_submissions == 1`,
  `geometry_buffer_creations == 4`, `checker_texture_creations == 1`, and
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
- on paths that exercise minimization, compares the complete `RendererStats`
  snapshot and proves no counter changes while minimized, including draws,
  graph work, uploads, texture bindings, clears, PIX events, timestamp
  writes/resolves, or timing-sample consumption;
- explicitly shuts down and validates renderer-owned D3D12 children;
- posts and accepts the native close request; and
- observes final HWND destruction.

Early close, dropped lifecycle events, occlusion that prevents progress,
deadline expiry, a dimension mismatch, any graphics failure, incomplete close,
or a final count other than the selected 1,000/120 target returns a nonzero
process exit code.
The smoke automatically selects solid terrain for the first half and wireframe
for the second half. It validates compilation, resource binding, indexed draw
and depth submission, aspect propagation, lifetime, and diagnostics; it does
not read back or compare the final pixels.

CTest runs hardware, packaged WARP, and packaged WARP with GPU-based validation
as separate serial processes. The focused validation test's 240-second CTest
timeout deliberately exceeds its 180-second application deadline. Debug and
Release must both pass those checks in addition to the device-only,
platform-only, frame-resource, render-graph planner, and D3D12 graph-executor
unit tests.

## Explicit non-goals

REN-001 does not expose a public/general upload allocator, global upload ring,
persistent descriptor allocator, generic deferred-destruction system, or
readback/image validation. Its timestamp query heap and readback buffer are a
fixed renderer diagnostic, not a general query allocator or asynchronous
readback service. Its two-slot shader-visible heap, focused root signatures,
static scene geometry, checker, retained startup cubemap SRV, and fixed PSOs
remain deliberately specific to this proof, not a general mesh manager,
material layout, pipeline cache, or hot-reload boundary.

The query-derived marker adds no GPU resource, descriptor, PSO, graph pass,
dependency, barrier, PIX event, or timestamp. The established frame remains
seven imports, three passes, two dependencies, four recorded barriers, 16
elisions, four geometry buffers, and eight timestamps. Its query ownership and
metric semantics remain in the platform-independent terrain module.

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
[the skybox contract](SKYBOX.md) for the visible background acceptance. See
[the terrain contract](TERRAIN.md) for the canonical surface-query contract,
deterministic tile, and diagnostic rendering modes. `REN-001` was completed on
July 18, 2026. The next increment is `T-003`, layered PBR terrain materials.
