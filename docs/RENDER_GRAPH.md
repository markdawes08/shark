# Minimal Render-Graph Contract

- **Completed through:** `S-002A`
- **Presentation integration verified through:** `S-002A`
- **Last updated:** July 18, 2026

G-006 moves the existing textured-cube frame behind Shark's first render graph
without changing the visible scene. S-002 extends it to ordered cube/sky
passes, read-only depth, and exact persistent texture-read declarations. T-001
adds the first `Terrain` pass and explicitly declares the cube and terrain
input-assembler buffers. S-002A replaces cubemap sampling with a procedural
daylight sky and removes that now-unused texture from the per-frame graph while
retaining its startup asset proof. The graph remains a small,
platform-independent planner plus a Direct3D 12 legacy-barrier executor. It
proves declared resource access, deterministic dependency compilation, and
centralized frame barriers before later work adds more passes or graph-owned
resources.

## Boundary and ownership

The `shark::render_graph` module depends on Shark Core, but it contains no
Windows, DXGI, Direct3D 12, WRL, or COM types. It owns only frame planning data:

- builder-scoped resource and pass handles;
- imported-resource names, external IDs, and initial/final states;
- pass names, callbacks, access declarations, and dependencies;
- compiled ordering and transition records; and
- compilation and execution statistics.

An imported resource remains owned by its caller. `ExternalResourceId` is an
opaque engine value that lets the backend resolve a graph declaration to the
native object supplied for that execution. Importing a resource never retains,
releases, creates, destroys, aliases, or resizes the native resource.

`ResourceHandle` and `PassHandle` values belong to exactly one `GraphBuilder`.
Default handles, out-of-range handles, and handles from another builder are
rejected. Resource names, pass names, and external IDs must each be unambiguous
within a builder. Moving a builder transfers its owner token and rekeys the
moved-from builder so the two can never accept each other's handles. Compilation
consumes a builder exactly once; later declarations or compilation attempts
fail. A compiled graph is move-only and owns the callbacks moved from its
builder.

## Declaration contract

`GraphBuilder::import_resource` requires:

- a nonempty unique name;
- a unique external resource ID;
- a valid initial state; and
- a valid final state.

The first state vocabulary is deliberately narrow:

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

Passes require a nonempty unique name and a valid callback. Each pass may
declare a whole resource exactly once. Read access is currently valid for
`depth_read`, `pixel_shader_read`, `shader_read`, `vertex_buffer`,
`index_buffer`, and `copy_source`; write access is valid only for
`render_target`, `depth_write`, and `copy_destination`.

The graph tracks whole resources, not individual mip levels, array slices,
planes, buffer ranges, RTV/DSV descriptors, clear values, or load/store
operations. A pass callback still owns its draw/dispatch commands and explicit
attachment binding.

## Compilation and ordering

Compilation combines explicit pass dependencies with resource hazards inferred
from declaration order:

- read-after-write (RAW) makes the reader depend on the preceding writer;
- write-after-read (WAR) makes the writer depend on all readers since the
  preceding write; and
- write-after-write (WAW) makes a later writer depend on the preceding writer.

Read-only passes do not acquire false dependencies on one another. Duplicate
edges are folded into one dependency.

The compiler performs a stable topological sort. When more than one pass is
ready, the pass declared earliest is selected first. Independent passes
therefore retain declaration order. Explicit dependencies can move a
later-declared pass ahead of an earlier one, while inferred hazards preserve
the declaration sequence of conflicting accesses. A cycle fails compilation
with a graphics invalid-state error that names the involved passes.

After ordering, the compiler walks each imported resource's state:

1. begin at the import's initial state;
2. emit a transition immediately before a pass when its declared state differs;
3. elide an equal-state transition, including the legacy-native
   `common`/`present` alias;
4. update the tracked state after each access; and
5. emit a final transition when the last tracked state differs from the
   import's requested final state.

`CompiledGraphStats` reports imported resources, passes, dependencies, emitted
transitions, and elided equal-state transitions.

## Execution and callback validation

`CompiledGraph::execute` runs compiled passes serially:

1. record every transition scheduled before the pass;
2. invoke the pass callback with a `PassContext`;
3. continue to the next pass; and
4. after all callbacks succeed, record final transitions.

The callback can resolve an external resource ID only through
`PassContext::read` or `PassContext::write` using the exact handle and access
mode it declared. Undeclared access and a read/write mode mismatch return an
invalid-state error. An invalid handle returns an invalid-argument error.

Execution stops at the first transition-recorder or pass-callback failure.
`ExecutionStats` counts only successfully executed passes and successfully
recorded transitions.

## Direct3D 12 legacy-barrier executor

The G-006 D3D12 executor maps graph states exactly:

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

Each transition resolves its external ID against the bindings supplied for the
current execution and emits one
`D3D12_RESOURCE_BARRIER_TYPE_TRANSITION` over
`D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES`. A missing, duplicate, or null native
binding fails. Presentation validates the complete binding set before graph
execution, including resources whose transitions were elided. A no-op or
native-equivalent transition reaching the executor also fails because the
compiler must have elided it.

The recorder requires one valid graphics command list and reports its recorded
barrier count. Presentation cross-checks that count against both graph
execution and compiled transition statistics before submission.

## Current frame graph

Every non-minimized frame builds and compiles a fresh graph after acquiring and
staging its frame context. It imports:

| Import | Initial state | Final state | Current native binding |
|---|---|---|---|
| `BackBuffer` | `present` | `present` | DXGI's current swap-chain buffer |
| `DepthBuffer` | `depth_write` | `depth_write` | the current extent-matched `D32_FLOAT` texture |
| `CheckerTexture` | `pixel_shader_read` | `pixel_shader_read` | persistent checker texture |
| `CubeVertexBuffer` | `vertex_buffer` | `vertex_buffer` | persistent 24-vertex cube buffer |
| `CubeIndexBuffer` | `index_buffer` | `index_buffer` | persistent 36-index cube buffer |
| `TerrainVertexBuffer` | `vertex_buffer` | `vertex_buffer` | persistent terrain and bounds vertices |
| `TerrainIndexBuffer` | `index_buffer` | `index_buffer` | persistent terrain and bounds indices |

The graph contains three passes. `Terrain` declares:

- write access to `BackBuffer` as `render_target`;
- write access to `DepthBuffer` as `depth_write`;
- read access to `TerrainVertexBuffer` as `vertex_buffer`; and
- read access to `TerrainIndexBuffer` as `index_buffer`.

`TexturedCube` declares:

- write access to `BackBuffer` as `render_target`; and
- write access to `DepthBuffer` as `depth_write`; and
- read access to `CheckerTexture` as `pixel_shader_read`;
- read access to `CubeVertexBuffer` as `vertex_buffer`; and
- read access to `CubeIndexBuffer` as `index_buffer`.

`Skybox` declares:

- write access to `BackBuffer` as `render_target`;
- read access to `DepthBuffer` as `depth_read`; and
- read access to `CubeVertexBuffer` as `vertex_buffer`; and
- read access to `CubeIndexBuffer` as `index_buffer`.

The terrain callback resolves its four declared resources, clears color/depth,
draws the selected 6,144-index surface PSO, and draws the always-present
24-index bounds box. The cube callback resolves its five declared resources;
the procedural-sky callback resolves its four. Neither clears. Color/depth
hazards produce
`Terrain -> TexturedCube -> Skybox`, deduplicated to two dependencies. The
compiled frame therefore has:

```text
imports                 7
passes                  3
dependencies            2
emitted transitions     4
elided transitions      16
```

The emitted barriers are `PRESENT -> RENDER_TARGET` before `Terrain`,
`DEPTH_WRITE -> DEPTH_READ` before `Skybox`, and final
`RENDER_TARGET -> PRESENT` plus `DEPTH_READ -> DEPTH_WRITE`. All matching
attachment, checker-texture, vertex-buffer, and index-buffer
declarations/final states account for the 16 elisions.

The retained 256-byte diagnostic `CopyBufferRegion` remains outside the graph
and uses D3D12 buffer promotion/decay. That record contains 224 bytes of
matrix/daylight constants and the retained `FrameProbe` at byte 224. The
one-time vertex, index, checker, and cubemap uploads remain in the startup
upload path. The graph owns all four per-frame attachment barriers plus the
checker-texture and input-assembler declarations; it does not own startup
copies or the diagnostic buffer copy. The retained cubemap is not imported
because the procedural sky neither binds nor samples it.

T-001 extends the state vocabulary but retains planner/executor ownership.
Presentation writes the
outer frame timestamps and `Frame` PIX event outside graph execution. The
`Terrain`, `TexturedCube`, and `Skybox` callbacks write their own timestamp
pairs and nested PIX events inside their passes. Query allocation, resolution,
readback, and statistics remain presentation-owned rather than automatic graph
behavior.

## Accounting and verification

`PresentationStats` adds:

- `render_graph_compilations`;
- `render_graph_executions`;
- `render_graph_resource_imports`;
- `render_graph_pass_executions`;
- `render_graph_dependencies`;
- `render_graph_transition_barriers`; and
- `render_graph_elided_transitions`.

For a successful fixed presentation smoke:

```text
render_graph_compilations       == frame_submissions
render_graph_executions         == frame_submissions
render_graph_resource_imports    == frame_submissions * 7
render_graph_pass_executions     == frame_submissions * 3
render_graph_dependencies        == frame_submissions * 2
render_graph_transition_barriers == frame_submissions * 4
render_graph_elided_transitions  == frame_submissions * 16
terrain_draw_calls + cube_draw_calls + skybox_draw_calls
    == render_graph_pass_executions
```

The separate terrain bounds draw is part of the `Terrain` callback and therefore
does not add another graph pass.

The hardware and normal packaged-WARP presentation processes complete exactly
1,000 successful presents. The focused packaged-WARP GPU-validation process
completes 120. Every path executes resize and scripted camera movement at its
quarter and three-quarter checkpoints, followed by shutdown retirement and
final DirectX validation. The 1,000-frame paths also exercise
minimize/restore halfway through; the focused path intentionally skips that
already-covered interval and has a 180-second internal deadline plus a
240-second CTest timeout.

Unit tests permanently cover declaration rejection, single-use and move-safe
builder ownership, owner-scoped handles, stable independent and dependency
ordering, exact RAW/WAR/WAW edges, read-only independence, cycle rejection,
transition generation/elision, callback access validation, fail-fast execution,
exact legacy state mapping and alias rejection, whole-resource barrier
construction, and invalid native binding rejection.

## Explicit non-goals

G-006 adds no graph-owned or transient resource creation, placed-resource pool,
lifetime analysis, aliasing, resource pooling, subresource tracking, UAV state,
render-pass load/store policy, automatic RTV/DSV binding, pass culling or
merging, parallel command recording, secondary command lists, queue preference,
copy/compute queue activation, async compute, cross-queue fences, or enhanced
barriers.

It also adds no renderer scene extraction, public scene API, material system,
typed RHI resource handles, PSO cache, shader reflection, timing HUD, or
automatic graph-owned PIX/timestamp instrumentation. Graph compilation is
intentionally frame-local and serial. T-001's three named GPU intervals use the
existing presentation and callback boundaries without broadening this graph
contract. S-002A changes only the sky's declared inputs and the exact import
and elision counts; it adds no pass, transition, or scheduler behavior.

See [the presentation and frame-resource contract](GRAPHICS_PRESENTATION.md)
for submission and resize ownership, [the HLSL pipeline contract](GRAPHICS_PIPELINE.md)
for the commands recorded by the graphics passes, and
[the camera/cube contract](CAMERA_AND_CUBE.md) for shared conventions. See
[the skybox contract](SKYBOX.md) for the procedural daylight background and
[the terrain contract](TERRAIN.md) for the first pass and input-assembler
declarations, and
[the GPU diagnostics contract](GPU_DIAGNOSTICS.md) for the presentation-owned
marker/query lifecycle.
