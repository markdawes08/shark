# Direct3D 12 Device Contract

- **Completed through:** `T-004`
- **Last verified:** July 19, 2026

G-001 establishes adapter discovery, diagnostics, capability reporting, and
device ownership independently of frame submission. It intentionally creates
no command queue, allocator, command list, fence, resource, swap chain, or
rendered frame.

## Public boundary

`shark::rhi::d3d12::Device` is move-only and hides all Windows, DXGI, D3D12,
WRL, and COM types behind a PIMPL. Public callers receive owned engine records:

- `AdapterInfo` for session index, name, LUID, PCI IDs, memory, flags, and
  required-baseline probe results;
- `RendererCaps` for the maximum Feature Level and Shader Model plus optional
  binding, barrier, DXR, mesh, VRS, sampler-feedback, work-graph, and memory
  capabilities; and
- `DebugMessageCounts` aggregated from the D3D12 and DXGI initialization info
  queues.

This is a narrow D3D12 RHI boundary, not a cross-API abstraction. Raw COM
pointers remain private until later renderer code receives typed resource and
queue operations.

G-002 adds a private `detail::DeviceAccess` bridge used only below the public
graphics boundary. REN-001's `shark::renderer::Renderer` receives `Device` by
reference at the sandbox composition root; its private D3D12 backend uses that
bridge to borrow the authoritative device and factory without exposing COM
through public renderer headers. `Device` must outlive every `Renderer`. There
is no public D3D12 `Presentation` class.

The process permits one authoritative `Device` startup. D3D12 debug-layer,
GPU-validation, and DRED settings are process-global and must be fixed before
the first probe; a second startup attempt returns an invalid-state error. GPU
integration cases therefore run as separate `SharkSandbox` processes.

## Initialization order

One device startup follows this fixed order:

1. Validate the tagged adapter selection and diagnostics configuration.
2. For WARP mode only, load the absolute app-local `d3d10warp.dll` and retain
   its module for the full device lifetime.
3. Enable the D3D12 debug layer and optional GPU-based validation.
4. Force DRED automatic breadcrumbs, page-fault tracking, and breadcrumb
   contexts before any device probe.
5. Bound the DXGI debug info queue and create a debug DXGI factory.
6. Enumerate all adapters in `HIGH_PERFORMANCE` preference order, probe Feature
   Level 12_0 and Shader Model support, and log every candidate.
7. Select the exact requested adapter with no fallback.
8. Create and name the authoritative Feature Level 12_0 device.
9. Bound its D3D12 info queue.
10. Query required and optional capabilities.
11. Inspect both DirectX queues, fail corruption or errors, then publish the
    immutable report.

G-001 arms DRED before device creation. G-002 consumes that setup and G-003
preserves it across asynchronous frame-context reuse: failed
submission, fence, resize, and present operations query the device-removal
reason and log bounded automatic-breadcrumb, breadcrumb-context, page-fault,
existing-allocation, and recently-freed-allocation details. G-004 preserves the
same path while adding root-signature/PSO creation and a real draw submission.
G-005 keeps that diagnostic boundary while adding the camera constants,
shader-visible checker binding, indexed geometry, static upload submission, and
reversed-Z depth resource. G-006 keeps the same device and removal-diagnostic
boundary while routing the two per-frame attachment transitions through the
minimal render-graph executor. T-001, S-002A, and T-002 preserve that device
boundary while adding terrain, procedural daylight, canonical CPU terrain
queries, and the query-derived diagnostic draw. REN-001 preserves the same
device contract while moving scene helpers, pass composition, and public
statistics into the renderer. Its private D3D12 backend also owns the
scene-named timestamp layout and accumulator. Generic frame-resource,
device-access, and legacy-transition helpers remain private D3D12 RHI
implementation details. T-003 adds fixed renderer-owned material resources and
bindings without changing adapter selection, feature-level requirements,
device ownership, validation, or removal diagnostics. S-003 adds fixed HDR
environment, scene-color, material-sphere, and tone-map resources while
preserving that same device contract and baseline.

If a debugger is attached, corruption and error messages also request a debug
break. Unattended processes count and report those severities instead of
trapping. Any initialization corruption or error fails startup; warnings are
reported but do not masquerade as errors.

## Adapter selection

The command-line contract is:

```text
SharkSandbox [--platform-smoke | --gpu-smoke | --present-smoke | --capabilities]
             [--warp | --adapter <index>]
             [--gpu-validation]
```

Selection rules are strict:

- With no selector, choose the first local, non-software adapter in DXGI
  high-performance order that meets the required baseline.
- `--adapter N` chooses exactly candidate `N` from the current startup report.
  It never falls through to another GPU or to WARP.
- `--warp` chooses the packaged software adapter explicitly.
- Automatic hardware selection never uses a software or remote adapter.
- An unavailable or insufficient explicit adapter is an error.
- Adapter indices are diagnostic session values, not persistent identities.

`--platform-smoke` remains GPU-independent and rejects GPU selectors. Both
`--gpu-smoke` and `--capabilities` create no window and exit after device
verification. `--present-smoke` accepts the normal GPU selectors and optional
GPU-based validation, creates a real window, presents exactly 1,000 successful
frames normally or 120 in the focused GPU-validation mode, containing the
visible indexed terrain chunks, matching magenta chunk bounds, material sphere,
cyan terrain-query marker, cube, and skybox plus final tone mapping, verifies
camera/depth/HDR/resource/graph lifecycles, and exits. The default interactive
path keeps the device alive while the free-fly camera, IBL-lit terrain and
query-normal pin, material sphere, textured cube, and HDR sky run as
`Terrain`, `TexturedCube`, `Skybox`, and `ToneMap` passes. `F3` retains the
procedural-daylight fallback. The startup DDS cubemap remains an asset/upload
proof and is not imported, bound, or sampled per frame.

## Required and optional capabilities

The only startup capability requirements are:

- Direct3D Feature Level 12_0 or newer; and
- Shader Model 6.0 or newer.

The retail Shader Model query begins at 6.9 for the pinned Agility/DXC family
and descends only when an older runtime rejects the requested enum. A missing
optional feature is valid and remains visible as `no`, `not supported`, or
`unavailable` in the report. Versioned tiers use semantic values such as `1.2`.
Enhanced barriers, DXR, mesh shaders, VRS, sampler feedback, work graphs, UMA,
and cache-coherent UMA never decide G-001 startup eligibility.

The selected adapter's name, LUID, vendor/device IDs, memory, Feature Level,
Shader Model, resource tiers, and optional tiers are logged. This gives later
feature experiments an evidence-based capability gate without changing the
baseline silently.

## Runtime layout and provenance

`SharkSandbox.exe` owns the required Agility exports:

```text
D3D12SDKVersion = 619
D3D12SDKPath    = .\D3D12\
```

The build copies the pinned runtime into:

```text
<target>/SharkSandbox.exe
<target>/d3d10warp.dll
<target>/D3D12/D3D12Core.dll
<target>/D3D12/d3d12SDKLayers.dll
```

Startup logs the loaded `D3D12Core.dll` path. Explicit WARP startup loads and
verifies the app-local path, so the smoke test cannot pass accidentally against
the inbox WARP binary. Generated runtime files remain under ignored `out/`.

The WARP NuGet binary is restricted to Windows development, testing, and
internal use and is not redistributed. D3D12 SDK Layers are a development
diagnostic and must be removed from a future production package. See
[Microsoft's WARP guide](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp)
and [Agility SDK integration guide](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/).

## Permanent checks

CTest keeps six independent integration processes:

- the GPU-independent Win32 lifecycle smoke;
- automatic hardware device creation;
- packaged WARP device creation;
- `integration.gpu.hardware_cube_present`;
- `integration.gpu.warp_cube_present`; and
- `integration.gpu.warp_cube_present_validation`.

Three separate serial build checks verify shader include dependency rebuilding,
malformed-source rejection, and warning-as-error rejection before the graphics
integration contract is considered complete.

Unit tests cover the exact Feature Level/Shader Model baseline, the complete CLI
conflict/malformed-index boundary, aligned frame-arena allocation and
exhaustion, invalid lifecycle transitions, fence-gated retirement, and monotonic
fence exhaustion. G-005 adds core matrix, finite reversed-Z camera,
controller/focus lifecycle, 24/36 cube geometry, checker generation, input
layout, and depth-state unit coverage. G-006 adds single-use, move-safe,
owner-scoped graph declarations, stable hazard-aware ordering,
cycle/access/failure validation, transition elision, exact legacy state
mapping/alias handling, and native binding rejection.
The presentation smoke additionally verifies all three frame contexts are
reused, every submission compiles and executes exactly four graph passes with
15 imports, three dependencies, six emitted barriers, and 31 elided
transitions plus four texture-table bindings, and every submission retires
during explicit shutdown before frame resources are released. S-003 retains
four geometry buffers. T-004 retains that resource count while issuing
`2V + 4` indexed draws for `V` visible chunks plus one fullscreen tone-map draw
and ten timestamps per frame. The smoke poses prove `V=16` and `V=5`.

Renderer shutdown explicitly drains and releases its command list, all scene
and tone-map PSOs/root signatures, descriptor heap, checker/cubemap/material/
environment textures, HDR scene target, cube/terrain vertex/index buffers,
depth texture/DSVs, swap-chain resources, and frame contexts before
`Device::validate_debug_state` checks new D3D12 and DXGI messages plus live
D3D12 child objects. Debug and Release must both build and pass all registered
tests with zero DirectX corruption or error messages. See the
[presentation and frame-resource contract](GRAPHICS_PRESENTATION.md) for the
G-002/G-003 frame, staging, retirement, and resize rules, and the
[HLSL pipeline contract](GRAPHICS_PIPELINE.md) for the G-004/G-005 shader and
draw path. See [the camera and textured-cube contract](CAMERA_AND_CUBE.md) for
the G-005 input, math, depth, static-resource, and acceptance rules. See
[the minimal render-graph contract](RENDER_GRAPH.md) for the G-006
platform-independent planner and D3D12 legacy-barrier executor.
See [the terrain contract](TERRAIN.md) for the canonical query, deterministic
full-resolution chunks, frustum culling, and diagnostic rendering modes, and
[the skybox contract](SKYBOX.md) for the S-003 HDR environment, analytic sun,
and procedural fallback. T-002 adds no device capability or lifetime policy;
REN-001, T-003, and S-003 likewise change no device capability or selection
policy. T-004 was completed on July 19, 2026 without changing adapter,
capability, debug-layer, device, or lifetime policy. The next increment is
`T-005`, one bounded coarser terrain LOD with crack-free seams and
full-resolution canonical queries.
