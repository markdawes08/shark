# Renderer and Direct3D 12 Presentation/Frame-Resource Contract

- **Completed through:** `PHY-002`
- **Last verified:** July 19, 2026

`shark::renderer::Renderer` owns Shark's focused D3D12 scene/presentation
backend. W-001 preserves the triple-buffered fence-gated HDR lifecycle,
225-chunk capacity, and default-off `F4` diagnostics while adding one
procedural visual-water draw. Normal frames still submit and present without
an unconditional post-frame queue drain.

## Public boundary and ownership

`Renderer` is a move-only public PIMPL. Its COM-free public records include
`RendererConfig`, `RenderFrameData`, `RenderStatus`, `RendererStats`,
shader-bytecode views, mesh/chunk/LOD-range views, and generic 2D/cube upload
views.
`Renderer::create` consumes every pointer-based upload/bytecode view
synchronously and retains no caller CPU pointer.

`RendererConfig` carries:

- HWND/physical extent, clear color, and vertical-sync policy;
- cube, sky, terrain, material-sphere, and tone-map shader bytecode;
- checker/cubemap/terrain mesh/material uploads; and
- four environment-lighting uploads: radiance, diffuse irradiance,
  GGX-prefiltered specular, and split-sum BRDF LUT.

`RenderFrameData` carries finite scene/sky matrices, daylight settings, camera
and material-sphere world positions, terrain fill/material views, and the
environment mode. `F3` selects image-based lighting or the retained
procedural-daylight fallback.
Each `TerrainChunkUploadView` carries contiguous LOD0/coarse ranges, exact
bounds, and the measured maximum geometric error. The renderer receives
query-derived terrain marker geometry but does not own or perform canonical
terrain queries.

At the sandbox composition root, `Renderer::create` borrows the authoritative
`shark::rhi::d3d12::Device`. Private `DeviceAccess` exposes native device/factory
objects only below the public boundary. Required lifetime order is:

```text
construct: Device -> Application/HWND -> Renderer
destroy:   Renderer -> Application/HWND -> Device
```

Explicit, idempotent `Renderer::shutdown` occurs before HWND destruction. There
is no public D3D12 `Presentation` class.

## Persistent D3D12 inventory

The private backend owns:

- one direct command queue, monotonic fence, and auto-reset event;
- one triple-buffered flip-discard swap chain with three
  `R8G8B8A8_UNORM` back buffers;
- one RTV heap for three back buffers plus the HDR scene target;
- one `D32_FLOAT` depth texture and writable/read-only DSVs;
- one resize-owned `R16G16B16A16_FLOAT` scene-color texture with RTV/SRV;
- one reusable graphics command list;
- cube, sky, terrain solid/wireframe/line, water, material-sphere, and tone-map PSOs
  plus their focused root signatures;
- four committed geometry buffers: cube vertex/index and packed shared
  terrain-LOD/chunk-bounds/query-marker/material-sphere vertex/index;
- checker and retained S-001 DDS cubemap textures;
- three two-layer `32x32`, six-mip terrain material arrays;
- a `32x32` six-mip radiance cube, `8x8` irradiance cube, `32x32` six-mip
  GGX-prefiltered specular cube, and `32x32` split-sum BRDF LUT;
- one fixed ten-slot shader-visible SRV heap containing checker, retained DDS,
  three materials, four environment/IBL SRVs, and HDR scene color;
- one 36-entry timestamp heap and persistently mapped 288-byte readback buffer;
- three frame contexts; and
- the swap-chain frame-latency/lifetime state.

Each frame context owns one direct allocator, persistently mapped 64 KiB upload
buffer, one 256-byte default-heap probe destination, one 64-slot CPU-only
descriptor staging heap, bounded upload/descriptor/12-query cursors,
generation and fence completion state, and pending timing metadata.

DXGI's current back-buffer index selects the context. CPU frame count is never
substituted. Fence zero means never submitted or already retired.

## Static upload

Renderer creation records one `StaticSceneUpload` direct-queue submission:

- cube and packed terrain-LOD/chunk-bounds/marker/sphere vertex/index data;
- deterministic checker;
- six retained DDS cubemap faces;
- 36 terrain-material subresources containing 32,760 meaningful bytes; and
- 79 HDR environment subresources containing 284,608 meaningful RGBA32-float
  bytes.

The environment derivatives come from a CPU-only deterministic `64x32`
linear-HDR latitude-longitude source and are:

```text
radiance                  32 x 32, 6 faces, 6 mips
diffuse irradiance         8 x  8, 6 faces, 1 mip
prefiltered specular      32 x 32, 6 faces, 6 mips
split-sum BRDF LUT        32 x 32, 1 mip
```

The bounded source/convolution excludes the directional sun. Sky renders its
disk/halo analytically, while terrain and sphere add the same direct sun
outside IBL. Diffuse irradiance is Lambert-normalized by the shader's `1/pi`
factor.

Thirteen initialization barriers establish final input/shader states. The
submission signals the normal fence, performs one bounded startup wait, and
releases temporary upload resources before the first frame. No static upload
occurs in the frame loop.

The terrain buffers contain a 58,081-vertex surface stream, 225 contiguous
1,536-index LOD0 ranges, 225 contiguous 864-index coarse ranges, 225
eight-vertex/24-index bounds ranges, the query marker, and the
266-vertex/1,584-index material sphere. The maximum surface index is 58,080;
the complete vertex buffer contains 60,153 vertices. Index offsets are 0 for
LOD0, 345,600 for coarse, 540,000 for bounds, 545,400 for the marker, and
545,406 for the sphere, for 546,990 total indices. T-006 historically
established that packing; T-008 adds no fifth geometry buffer.

Meaningful surface payload is 1,393,944 vertex bytes plus 1,080,000 index
bytes. Bounds/query diagnostics add 54,156 bytes, while the packed
vertex/index resource widths including the sphere are
1,443,672/1,093,980 bytes. Their logical total is 2,537,652 bytes and the two
committed allocations total 2,621,440 bytes. The separately logged CPU-build
boundary covers fixture construction through LOD/query proof. T-006
historically measured 6,049.240 ms in Debug and 82.738 ms in Release; T-007
measured 8,098.750 ms on Debug hardware, 87.203 ms on Release hardware,
7,699.463 ms on Debug WARP, and 6,008.669 ms on focused WARP+GBV.
No separate T-008 construction time is promoted as an acceptance threshold;
the active suite and presentation-gate timings are recorded below.

## Frame acquisition, recording, and present

One frame:

1. extracts the current Direct3D frustum, tests all 225 terrain AABBs, chooses
   each visible chunk's LOD from finite camera-to-AABB distance, and logs a
   changed visible/total/LOD split;
2. obtains DXGI's current back-buffer index and selects that context;
3. waits only if that context's preceding submission is still in flight;
4. consumes its completed 12-timestamp sample, retires the submission, and
   resets bounded cursors;
5. reserves one 256-byte frame record and one CPU staging descriptor;
6. reserves the exact 12-query timestamp slice;
7. composes the 15-import/five-pass frame graph;
8. resets the allocator and shared command list;
9. begins `Frame`, writes `frame_begin`, and copies the 256-byte diagnostic
   record to the context probe outside the graph;
10. executes `Terrain`, `TexturedCube`, `Skybox`, `Water`, and `ToneMap` with
    graph-owned transitions between them;
11. writes `frame_end`, resolves 12 queries to the context readback slice, and
    ends `Frame`;
12. closes/executes, signals the next fence value, and marks the timing sample
    pending; and
13. calls `Present` without an unconditional fence wait.

The exact frame graph is:

```text
imports                15
passes                  5
dependencies            5
emitted transitions     6
elided transitions      34
```

`Terrain`, `TexturedCube`, `Skybox`, and `Water` render linear color into scene
color. `ToneMap` reads scene color and writes the current swap-chain buffer. The
six transitions cover scene-color render/read state, depth write/read state,
and back-buffer present/render state.

For `V0` visible LOD0 chunks, `Vc` visible coarse chunks, and `V=V0+Vc`, normal
submitted commands contain `V + 3` indexed scene draws plus one non-indexed
six-vertex water draw. `F4` adds `V + 1` diagnostic draws:

```text
LOD0 terrain chunks       1,536 * V0 indices
coarse terrain chunks       864 * Vc indices
material sphere          1,584 indices
visible chunk AABBs       F4 ? 24 * V indices : 0
terrain query marker      F4 ? 6 indices : 0
textured cube                 36 indices
skybox                        36 indices
visual water                  6 vertices
```

`ToneMap` adds `DrawInstanced(3, 1, 0, 0)`. Per frame there is one depth clear
and five texture-table binds: terrain/IBL, checker, sky radiance, water
radiance, and HDR scene color. The fixed smoke poses produce
`V0/Vc=0/93`, then `0/72`, then `1/60`,
for 80,352, 62,208, and 53,376 terrain-surface indices. The final split occurs
only in the smoke-only near phase and keeps both packed terrain index ranges
live. Bounds/query diagnostics are off by default.

The 256-byte frame record remains:

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

The diagnostic copy uses D3D12 buffer promotion/decay. The context fence
protects both upload source and probe destination until reuse.

Interactive presentation uses `Present(1, 0)`. Smoke uses `Present(0, 0)`
without tearing. Only `S_OK` counts as presented; occluded attempts still have
a submitted command list and fence checkpoint but do not satisfy the
successful-present target.

## PIX and timestamps

The stable hierarchy is:

```text
StaticSceneUpload

Frame
  Terrain
  TexturedCube
  Skybox
  Water
  ToneMap
```

One direct-queue timestamp heap/readback allocation is divided:

```text
context index       0        1         2
query base          0       12        24
readback offset     0       96       192
slice bytes        96       96        96
```

Each 12-query slice stores frame begin/end and begin/end for each of the five
passes. The frame interval includes the probe copy, all six graph transitions,
and all passes, but excludes its own query resolve. Readback is inspected only
after the normal context fence completes. Resize/shutdown drains consume
pending samples; timing introduces no separate wait.

## Resize, minimize, and shutdown

For an effective nonzero resize, the renderer:

1. drains the direct queue;
2. consumes pending timing samples and retires all completed contexts;
3. releases three back-buffer references, depth, and HDR scene color;
4. calls `ResizeBuffers`;
5. reacquires/names back buffers and recreates RTVs;
6. recreates the extent-matched depth texture plus writable/read-only DSVs; and
7. recreates the `R16G16B16A16_FLOAT` scene texture, RTV, and fixed-slot SRV.

A minimized window publishes no zero extent and submits no frame. A duplicate
restore extent is a no-op.

Shutdown drains, consumes all samples, retires every context, releases
swap-chain/depth/HDR resources, command/query storage, static resources,
descriptors, root signatures/PSOs, fence/event state, and is safe to call
again. `Device::validate_debug_state` then rejects DirectX corruption/errors,
discarded bounded messages, or live renderer-owned D3D12 children.

`full_queue_drains == resize_count + 1` covers effective resizes plus
shutdown. The static-upload wait is not counted as a full drain.

## Fixed presentation smoke

Run:

```powershell
.\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke
.\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp
.\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp --gpu-validation
```

Hardware requires 1,000 successful presents, normal WARP requires 600, and
focused GPU-validated WARP requires 120. The paths:

- resize hardware/normal WARP from `1280x720` to `960x600`; focused GBV alone
  uses `640x360 -> 480x300`, preserving the same aspect sequence;
- start with 93 of 225 terrain chunks visible at a `0/93` LOD0/coarse split,
  retain that after resize, then script yaw `1.25` radians so 72 remain visible
  at a `0/72` split from three quarters through seven eighths, then move only
  the smoke camera to `(16, -1, 0)` with the same yaw/pitch for the final
  eighth so 61 remain at `1/60`;
- exercise all three contexts;
- exercise both terrain fill modes, all three material views, and both
  environment modes;
- prove `225 * frame_submissions` chunk tests, visible-plus-culled conservation,
  actual LOD0/coarse surface and bounds draws/indices, min/max/last visibility,
  exact `0.603515625`-meter maximum geometric error, graph, texture-binding, upload,
  descriptor, and timestamp accounting;
- prove scene/sky matrix-change counts `4/3`, because the final translation
  changes the scene matrix but not the translation-free sky matrix;
- prove the default-off F4 diagnostics through exactly 2,790 bounds draws and
  30 query-marker draws;
- prove one static upload, four geometry buffers, three material arrays, four
  HDR environment textures, ten persistent descriptors, and 79/284,608
  environment upload accounting;
- prove HDR scene texture/RTV/SRV creation counts equal `resize_count + 1`;
- prove 12 timestamps and one resolve per submission, with 36 global slots;
- retire every submission before final validation; and
- report zero D3D12/DXGI corruption/errors and no live children.

Hardware and normal WARP also minimize/restore and compare the complete
statistics snapshot to prove no counter changes while minimized. T-006
historically completed Debug/Release hardware, Debug WARP, and focused GPU
validation with clean Direct3D state. T-007's historical four-phase schedule uses
`Q=F/8` and `Q * (2*A + 4*B + C + D)`, with
`A/B/C/D=93/93/72/61`. Its hardware/WARP/GBV paths produced
86,375/51,825/10,365 visible chunks, including 125/75/15 LOD0 draws and
86,250/51,750/10,350 coarse draws. Their total terrain surface-index counts
were 74,712,000/44,827,200/8,965,440. Hardware Debug/Release, normal WARP, and
focused GBV passed exact accounting with zero corruption/errors and zero live
child objects. T-008 retains the same schedule and expected totals; its active
Debug and Release test runs passed them exactly. Debug completed all
`150/150` tests in 195.60 seconds, with hardware/WARP/WARP+GBV presentation
times of 8.61/83.60/66.85 seconds. Release completed all `150/150` in 157.45
seconds, with corresponding 2.06/78.66/60.14-second gates.

The direct final Debug RTX 4070 hardware smoke submitted 1,000 frames with
86,375 visible and 138,625 culled chunks, 125/86,250 LOD0/coarse draws, and
192,000/74,520,000 LOD0/coarse indices. It retained a 0.603516-meter maximum
coarse error, measured GPU frame average/maximum 0.125/0.681 ms and Terrain
average/maximum 0.105/0.285 ms, and reported zero corruption, zero errors, zero
live child objects, and only the two expected `ReportLiveDeviceObjects`
warnings. Smoke checks commands and accounting, not pixels.

## Explicit non-goals

T-008 does not expose a public/general upload allocator, global upload ring,
persistent descriptor allocator, generic deferred-destruction service, or
image readback. Its fixed scene, ten-slot heap, query storage, HDR target, and
environment maps are focused infrastructure, not a general scene/material/
probe system.

The graph remains frame-local, direct-queue, serial, and whole-resource. There
is no graph-owned transient allocation, aliasing, subresource tracking,
parallel recording, copy queue, async compute, enhanced barriers, automatic
exposure, HDR display output, or texture streaming.

This modern chunked HDR implementation does not broaden Shark beyond its
approved San Andreas-class local-sandbox feature ceiling. T-006 historically
completed the bounded `241x241`-sample, `960x960`-meter capacity fixture.
`T-007` completed its fixed-seed natural rolling heights on July 19, 2026 with
unchanged topology, resources, and canonical queries. Its final near-pose phase
kept both terrain index ranges live; hardware Debug/Release, normal WARP, and
focused GBV validation passed as historical evidence.

W-001 consumes the dry spawn, basin core, and waterline while retaining four
geometry buffers and every frame-resource/lifetime rule. Its six-vertex
`SV_VertexID` draw defines the authoritative water as the intersection of the
local `64/56` X/Z half-extent quad domain with warped `rho <= 1`, selecting the
spawn-side component. It adds no water buffer, texture, resource, descriptor,
or static upload. The frame now has 15 imports, five passes, five dependencies,
six transitions, 34 elisions, and five texture bindings. Sky renders before
premultiplied transparent water; canonical-terrain depth testing determines the
visible shoreline. Terrain remains unchanged and no fluid simulation is
claimed. PHY-002 feeds a supported simulation position through the existing
three sphere-translation root constants without changing frame-resource or
presentation accounting. Rain remains
deferred under the San Andreas-class ceiling. See
[ENGINE_PLAN.md](ENGINE_PLAN.md) for the active increment queue.
