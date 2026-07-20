# Minimal Render-Graph Contract

- **Completed through:** `W-001`
- **Renderer integration verified through:** `PHY-003`
- **Last updated:** July 19, 2026

Shark's render graph is a small platform-independent planner with a Direct3D
12 legacy-barrier executor. W-001 keeps the frame-local, whole-resource HDR
composer while extending it to:
`Terrain -> TexturedCube -> Skybox -> Water -> ToneMap`. It adds no
graph-owned resource, subresource tracking, or multi-queue scheduling.

## Boundary and ownership

`shark::render_graph` depends on Shark Core and contains no Windows, DXGI,
Direct3D 12, WRL, or COM type. It owns:

- builder-scoped resource/pass handles;
- imported-resource names, external IDs, and initial/final states;
- pass callbacks, access declarations, and dependencies;
- stable compiled order and transition records; and
- compilation/execution statistics.

Imports remain caller-owned. `ExternalResourceId` is an opaque value used by
the backend to bind a graph declaration to a native object for one execution.
Importing never creates, resizes, retains, releases, aliases, or destroys the
native resource.

Handles belong to exactly one `GraphBuilder`. Default, out-of-range, and
foreign-builder handles are rejected. Resource names, pass names, and external
IDs are unique. Moving a builder transfers its owner token and rekeys the
moved-from instance. Compilation consumes the builder once; a compiled graph
is move-only and owns the moved callbacks.

The renderer-owned private `compose_frame_pipeline` function owns the 15
semantic IDs, import states, typed pass resource bundles, and exact production
order. The generic graph knows nothing about terrain, sky, IBL, or tone
mapping. The private renderer D3D12 backend supplies callbacks and native
bindings.

## Declaration and compilation

The bounded state vocabulary is:

```text
common
present
render_target
depth_write
depth_read
pixel_shader_read
shader_read
vertex_buffer
index_buffer
copy_source
copy_destination
```

A pass declares each whole resource at most once. Read access is valid for the
read, input-assembler, and copy-source states; write access is valid for render
target, depth write, and copy destination. The graph does not model mip/slice
ranges, buffer ranges, descriptors, clear values, attachment load/store
operations, or UAV hazards.

Compilation combines explicit dependencies with hazards in declaration order:

- RAW makes a reader depend on the preceding writer;
- WAR makes a writer depend on readers since the preceding write; and
- WAW makes a later writer depend on the preceding writer.

Duplicate edges collapse. Read-only passes do not gain false dependencies.
Stable topological sorting chooses the earliest-declared ready pass. Cycles
fail with a graphics invalid-state error naming the involved passes.

For each import, compilation begins at its declared initial state, emits a
transition before a pass only when the required state differs, updates the
tracked state after access, and emits a final transition when needed. Equal
states and the legacy-native `common`/`present` alias are elided.

## Execution and D3D12 mapping

`CompiledGraph::execute` records transitions scheduled before a pass, invokes
its callback, then records final transitions. `PassContext::read` and
`PassContext::write` resolve only the exact handles/modes declared by that
callback. Undeclared or mismatched access fails. Execution stops on the first
recorder or callback error and reports only completed work.

The D3D12 executor maps graph states directly:

| Graph state | Legacy D3D12 state |
|---|---|
| `common` | `D3D12_RESOURCE_STATE_COMMON` |
| `present` | `D3D12_RESOURCE_STATE_PRESENT` |
| `render_target` | `D3D12_RESOURCE_STATE_RENDER_TARGET` |
| `depth_write` | `D3D12_RESOURCE_STATE_DEPTH_WRITE` |
| `depth_read` | `D3D12_RESOURCE_STATE_DEPTH_READ` |
| `pixel_shader_read` | `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE` |
| `shader_read` | `D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE` |
| `vertex_buffer` | `D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER` |
| `index_buffer` | `D3D12_RESOURCE_STATE_INDEX_BUFFER` |
| `copy_source` | `D3D12_RESOURCE_STATE_COPY_SOURCE` |
| `copy_destination` | `D3D12_RESOURCE_STATE_COPY_DEST` |

Every transition is one whole-resource legacy barrier. Missing, duplicate, or
null native bindings fail even for an import whose transitions were elided.
The renderer cross-checks compiled, executed, and recorded counts before
submission.

## W-001 frame graph

Every non-minimized frame imports:

| Import | Initial/final state | Native role |
|---|---|---|
| `BackBuffer` | `present` | current swap-chain buffer |
| `SceneColor` | `pixel_shader_read` | resize-owned `R16G16B16A16_FLOAT` scene texture |
| `DepthBuffer` | `depth_write` | extent-matched `D32_FLOAT` texture |
| `CheckerTexture` | `pixel_shader_read` | persistent checker |
| `CubeVertexBuffer` | `vertex_buffer` | persistent cube vertices |
| `CubeIndexBuffer` | `index_buffer` | persistent cube indices |
| `TerrainVertexBuffer` | `vertex_buffer` | shared surface/chunk-bounds/marker/sphere vertices |
| `TerrainIndexBuffer` | `index_buffer` | packed LOD0/coarse/chunk-bounds/marker/sphere indices |
| `TerrainAlbedoLayers` | `pixel_shader_read` | two-layer sRGB array |
| `TerrainNormalLayers` | `pixel_shader_read` | two-layer linear array |
| `TerrainRoughnessLayers` | `pixel_shader_read` | two-layer linear array |
| `EnvironmentRadiance` | `pixel_shader_read` | six-mip HDR cube |
| `EnvironmentIrradiance` | `pixel_shader_read` | diffuse HDR cube |
| `EnvironmentPrefilteredSpecular` | `pixel_shader_read` | six-mip GGX HDR cube |
| `EnvironmentBrdfLut` | `pixel_shader_read` | split-sum HDR Texture2D |

`Terrain` writes `SceneColor`/depth and reads the terrain buffers, three
material arrays, irradiance, prefiltered specular, and BRDF LUT.
`TexturedCube` writes the same scene/depth attachments and reads checker plus
cube buffers. `Skybox` writes scene color, reads depth and cube buffers, and
reads radiance. `Water` then writes scene color with premultiplied transparency
and reads depth plus the existing environment radiance; its procedural quad
imports no geometry.
`ToneMap` writes `BackBuffer` and reads `SceneColor`.

Color/depth hazards produce the exact chain:

```text
Terrain -> TexturedCube -> Skybox -> Water -> ToneMap
```

The compiled accounting contract is:

```text
imports                15
passes                  5
dependencies            5
emitted transitions     6
elided transitions      34
```

The six transitions are:

1. `SceneColor`: pixel-shader read -> render target before `Terrain`;
2. depth: write -> read before `Skybox`;
3. `BackBuffer`: present -> render target before `ToneMap`;
4. `SceneColor`: render target -> pixel-shader read before `ToneMap`;
5. `BackBuffer`: render target -> present after `ToneMap`; and
6. depth: read -> write after `ToneMap`.

All persistent texture/buffer state matches are elided. The one-time static
uploads and per-frame diagnostic `CopyBufferRegion` remain outside the graph.

The graph pass callbacks own commands, not graph policy:

- `Terrain` clears scene/depth and issues one selected LOD0/coarse surface per
  visible chunk plus four material-sphere draws; default-off `F4` diagnostics add one
  magenta-bounds draw per visible chunk and the query marker;
- `TexturedCube` issues one checker-cube indexed draw;
- `Skybox` binds read-only depth and issues one far-depth indexed draw;
- `Water` issues one premultiplied six-vertex procedural draw with read-only
  depth; and
- `ToneMap` issues one non-indexed fullscreen-triangle draw.

If `V` of the 225 chunks are visible, normal `Terrain` contains `V + 4`
indexed draws and the frame contains `V + 6` indexed draws plus the water and
tone-map non-indexed draws. `F4` adds `V + 1` diagnostic draws without
changing the graph. The
initial/resized and scripted-overview smoke poses expose 93 and 72 chunks;
their `0/93` and `0/72` LOD0/coarse splits submit 80,352 and 62,208
terrain-surface indices. The final smoke-only near pose exposes 61 chunks at
`1/60` and submits 53,376 indices, keeping both packed D3D12 terrain ranges
live. These variable draws inside `Terrain` do not add passes or resources.
CPU frustum extraction, AABB tests, distance measurement, and LOD selection
occur before graph execution and likewise add no graph declaration.

## Diagnostics and verification

`RendererStats` must satisfy:

```text
render_graph_compilations       == frame_submissions
render_graph_executions         == frame_submissions
render_graph_resource_imports    == frame_submissions * 15
render_graph_pass_executions     == frame_submissions * 5
render_graph_dependencies        == frame_submissions * 5
render_graph_transition_barriers == frame_submissions * 6
render_graph_elided_transitions  == frame_submissions * 34

pix_terrain_events + cube_draw_calls + skybox_draw_calls + water_draw_calls
    + tone_map_draw_calls == render_graph_pass_executions
```

`terrain_draw_calls` counts actual visible chunk draws, each selecting its
1,536-index LOD0 or 864-index coarse range, so it is deliberately not a
graph-pass proxy.

The production composer test locks all 15 IDs, each pass's exact access set,
the dependency chain, transition order, final transitions, callback order,
and `15/5/5/6/34` statistics. Generic unit tests retain declaration rejection,
single-use/move-safe ownership, RAW/WAR/WAW ordering, read-only independence,
cycle rejection, transition elision, callback validation, fail-fast execution,
legacy state mapping, and invalid native binding coverage.

PIX/timestamp policy remains renderer-owned. The outer `Frame` interval wraps
the complete graph, while `Terrain`, `TexturedCube`, `Skybox`, `Water`, and
`ToneMap` callbacks own nested markers and timestamp pairs. Twelve timestamps
are allocated per frame context.

## Explicit non-goals

W-001 adds no graph-owned/transient resource creation, placed-resource pool,
lifetime/aliasing analysis, resource pooling, subresource tracking, UAV state,
automatic RTV/DSV binding, render-pass load/store policy, pass
culling/merging, parallel recording, queue preference, copy/compute queue,
cross-queue fences, async compute, or enhanced barriers.

Graph compilation remains frame-local, serial, and intentionally small. The
HDR target is still renderer-created and imported, not graph-created. This
keeps the implementation proportional to Shark's approved San Andreas-class
scope while leaving later renderer infrastructure possible when a measured
need appears.

T-006 historically completed clean hardware, WARP, and GPU-validation runs
without changing the exact `15/4/3/6/31` graph contract. `T-007` completed the
fixed-seed natural-height contract on July 19, 2026 and changed no graph
declaration or callback structure. Its then-active four-phase schedule changed
draw ranges within `Terrain`, not graph topology. Hardware Debug/Release, normal
WARP, and focused GBV graph/Direct3D validation passed as historical evidence.

W-001 adds transparent `Water` between `Skybox` and `ToneMap`. Its access set
writes `SceneColor` and reads depth plus the already-imported radiance cubemap.
The procedural `SV_VertexID` quad therefore adds no import or GPU resource.
Color and depth hazards produce five dependencies, while the six physical
transitions stay unchanged and three additional accesses elide, yielding the
exact active `15/5/5/6/34` contract. Rain remains deferred under the San
Andreas-class ceiling. PHY-003 supplies four interpolated sphere positions
inside the existing `Terrain` callback and preserves this graph exactly.
This component page no longer
duplicates the active queue; [ENGINE_PLAN.md](ENGINE_PLAN.md) is authoritative.
