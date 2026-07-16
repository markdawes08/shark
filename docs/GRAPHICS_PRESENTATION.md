# Direct3D 12 Presentation and Frame-Resource Contract

- **Completed through:** `G-004`
- **Last verified:** July 16, 2026

G-002 produced Shark's first pixels through a resize-safe Direct3D 12
flip-model swap chain. G-003 replaces its wait-after-every-present path with
three fence-gated reusable frame contexts and bounded transient staging. G-004
adds the first build-time HLSL pipeline and draws one color-interpolated
triangle while preserving the established window and frame lifecycles.

## Public boundary and ownership

`shark::rhi::d3d12::Presentation` is a move-only PIMPL. Its public header uses
an opaque native-window pointer and engine-owned extent, color, status, and
statistics records plus pointer/size `ShaderBytecodeView` inputs; it exposes no
Win32, DXGI, D3D12, WRL, or COM types. `present_frame()` records the current
first-triangle frame.

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
- one reusable graphics command list;
- one persistent empty root signature and one immutable triangle graphics PSO;
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

One frame performs the following work:

1. read DXGI's current back-buffer index and select that context;
2. inspect its preceding completion value and wait only when that context is
   still in flight;
3. retire the completed submission and reset its allocator-facing upload and
   descriptor cursors;
4. reserve and write one 256-byte diagnostic upload record, then create one CBV
   in the context's CPU-only staging heap;
5. reset that context's command allocator and the shared command list with the
   immutable triangle PSO;
6. copy the diagnostic record from the upload heap to the context's default-heap
   probe destination on the direct queue;
7. transition the current back buffer from `PRESENT` to `RENDER_TARGET`, clear
   it, bind the empty root signature, set the current physical viewport/scissor
   and triangle-list topology, issue `DrawInstanced(3, 1, 0, 0)`, then
   transition the buffer back to `PRESENT`;
8. close and execute the command list;
9. immediately signal the next monotonic fence value and store it on the
   context; and
10. call `Present` without an unconditional post-frame fence wait.

The copy makes upload-memory reuse genuinely GPU-fence-sensitive. Probe content
is not read back. The staged CBV exercises bounded descriptor addressing and
exhaustion policy, but it is CPU-only, never bound to the G-004 triangle, and is
not a shader-visible descriptor system. Its cursor follows the same
conservative frame retirement boundary even though an unbound staging
descriptor is not itself consumed by the GPU.

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
artifacts after creation. The root signature and PSO remain valid across swap
chain resize and are released only after shutdown drains and retires every
submitted frame.

Legacy transition barriers are intentional for explicit back-buffer state
changes here. Enhanced-barrier selection is owned by the later render-graph
work, not this first-pixel proof.

## Resize, minimize, and shutdown

`WindowResizedEvent` carries the latest usable nonzero client extent. If it
differs from the swap chain, presentation:

1. drains its direct queue;
2. retires every context's completed frame submission;
3. releases all three back-buffer references;
4. calls `ResizeBuffers` with the new physical extent; and
5. reacquires, renames, and recreates the three RTVs.

A minimized window publishes no zero render extent. The sandbox stops
presenting while minimized and resumes from the restored resize event. A
duplicate restore extent is a no-op. Shutdown drains the queue, retires every
outstanding frame submission, verifies every submitted frame retired, releases
every back buffer and all resources owned by the contexts, closes the fence
event, and remains safe to call again. Resize and shutdown are the only
unconditional full-queue drains in a successful fixed smoke run.

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
- changes the physical client area to `960x540` and verifies the presentation
  extent follows it;
- reaches 500 frames, minimizes, and proves the count does not advance;
- observes restore followed by its resize event;
- reaches exactly 1,000 successful presents;
- proves all three DXGI-selected contexts were acquired and reused;
- matches acquisitions, upload/descriptor allocations, and fence-tracked
  submissions to successful plus occluded present attempts;
- proves every submitted frame records exactly one triangle draw and three
  vertices;
- retires every submission by explicit shutdown;
- verifies one 256-byte GPU-consumed upload and one CPU staging descriptor per
  attempt, with high-water marks of 256 bytes and one descriptor;
- proves full queue drains equal effective resizes plus shutdown while reporting
  the hardware-dependent context-reuse wait count without constraining it;
- explicitly shuts down and validates presentation children;
- posts and accepts the native close request; and
- observes final HWND destruction.

Early close, dropped lifecycle events, occlusion that prevents progress,
deadline expiry, a dimension mismatch, any graphics failure, incomplete close,
or a final count other than 1,000 returns a nonzero process exit code.
The smoke validates compilation, draw submission, lifetime, and diagnostics; it
does not read back or compare the final pixels.

CTest runs hardware, packaged WARP, and packaged WARP with GPU-based validation
as separate serial processes. Debug and Release must both pass those checks in
addition to the device-only, platform-only, and frame-resource unit tests.

## Explicit non-goals

G-004 does not expose a public/general upload allocator, global upload ring,
shader-visible descriptor heap, persistent descriptor allocator, generic
deferred-destruction system, or readback/image validation. Its root signature
and PSO are deliberately specific to the first triangle, not a general shader
asset system, pipeline cache, or hot-reload boundary. It adds no vertex/index
buffer, texture, shader-visible binding, camera, depth buffer, render graph,
copy queue, asynchronous compute, fullscreen policy, or tearing mode. Camera
math, reversed-Z depth, and the textured cube are G-005.

See [the first HLSL pipeline contract](GRAPHICS_PIPELINE.md) for compilation,
generated artifacts, root-signature/PSO state, and the draw contract.
