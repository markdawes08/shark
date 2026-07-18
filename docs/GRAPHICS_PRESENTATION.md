# Renderer and Direct3D 12 Presentation/Frame-Resource Contract

- **Completed through:** `S-003`
- **Last verified:** July 18, 2026

`shark::renderer::Renderer` owns Shark's focused D3D12 scene/presentation
backend. S-003 preserves the triple-buffered fence-gated lifecycle and adds a
deterministic HDR environment, a resize-owned linear-HDR scene target, shared
terrain/material-sphere IBL, and a final tone-map pass. Normal frames still
submit and present without an unconditional post-frame queue drain.

## Public boundary and ownership

`Renderer` is a move-only public PIMPL. Its COM-free public records include
`RendererConfig`, `RenderFrameData`, `RenderStatus`, `RendererStats`,
shader-bytecode views, mesh views, and generic 2D/cube upload views.
`Renderer::create` consumes every pointer-based upload/bytecode view
synchronously and retains no caller CPU pointer.

`RendererConfig` carries:

- HWND/physical extent, clear color, and vertical-sync policy;
- cube, sky, terrain, material-sphere, and tone-map shader bytecode;
- checker/cubemap/terrain mesh/material uploads; and
- four environment-lighting uploads: radiance, diffuse irradiance,
  GGX-prefiltered specular, and split-sum BRDF LUT.

`RenderFrameData` carries finite scene/sky matrices, daylight settings, camera
world position, terrain fill/material views, and the environment mode. `F3`
selects image-based lighting or the retained procedural-daylight fallback.
The renderer receives query-derived terrain marker geometry but does not own or
perform canonical terrain queries.

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
- cube, sky, terrain solid/wireframe/line, material-sphere, and tone-map PSOs
  plus their focused root signatures;
- four committed geometry buffers: cube vertex/index and packed
  terrain/bounds/query-marker/material-sphere vertex/index;
- checker and retained S-001 DDS cubemap textures;
- three two-layer `32x32`, six-mip terrain material arrays;
- a `32x32` six-mip radiance cube, `8x8` irradiance cube, `32x32` six-mip
  GGX-prefiltered specular cube, and `32x32` split-sum BRDF LUT;
- one fixed ten-slot shader-visible SRV heap containing checker, retained DDS,
  three materials, four environment/IBL SRVs, and HDR scene color;
- one 30-entry timestamp heap and persistently mapped 240-byte readback buffer;
- three frame contexts; and
- the swap-chain frame-latency/lifetime state.

Each frame context owns one direct allocator, persistently mapped 64 KiB upload
buffer, one 256-byte default-heap probe destination, one 64-slot CPU-only
descriptor staging heap, bounded upload/descriptor/ten-query cursors,
generation and fence completion state, and pending timing metadata.

DXGI's current back-buffer index selects the context. CPU frame count is never
substituted. Fence zero means never submitted or already retired.

## Static upload

Renderer creation records one `StaticSceneUpload` direct-queue submission:

- cube and packed terrain/sphere vertex/index data;
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

The 266-vertex/1,584-index material sphere is packed after the terrain query
marker; it adds no fifth geometry buffer.

## Frame acquisition, recording, and present

One frame:

1. obtains DXGI's current back-buffer index and selects that context;
2. waits only if that context's preceding submission is still in flight;
3. consumes its completed ten-timestamp sample, retires the submission, and
   resets bounded cursors;
4. reserves one 256-byte frame record and one CPU staging descriptor;
5. reserves the exact ten-query timestamp slice;
6. composes the 15-import/four-pass frame graph;
7. resets the allocator and shared command list;
8. begins `Frame`, writes `frame_begin`, and copies the 256-byte diagnostic
   record to the context probe outside the graph;
9. executes `Terrain`, `TexturedCube`, `Skybox`, and `ToneMap` with graph-owned
   transitions between them;
10. writes `frame_end`, resolves ten queries to the context readback slice, and
    ends `Frame`;
11. closes/executes, signals the next fence value, and marks the timing sample
    pending; and
12. calls `Present` without an unconditional fence wait.

The exact frame graph is:

```text
imports                15
passes                  4
dependencies            3
emitted transitions     6
elided transitions      31
```

`Terrain`, `TexturedCube`, and `Skybox` render linear color into scene color.
`ToneMap` reads scene color and writes the current swap-chain buffer. The six
transitions cover scene-color render/read state, depth write/read state, and
back-buffer present/render state.

The submitted commands contain six indexed scene draws:

```text
terrain surface          6,144 indices
material sphere          1,584 indices
terrain AABB                 24 indices
terrain query marker          6 indices
textured cube                 36 indices
skybox                        36 indices
```

`ToneMap` adds `DrawInstanced(3, 1, 0, 0)`. Per frame there is one depth clear
and four texture-table binds: terrain/IBL, checker, sky radiance, and HDR scene
color.

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
  ToneMap
```

One direct-queue timestamp heap/readback allocation is divided:

```text
context index       0        1         2
query base          0       10        20
readback offset     0       80       160
slice bytes        80       80        80
```

Each ten-query slice stores frame begin/end and begin/end for each of the four
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

Hardware and normal WARP require 1,000 successful presents. Focused
GPU-validated WARP requires 120, a 180-second application deadline, and a
240-second CTest timeout. The paths:

- resize `1280x720` to `960x600`;
- script camera yaw;
- exercise all three contexts;
- exercise both terrain fill modes, all three material views, and both
  environment modes;
- prove the exact draws, graph, texture-binding, upload, descriptor, and
  timestamp accounting;
- prove one static upload, four geometry buffers, three material arrays, four
  HDR environment textures, ten persistent descriptors, and 79/284,608
  environment upload accounting;
- prove HDR scene texture/RTV/SRV creation counts equal `resize_count + 1`;
- prove ten timestamps and one resolve per submission, with 30 global slots;
- retire every submission before final validation; and
- report zero D3D12/DXGI corruption/errors and no live children.

The normal 1,000-frame paths also minimize/restore and compare the complete
statistics snapshot to prove no counter changes while minimized. Smoke checks
commands and accounting, not pixels.

## Explicit non-goals

S-003 does not expose a public/general upload allocator, global upload ring,
persistent descriptor allocator, generic deferred-destruction service, or
image readback. Its fixed scene, ten-slot heap, query storage, HDR target, and
environment maps are focused infrastructure, not a general scene/material/
probe system.

The graph remains frame-local, direct-queue, serial, and whole-resource. There
is no graph-owned transient allocation, aliasing, subresource tracking,
parallel recording, copy queue, async compute, enhanced barriers, automatic
exposure, HDR display output, or texture streaming.

This modern HDR implementation does not broaden Shark beyond its approved San
Andreas-class local-sandbox feature ceiling. `S-003` was completed on
July 18, 2026. The next increment is `T-004`, terrain chunk culling, followed
by `T-005`, bounded visual LOD.
