# Direct3D 12 Presentation and Frame-Resource Contract

- **Completed through:** `G-007`
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
changing the visible frame or adding a normal-frame wait.

## Public boundary and ownership

`shark::rhi::d3d12::Presentation` is a move-only PIMPL. Its public header uses
an opaque native-window pointer and engine-owned extent, color,
`PresentationFrameData`, status, and statistics records plus pointer/size
`ShaderBytecodeView` inputs; it exposes no Win32, DXGI, D3D12, WRL, or COM
types. `PresentationFrameData` contains one finite engine-owned row-major
`math::Matrix4x4 view_projection` value, which the per-frame call borrows while
recording the current identity-world cube frame.

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
- one 12-entry timestamp query heap and one persistently mapped 96-byte
  readback buffer partitioned into three fixed context slices;
- one three-buffer `DXGI_SWAP_EFFECT_FLIP_DISCARD` swap chain;
- three `DXGI_FORMAT_R8G8B8A8_UNORM` back-buffer references;
- one CPU-only RTV heap with three descriptors;
- one CPU-only DSV heap with one descriptor and one
  `DXGI_FORMAT_D32_FLOAT` depth texture matching the current physical extent;
- one reusable graphics command list;
- one resource-bound root signature and one immutable cube graphics PSO;
- one committed default-heap cube vertex buffer containing 24 position/UV
  vertices and one committed default-heap index buffer containing 36
  `uint16_t` indices;
- one committed default-heap `8x8` one-mip
  `DXGI_FORMAT_R8G8B8A8_UNORM` procedural checker texture;
- one persistent one-slot shader-visible CBV/SRV/UAV heap holding the checker
  SRV;
- three frame contexts, each containing one direct command allocator, one
  persistently mapped 64 KiB committed upload buffer, one 256-byte default-heap
  upload-probe destination, one CPU-only 64-slot CBV/SRV/UAV staging heap, a
  checked upload cursor, a checked descriptor cursor, a checked four-query
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
cube vertex/index data and checker. It transitions the immutable resources to
their final draw states, signals the normal monotonic fence, performs one
bounded startup wait, and releases the temporary upload resources before the
first frame. No static geometry or texture upload occurs in the frame loop.

One frame performs the following work:

1. read DXGI's current back-buffer index and select that context;
2. inspect its preceding completion value and wait only when that context is
   still in flight;
3. consume its pending GPU timing sample only if the preceding completion
   fence is complete, then retire the submission and reset its allocator-facing
   upload, descriptor, and timestamp cursors;
4. reserve one 256-byte frame record, write the 64-byte `view_projection`
   matrix followed by the retained diagnostic probe, create one CBV in the
   context's CPU-only staging heap, and point the root CBV at the same GPU
   upload address;
5. reserve the context's exact four-query timestamp slice;
6. build and compile one frame-local graph that imports the current back buffer
   in `PRESENT`, imports the depth texture in `DEPTH_WRITE`, and declares one
   `TexturedCube` pass writing both attachments;
7. reset that context's command allocator and the shared command list with the
   immutable cube PSO;
8. begin the `Frame` PIX event, write the frame-begin timestamp, and copy the
   256-byte frame record from the upload heap to the context's
   default-heap probe destination on the direct queue, outside the graph;
9. execute the graph, which records `PRESENT -> RENDER_TARGET`, invokes the
   pass inside a nested `TexturedCube` PIX event and pass timestamp pair, and
   records `RENDER_TARGET -> PRESENT`; the depth target remains in
   `DEPTH_WRITE`, so both of its equal-state transitions are elided;
10. write the frame-end timestamp, resolve all four query values to the
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
the staged descriptor is not copied into the persistent checker heap. Its
cursor follows the same conservative frame retirement boundary.

Each probe destination is created in `D3D12_RESOURCE_STATE_COMMON`.
`CopyBufferRegion` uses Direct3D 12 buffer promotion to `COPY_DEST`, and the
buffer decays to `COMMON` after the command-list execution boundary. The
per-context fence completes before either side is reused.

Interactive presentation uses `Present(1, 0)` to synchronize to vertical
refresh. The bounded smoke uses `Present(0, 0)` and requests no tearing flag.
Only `S_OK` increments `presented_frames`; `DXGI_STATUS_OCCLUDED` is reported
separately and never satisfies the 1,000-frame gate. An occluded attempt still
has a signaled context checkpoint because its command list was submitted.
Vertical synchronization may block inside `Present`, and context reuse may
perform a bounded fence wait; G-003 removes only the unconditional queue drain
after every frame.

The generated shader byte arrays are borrowed only while the root signature and
PSO are created synchronously. Presentation owns neither source code nor shader
artifacts after creation. The root signature, PSO, cube buffers, checker, and
checker heap remain valid across swap-chain resize and are released only after
shutdown drains and retires every submitted frame.

G-006's graph executor owns the per-frame whole-resource legacy transition
barriers. It resolves the current imported resources immediately before graph
execution and verifies that graph, executor, and recorder transition counts
agree. Static-upload barriers remain in the focused startup upload path, and
the diagnostic probe continues to use D3D12 buffer promotion/decay. Enhanced
barriers remain a later capability-gated graph backend.

## PIX events and GPU timestamps

G-007 privately links the pinned WinPixEventRuntime and enables its retail event
path in both Debug and Release. The command list contains three balanced,
stable names:

```text
StaticCubeUpload
Frame
  TexturedCube
```

`StaticCubeUpload` surrounds the one-time vertex, index, checker copy, and their
three initialization barriers. Each submitted `Frame` begins after command-list
reset and encloses the diagnostic probe copy, graph barriers, `TexturedCube`,
and timestamp resolve. `TexturedCube` begins after the graph's pre-pass color
barrier and contains only the pass callback's clear, bind, and draw commands.

The direct queue's GPU tick frequency is queried once. The global query/readback
allocation is split exactly:

```text
context index       0       1       2
query base          0       4       8
readback bytes      0      32      64
```

Each four-query slice stores frame begin, pass begin, pass end, and frame end.
Frame duration includes the probe copy, both graph color barriers, and pass
work, but excludes `ResolveQueryData`. Pass duration excludes graph barriers.
Resolved samples must satisfy
`frameBegin <= passBegin <= passEnd <= frameEnd`; equal timestamps are valid.

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
   matching `D32_FLOAT` depth texture and DSV.

A minimized window publishes no zero render extent. The sandbox stops
updating or presenting the camera while minimized and resumes from the restored
resize event. A duplicate restore extent is a no-op. Shutdown drains the queue,
consumes and retires every outstanding frame submission, verifies every
submission retired, releases every back buffer, the depth texture, cube
resources, checker heap, pipeline objects, timestamp query/readback storage,
and all context-owned resources, closes the fence event, and remains safe to
call again. The fixed smoke then verifies that every submission produced one
timing sample.

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

The frame count is not user-configurable. A successful run:

- shows a real nonactivating window;
- presents 250 successful frames;
- changes the physical client area to `960x600`, verifies the presentation and
  depth extents follow it, and proves the projection aspect changed from the
  initial `1280x720`;
- reaches 500 frames, minimizes, and proves the count does not advance;
- observes restore followed by its resize event;
- reaches exactly 1,000 successful presents;
- proves all three DXGI-selected contexts were acquired and reused;
- matches `frame_context_acquisitions`, `camera_constant_updates`,
  `upload_allocations`, `descriptor_allocations`, and fence-tracked
  `frame_submissions` to successful plus occluded present attempts;
- proves `cube_draw_calls`, `depth_clear_count`, and `texture_bindings` each
  equal `frame_submissions`, while `cube_indices == cube_draw_calls * 36`;
- proves one graph compilation, one graph execution, and one `TexturedCube`
  pass execution per frame submission;
- proves two frame-local imports and exactly two recorded graph transition
  barriers per frame submission, with the cube draw count equal to the graph
  pass-execution count;
- proves one `StaticCubeUpload` PIX event, one `Frame` event per submission,
  and one `TexturedCube` event per graph-pass execution;
- proves global timestamp capacity is 12, per-context high-water is four,
  exactly four queries and one resolve are recorded per submission, and
  shutdown consumes one nested frame/pass timing sample per retired
  submission;
- reports average and maximum frame/pass milliseconds using the direct queue's
  nonzero GPU timestamp frequency without imposing a duration threshold;
- retires every submission by explicit shutdown;
- verifies one 256-byte GPU-consumed upload and one CPU staging descriptor per
  attempt, with high-water marks of 256 bytes and one descriptor;
- proves `static_upload_submissions == 1`,
  `geometry_buffer_creations == 2`, `checker_texture_creations == 1`, and
  `texture_srv_creations == 1`; the static upload completes its bounded wait
  before the first frame;
- proves `depth_resource_creations == resize_count + 1` and
  `full_queue_drains == resize_count + 1` for effective resizes plus shutdown,
  while reporting the hardware-dependent context-reuse wait count without
  constraining it;
- proves `camera_matrix_changes >= 3` for the initial matrix, the
  aspect-changing resize, and the scripted `0.25`-radian yaw after frame 750;
- verifies no cube draw, camera upload, depth clear, PIX frame/pass event,
  timestamp write/resolve, or timing-sample consumption occurs while minimized;
- explicitly shuts down and validates presentation children;
- posts and accepts the native close request; and
- observes final HWND destruction.

Early close, dropped lifecycle events, occlusion that prevents progress,
deadline expiry, a dimension mismatch, any graphics failure, incomplete close,
or a final count other than 1,000 returns a nonzero process exit code.
The smoke validates compilation, resource binding, indexed draw and depth
submission, aspect propagation, lifetime, and diagnostics; it does not read
back or compare the final pixels.

CTest runs hardware, packaged WARP, and packaged WARP with GPU-based validation
as separate serial processes. Debug and Release must both pass those checks in
addition to the device-only, platform-only, frame-resource, render-graph
planner, and D3D12 graph-executor unit tests.

## Explicit non-goals

G-007 does not expose a public/general upload allocator, global upload ring,
persistent descriptor allocator, generic deferred-destruction system, or
readback/image validation. Its timestamp query heap and readback buffer are a
fixed presentation diagnostic, not a general query allocator or asynchronous
readback service. Its one-slot shader-visible heap, root signature, geometry,
checker, and PSO are deliberately specific to the cube proof, not a general
asset system, mesh manager, material layout, pipeline cache, or hot-reload
boundary.

The graph remains frame-local, direct-queue, serial, and limited to imported
whole resources. It adds no graph-owned transients, lifetime/aliasing analysis,
subresource tracking, automatic attachment binding, pass culling/merging,
parallel recording, copy queue, asynchronous compute, enhanced barriers,
fullscreen policy, tearing mode, asset texture loading, mip generation, or
scene/ECS layer.

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
details plus manual PIX acceptance.
