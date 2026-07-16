# Direct3D 12 Presentation and Frame-Resource Contract

- **Completed through:** `G-005`
- **Last verified:** July 16, 2026

G-002 produced Shark's first pixels through a resize-safe Direct3D 12
flip-model swap chain. G-003 replaces its wait-after-every-present path with
three fence-gated reusable frame contexts and bounded transient staging. G-004
adds the first build-time HLSL pipeline. G-005 preserves those lifecycles while
adding the engine camera, one indexed textured cube, the first shader-visible
SRV binding, and a resize-safe reversed-Z depth target.

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
  checked upload cursor, a checked descriptor cursor, a generation, and one
  direct-fence completion value; and
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
3. retire the completed submission and reset its allocator-facing upload and
   descriptor cursors;
4. reserve one 256-byte frame record, write the 64-byte `view_projection`
   matrix followed by the retained diagnostic probe, create one CBV in the
   context's CPU-only staging heap, and point the root CBV at the same GPU
   upload address;
5. reset that context's command allocator and the shared command list with the
   immutable cube PSO;
6. copy the 256-byte frame record from the upload heap to the context's
   default-heap probe destination on the direct queue;
7. transition the current back buffer from `PRESENT` to `RENDER_TARGET`, clear
   it, clear the matching depth texture to `0.0F`, bind the RTV and DSV, bind
   the root signature, checker heap/SRV, camera root CBV, current physical
   viewport/scissor, cube vertex/index buffers, and triangle-list topology,
   issue `DrawIndexedInstanced(36, 1, 0, 0, 0)`, then transition the buffer
   back to `PRESENT`;
8. close and execute the command list;
9. immediately signal the next monotonic fence value and store it on the
   context; and
10. call `Present` without an unconditional post-frame fence wait.

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

Legacy transition barriers are intentional for the explicit back-buffer and
static-upload transitions here. The depth target remains in `DEPTH_WRITE`.
Enhanced-barrier selection is owned by the later render-graph work, not this
focused presentation proof.

## Resize, minimize, and shutdown

`WindowResizedEvent` carries the latest usable nonzero client extent. If it
differs from the swap chain, presentation:

1. drains its direct queue;
2. retires every context's completed frame submission;
3. releases all three back-buffer references and the old depth texture;
4. calls `ResizeBuffers` with the new physical extent; and
5. reacquires, renames, and recreates the three RTVs, then creates and names a
   matching `D32_FLOAT` depth texture and DSV.

A minimized window publishes no zero render extent. The sandbox stops
updating or presenting the camera while minimized and resumes from the restored
resize event. A duplicate restore extent is a no-op. Shutdown drains the queue,
retires every outstanding frame submission, verifies every submitted frame
retired, releases every back buffer, the depth texture, cube resources,
checker heap, pipeline objects, and all context-owned resources, closes the
fence event, and remains safe to call again.

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

Upload and descriptor allocation is linear and fixed-capacity. Zero-size,
invalid-alignment, overflow, and exhaustion requests return structured errors
without advancing a cursor. Unit tests also prove an incomplete context refuses
retirement or reset and that the fence timeline fails before wraparound.

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
- verifies no cube draw, camera upload, or depth clear occurs while minimized;
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
addition to the device-only, platform-only, and frame-resource unit tests.

## Explicit non-goals

G-005 does not expose a public/general upload allocator, global upload ring,
persistent descriptor allocator, generic deferred-destruction system, or
readback/image validation. Its one-slot shader-visible heap, root signature,
geometry, checker, and PSO are deliberately specific to the cube proof, not a
general asset system, mesh manager, material layout, pipeline cache, or
hot-reload boundary. It adds no copy queue, asynchronous compute, render graph,
fullscreen policy, tearing mode, asset texture loading, mip generation, or
scene/ECS layer.

See [the HLSL pipeline contract](GRAPHICS_PIPELINE.md) for compilation,
generated artifacts, root-signature/PSO state, and the indexed draw contract.
See [the camera and textured-cube contract](CAMERA_AND_CUBE.md) for coordinate,
input, depth, geometry, texture, and acceptance rules.
