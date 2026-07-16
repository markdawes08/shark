# Minimal Render-Graph Contract

- **Completed through:** `G-006`
- **Presentation integration verified through:** `G-007`
- **Last verified:** July 16, 2026

G-006 moves the existing textured-cube frame behind Shark's first render graph
without changing the visible scene. The graph is a small, platform-independent
planner plus a Direct3D 12 legacy-barrier executor. It proves declared resource
access, deterministic dependency compilation, and centralized frame barriers
before later work adds more passes or graph-owned resources.

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
shader_read
copy_source
copy_destination
```

Passes require a nonempty unique name and a valid callback. Each pass may
declare a whole resource exactly once. Read access is currently valid only for
`shader_read` and `copy_source`; write access is valid only for
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
| `shader_read` | `D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE` |
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

The graph contains one pass named `TexturedCube`. It declares:

- write access to `BackBuffer` as `render_target`; and
- write access to `DepthBuffer` as `depth_write`.

Its callback first resolves both declared writes, then binds and clears the
RTV/DSV pair, binds the existing camera/checker cube pipeline, and issues the
36-index draw. The compiled frame therefore has:

```text
imports                 2
passes                  1
dependencies            0
emitted transitions     2
elided transitions      2
```

The emitted barriers are `PRESENT -> RENDER_TARGET` before `TexturedCube` and
`RENDER_TARGET -> PRESENT` after it. The depth target is already and remains in
`DEPTH_WRITE`, so its pre-pass and final transitions are both elided.

The retained 256-byte diagnostic `CopyBufferRegion` remains outside the graph
and uses D3D12 buffer promotion/decay. The one-time vertex, index, and checker
uploads also remain in the startup upload path. G-006 centralizes the two
per-frame attachment barriers; it does not claim every command or resource in
Presentation.

G-007 leaves the planner and executor APIs unchanged. Presentation writes the
outer frame timestamps and `Frame` PIX event outside graph execution. The
`TexturedCube` callback writes its own begin/end timestamps and nested PIX event
inside the pass, after the graph's pre-pass barrier and before its final
barrier. Query allocation, resolution, readback, and statistics remain
presentation-owned rather than automatic graph behavior.

## Accounting and verification

`PresentationStats` adds:

- `render_graph_compilations`;
- `render_graph_executions`;
- `render_graph_resource_imports`;
- `render_graph_pass_executions`; and
- `render_graph_transition_barriers`.

For a successful fixed presentation smoke:

```text
render_graph_compilations       == frame_submissions
render_graph_executions         == frame_submissions
render_graph_resource_imports   == frame_submissions * 2
render_graph_pass_executions    == frame_submissions
render_graph_transition_barriers == frame_submissions * 2
cube_draw_calls                 == render_graph_pass_executions
```

The hardware, packaged-WARP, and packaged-WARP GPU-validation presentation
processes still complete exactly 1,000 successful presents, including resize,
minimize/restore, scripted camera movement, shutdown retirement, and final
DirectX validation.

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
intentionally frame-local and serial. G-007's first named GPU diagnostics use
the existing presentation and callback boundaries without broadening this graph
contract.

See [the presentation and frame-resource contract](GRAPHICS_PRESENTATION.md)
for submission and resize ownership, [the HLSL pipeline contract](GRAPHICS_PIPELINE.md)
for the commands recorded by `TexturedCube`, and
[the camera and textured-cube contract](CAMERA_AND_CUBE.md) for the unchanged
visible scene. See [the GPU diagnostics contract](GPU_DIAGNOSTICS.md) for the
presentation-owned marker and query lifecycle.
