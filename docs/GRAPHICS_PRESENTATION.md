# Direct3D 12 Presentation Contract

- **Increment:** `G-002`
- **Last verified:** July 15, 2026

G-002 produces Shark's first pixels: a clear color presented through a
resize-safe Direct3D 12 flip-model swap chain. This is a deliberately small
proof of window, DXGI, queue, resource-state, and shutdown ownership. It is not
the durable frame-resource system planned for G-003.

## Public boundary and ownership

`shark::rhi::d3d12::Presentation` is a move-only PIMPL. Its public header uses
an opaque native-window pointer and engine-owned extent, color, status, and
statistics records; it exposes no Win32, DXGI, D3D12, WRL, or COM types.

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
- one direct command allocator and graphics command list; and
- one fence plus one auto-reset Windows event.

The DXGI factory is associated with the HWND using `DXGI_MWA_NO_ALT_ENTER`.
Every significant D3D12 object is named. The platform manifests establish
Per-Monitor DPI Awareness v2 before HWND creation, so the initial and resized
swap-chain extents are the same physical client-pixel dimensions published by
`Application`.

This is a triple-buffered swap chain, not three reusable frame contexts. G-002
waits for the direct queue after every frame. G-003 will introduce independent
allocator, transient-storage, descriptor, and fence ownership per back buffer.

## Serialized clear and present

One frame performs the following work:

1. reset the allocator and command list after the preceding fence completed;
2. transition the current back buffer from `PRESENT` to `RENDER_TARGET`;
3. bind its RTV and clear it;
4. transition it back to `PRESENT`;
5. close and execute the command list;
6. call `Present`; and
7. signal and wait for the direct-queue fence.

Interactive presentation uses `Present(1, 0)` to synchronize to vertical
refresh. The bounded smoke uses `Present(0, 0)` and requests no tearing flag.
Only `S_OK` increments `presented_frames`; `DXGI_STATUS_OCCLUDED` is reported
separately and never satisfies the 1,000-frame gate.

Legacy transition barriers are intentional here. Enhanced-barrier selection is
owned by the later render-graph work, not this first-pixel proof.

## Resize, minimize, and shutdown

`WindowResizedEvent` carries the latest usable nonzero client extent. If it
differs from the swap chain, presentation:

1. drains its direct queue;
2. releases all three back-buffer references;
3. calls `ResizeBuffers` with the new physical extent; and
4. reacquires, renames, and recreates the three RTVs.

A minimized window publishes no zero render extent. The sandbox stops
presenting while minimized and resumes from the restored resize event. A
duplicate restore extent is a no-op. Shutdown drains the queue, releases every
back buffer and presentation child, closes the fence event, and remains safe to
call again.

## Failure and diagnostics contract

Every result-returning D3D12, DXGI, fence, event, resize, and present operation
is checked. Fence waiting is bounded and periodically queries the device-removal
reason instead of waiting forever. A removal-class failure logs the triggering
HRESULT and removal reason, then emits bounded DRED automatic-breadcrumb nodes,
breadcrumb contexts, page-fault address, existing allocations, and recently
freed allocations.

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
- explicitly shuts down and validates presentation children;
- posts and accepts the native close request; and
- observes final HWND destruction.

Early close, dropped lifecycle events, occlusion that prevents progress,
deadline expiry, a dimension mismatch, any graphics failure, incomplete close,
or a final count other than 1,000 returns a nonzero process exit code.

CTest runs hardware, packaged WARP, and packaged WARP with GPU-based validation
as separate serial processes. Debug and Release must both pass those checks in
addition to the existing device-only and platform-only integrations.

## Explicit non-goals

G-002 adds no reusable frame contexts, upload arena, general descriptor
allocator, shader compilation, pipeline state, draw call, depth buffer, render
graph, copy queue, asynchronous compute, fullscreen policy, or tearing mode.
Those remain isolated future increments.
