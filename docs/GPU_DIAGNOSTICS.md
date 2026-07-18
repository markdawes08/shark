# Direct3D 12 GPU Diagnostics Contract

- **Completed through:** `T-003`
- **Last verified:** July 18, 2026

G-007 adds the first bounded GPU diagnostics to the existing G-006
presentation path. S-002 retains the frame interval and adds separate stable
PIX/timestamp intervals for the `TexturedCube` and `Skybox` graph passes.
T-001 adds the `Terrain` pass interval and expands the one-time named upload to
the complete static scene. S-002A keeps every marker and timestamp boundary
while changing `Skybox` to a b0-only procedural daylight draw. T-002 appends a
cyan, query-derived line-list pin to the existing `Terrain` interval without
adding or moving a PIX marker or timestamp. REN-001 moves scene/pass policy
and public statistics to `shark::renderer` while preserving every diagnostic
boundary and count. T-003 adds material resources and binding/accounting inside
the existing upload, graph, and `Terrain` scopes without adding a marker or
timestamp.

The diagnostics remain fixed-capacity and fence-delayed while the visible
contract uses three passes, ten resource imports, four attachment
transitions, exact vertex/index/material reads, and two per-frame texture-table
bindings. The
retained cubemap remains part of startup diagnostics but not the frame graph.

## PIX runtime and marker contract

Shark links the pinned `WinPixEventRuntime` dependency and compiles the engine
with `USE_PIX_RETAIL`. The command-list markers therefore remain available in
both Debug and Release builds. The standalone PIX application is a
development-machine prerequisite documented in
[the Windows setup guide](WINDOWS_SETUP.md); Shark does not install or launch
PIX automatically.

The marker names and boundaries are part of the diagnostics contract:

| Marker | Color index | Frequency | Command-list boundary |
|---|---:|---:|---|
| `StaticSceneUpload` | 3 | Once for the one static upload submission | Begins after the upload command list is reset; contains the cube and packed surface/bounds/query-marker terrain vertex/index buffer copies, checker-texture copy, six cubemap face/mip copies, 36 terrain-material subresource copies, and nine initialization transitions; ends before command-list close, execution, fence signal, and the required initialization wait |
| `Frame` | 1 | Once per submitted frame | Begins after the reusable frame command list is reset; contains the frame timestamp interval, 256-byte constants/probe copy, complete render-graph execution, and timestamp resolve; ends before command-list close, execution, fence signal, and `Present` |
| `Terrain` | 4 | Once per graph-pass execution | Begins after declared color/depth, terrain-buffer, and three material-array accesses are validated; contains the pass timestamp interval, material table/constants, selected solid/wireframe surface draw and material view, depth-tested bounds-line draw, and depth-tested cyan query-marker draw; ends before the callback returns |
| `TexturedCube` | 2 | Once per graph-pass execution | Begins inside the graph callback after declared resource access is validated; contains the pass timestamp interval, attachment and checker binding, and indexed draw without clearing; ends before the callback returns |
| `Skybox` | 3 | Once per graph-pass execution | Begins after its declared color/depth and cube-buffer access is validated; contains its timestamp interval, b0-only procedural-sky setup, read-only DSV, and reused 36-index draw; ends before the callback returns |

All three pass markers are nested sequentially inside `Frame`. Graph barriers
occur between/outside callbacks: color enters render-target state before
`Terrain`, depth enters read state before `Skybox`, and final color/depth states
are restored after `Skybox`. All four barriers remain inside `Frame` but
outside the pass timing intervals.

Markers use a small RAII command-list scope so an early result-return closes an
event that has already begun. The static event is counted after its command
list executes; frame/pass events and query/resolve accounting are committed
only after the frame receives its normal completion-fence checkpoint.
The copied 256-byte frame record contains 224 bytes of matrix/daylight
constants and the retained 32-byte `FrameProbe` beginning at byte 224.

## Fixed timestamp storage

The private D3D12 renderer backend owns one direct-queue timestamp query heap
with 24 slots and one persistently mapped 192-byte readback buffer. The storage
is partitioned into three non-overlapping slices, one for each swap-chain frame
context. REN-001 moves the fixed scene-named timestamp query layout and
accumulator to `engine/renderer/src/d3d12`; the public results are published
through `RendererStats`:

| Frame context | Query indices | Query base | Readback byte range | Readback offset |
|---:|---|---:|---|---:|
| 0 | `0..7` | 0 | `0..63` | 0 |
| 1 | `8..15` | 8 | `64..127` | 64 |
| 2 | `16..23` | 16 | `128..191` | 128 |

Each result is one 64-bit timestamp, so an eight-query context slice occupies
64 bytes. Each `FrameResourceState` owns a fixed-capacity eight-slot timestamp
arena. A frame allocates its entire local slice at offset zero; a ninth slot fails
instead of growing or overwriting storage.

`timestamp_query_capacity` reports the global 24-query heap capacity.
`timestamp_query_high_water` reports the largest per-context arena use and is
therefore exactly eight in a successful smoke.

## Eight-query frame layout

Each submitted frame writes exactly eight timestamps in this order:

| Local index | Name | Position |
|---:|---|---|
| 0 | `frame_begin` | Immediately after the `Frame` PIX marker begins and before the per-frame diagnostic probe copy |
| 1 | `terrain_begin` | Immediately after the `Terrain` marker begins and before attachment clears |
| 2 | `terrain_end` | Immediately after the surface, bounds, and query-marker draws and before the terrain marker ends |
| 3 | `textured_cube_begin` | Immediately after the `TexturedCube` marker begins and before attachment/resource setup |
| 4 | `textured_cube_end` | Immediately after the cube draw and before its marker ends |
| 5 | `skybox_begin` | Immediately after the `Skybox` marker begins and before read-only depth/resource setup |
| 6 | `skybox_end` | Immediately after the skybox draw and before its marker ends |
| 7 | `frame_end` | After graph execution and both final attachment transitions, before resolution |

The eight results are resolved as one batch to the current context's 64-byte
readback slice on the same direct command list:

```text
reset frame command list
  -> begin PIX "Frame"
  -> write frame_begin
  -> copy diagnostic frame probe
  -> graph transition: PRESENT -> RENDER_TARGET
  -> begin PIX "Terrain"
       -> write terrain_begin
       -> clear/bind/draw surface, bind/draw bounds, bind/draw query marker
       -> write terrain_end
     end PIX "Terrain"
  -> begin PIX "TexturedCube"
        -> write textured_cube_begin
        -> bind/draw
        -> write textured_cube_end
      end PIX "TexturedCube"
  -> graph transition: DEPTH_WRITE -> DEPTH_READ
  -> begin PIX "Skybox"
       -> write skybox_begin
       -> bind b0-only daylight constants/read-only depth and draw
       -> write skybox_end
     end PIX "Skybox"
  -> graph transitions: RENDER_TARGET -> PRESENT, DEPTH_READ -> DEPTH_WRITE
  -> write frame_end
  -> resolve eight timestamps to this context's readback slice
  -> end PIX "Frame"
  -> close, execute, signal the context completion fence, present
```

CPU validation requires the nested ordering:

```text
frame_begin <= terrain_begin <= terrain_end
            <= textured_cube_begin <= textured_cube_end
            <= skybox_begin <= skybox_end <= frame_end
```

Zero-length intervals are valid. Incomplete, reversed, or accumulation-
overflowing samples fail with a graphics error and do not partially update the
aggregates.

## Fence-delayed result consumption

Resolving timestamp data does not make it immediately safe for the CPU to read.
After submission, the frame context records both its normal completion-fence
value and that timestamp results are pending.

Before the same context can be reused, Shark:

1. reads the direct queue's completed fence value;
2. performs the existing context-reuse wait only if that context is still in
   flight;
3. consumes its eight mapped results only after the completion value covers the
   submitted frame;
4. clears the pending-result flag; and
5. begins the next context generation, which resets the bounded timestamp
   arena.

A pending slice cannot be allocated again or overwritten. Resize and shutdown
use their already-required full queue drain, consume all completed context
results, and then retire the contexts. G-007 adds no normal-frame queue drain,
extra timestamp fence, or synchronous readback wait.

During normal execution, `gpu_timing_samples` can temporarily trail
`frame_submissions` because up to one result per reusable context may still be
in flight. The presentation smoke calls `Renderer::shutdown` before checking final
statistics, so every submitted frame must have been consumed at that gate.

## Timing boundary definitions

The direct queue reports `gpu_timestamp_frequency_hz`. Shark retains timing
aggregates in native queue ticks and converts them for the final smoke log with:

```text
milliseconds = ticks * 1000 / frequency
average milliseconds = total ticks * 1000 / frequency / sample count
```

The log formats milliseconds to three decimal places.

The frame duration is `frame_end - frame_begin`. It includes:

- the diagnostic frame-probe buffer copy;
- all four render-graph attachment transitions;
- the complete `Terrain`, `TexturedCube`, and `Skybox` passes; and
- command processing between those timestamp writes.

It excludes timestamp resolution, command-list close, CPU recording time,
queue submission, the fence signal, swap-chain `Present`, display latency, and
the one-time static upload.

The terrain, cube, and sky durations are each their named end minus begin.
Terrain timing includes its clears, selected surface setup/draw, bounds
setup/draw, and query-marker setup/draw. Cube timing includes setup and indexed
draw; sky timing includes daylight-CBV setup, the read-only DSV, and indexed
draw. All three exclude graph barriers and callback resource-access validation.

These are stable measurement boundaries, not fixed performance expectations.
Exact values depend on the selected adapter, driver, power state, validation
mode, window state, and concurrent system work.

## Public statistics and smoke invariants

`RendererStats` exposes marker counts, bounded query accounting, the direct
queue frequency, timing sample count, and frame/pass total, minimum, maximum,
and last-consumed frame/terrain/cube/sky durations in ticks. "Last" follows
context retirement order; it is not a chronological newest-submission
identifier.

After the smoke's final shutdown drain, the following equalities must hold:

```text
pix_static_upload_events == static_upload_submissions == 1
pix_frame_events == frame_submissions
pix_pass_events == render_graph_pass_executions == frame_submissions * 3
pix_terrain_events == pix_textured_cube_events == pix_skybox_events
pix_terrain_events == frame_submissions

gpu_timestamp_frequency_hz > 0
timestamp_query_capacity == 24
timestamp_query_high_water == 8
timestamp_queries_written == frame_submissions * 8
timestamp_resolve_batches == frame_submissions
gpu_timing_samples == frame_submissions
render_graph_resource_imports == frame_submissions * 10
render_graph_elided_transitions == frame_submissions * 22
texture_bindings == frame_submissions * 2
terrain_material_bindings == frame_submissions

terrain_query_marker_draw_calls == frame_submissions
terrain_query_marker_indices == terrain_query_marker_draw_calls * 6
terrain_query_marker_vertex_count == 6
terrain_query_marker_index_count == 6

terrain_material_texture_array_creations == 3
terrain_material_srv_creations == 3
terrain_material_layers == 2
terrain_material_mip_levels == 6
terrain_material_subresources_uploaded == 36
terrain_material_source_bytes_uploaded == 32760
terrain_material_srgb_resources == 1
persistent_texture_descriptors == 5
```

Each submitted frame therefore contains five indexed draws: terrain surface,
terrain bounds, terrain query marker, textured cube, and sky. The marker is
packed into the existing terrain buffers, so startup still creates four
geometry buffers. The graph and diagnostics totals are ten imports, three
passes, two dependencies, four barriers, 22 elisions, and eight timestamp
writes per frame.

The aggregate checks also require:

```text
gpu_frame_total_ticks >= gpu_terrain_total_ticks
                       + gpu_textured_cube_total_ticks
                       + gpu_skybox_total_ticks
gpu_frame_last_ticks >= gpu_terrain_last_ticks
                      + gpu_textured_cube_last_ticks
                      + gpu_skybox_last_ticks
gpu_frame_max_ticks >= gpu_terrain_max_ticks
gpu_frame_max_ticks >= gpu_textured_cube_max_ticks
gpu_frame_max_ticks >= gpu_skybox_max_ticks
gpu_frame_min_ticks <= gpu_frame_max_ticks
gpu_terrain_min_ticks <= gpu_terrain_max_ticks
gpu_textured_cube_min_ticks <= gpu_textured_cube_max_ticks
gpu_skybox_min_ticks <= gpu_skybox_max_ticks
```

While the window is minimized, the smoke compares the complete `RendererStats`
snapshot and no counter may advance. This covers frame submission, graph work,
PIX markers, timestamp allocation/resolution/consumption, every draw, uploads,
matrix updates, texture bindings, clears, and resource accounting. Submitted
frames still retain exactly ten imports, four graph barriers, 22 elided
transitions, one checker-texture binding, and one terrain-material binding each.

The successful smoke summary contains these reproducible fields:

```text
pix-events(static/frame/pass)=...
timestamp-queries(high/capacity)=8/24
gpu-samples=...
gpu-frame-ms(avg/max)=.../...
gpu-Terrain-ms(avg/max)=.../...
gpu-TexturedCube-ms(avg/max)=.../...
gpu-Skybox-ms(avg/max)=.../...
```

"Reproducible" means the same named boundaries, bounded counts, and aggregation
rules are exercised every run. It does not mean GPU durations must be
bit-for-bit equal across runs or adapters.

## Verification and manual PIX acceptance

The unit tests cover:

- exact three-context query/readback partitioning;
- valid sequential terrain/cube/sky intervals nested within the frame interval,
  including zero-length intervals;
- rejection of incomplete and reversed intervals;
- overflow rejection without partial accumulator mutation; and
- fixed-capacity frame timestamp allocation, fence retirement, and retained
  high-water accounting.

The renderer-owned production `frame_pipeline` test additionally proves the
ten imports, `Terrain -> TexturedCube -> Skybox` callback order, exact
resource access sets, two dependencies, four transitions, and 22 elisions.
The scene-named timestamp tests move with the private renderer D3D12 helper.
Generic frame-resource state and the legacy transition recorder/tests remain
under the D3D12 RHI because those helpers contain no scene policy.

The presentation smoke exercises the full runtime lifecycle on hardware and
packaged WARP:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp --gpu-validation
```

Hardware and normal WARP complete 1,000 successful presents with lifecycle
checkpoints at frames 250, 500, and 750. Focused WARP GPU validation completes
120 with resize at frame 30 and rotation at frame 90, intentionally skipping
the normal paths' minimize/restore interval. It has a 180-second application
deadline and a 240-second CTest timeout. The per-submission diagnostics
invariants are identical across both budgets.

For manual PIX acceptance:

1. Launch the hardware `SharkSandbox.exe` from the current stable,
   non-preview PIX release.
2. Capture a rendered frame after the window appears.
3. Confirm the GPU event hierarchy contains one `Frame` scope with sequential
   nested `Terrain`, `TexturedCube`, and `Skybox` scopes. `Terrain` contains
   the indexed surface draw, indexed bounds-line draw, and indexed six-index
   cyan query-marker draw; each other pass has one indexed draw.
4. Confirm the graph owns all four attachment transitions inside `Frame`: the
   back buffer enters render-target state before `Terrain`, depth enters
   read state before `Skybox`, and both attachments return to their imported
   final states after `Skybox`. The checker texture and three material arrays
   remain in exact `pixel_shader_read` state and require no transition. The
   retained startup cubemap is absent from the per-frame graph.
5. Use a startup-inclusive capture or timing capture to verify the one-time
   `StaticSceneUpload` scope. A frame capture started after initialization is not
   expected to contain that startup-only event.
6. Confirm the application smoke log reports `8/24` query high-water/capacity,
   one timing sample per submitted frame after shutdown, and finite frame,
   `Terrain`, `TexturedCube`, and `Skybox` average and maximum durations.
7. Confirm the D3D12 debug layer reports no errors or corruption messages.

## Explicit non-goals

G-007 adds no live diagnostics HUD, Dear ImGui integration, automated PIX
capture control, capture-file management, CPU profiler, pipeline-statistics
queries, occlusion queries, or static-upload timestamp measurement.

It adds no general-purpose dynamic pass profiler, dynamic query-heap growth,
graph-wide automatic pass instrumentation, cross-queue clock calibration,
compute/copy queue timing, async compute, parallel command recording, or
cross-queue fence policy. It also adds no new queue drain or normal-frame
synchronous GPU readback.

The render graph remains frame-local and serial. T-001 adds only the explicit
terrain pass and persistent scene-buffer declarations; it does not add general
scheduling, transient-resource allocation, or queue selection. S-002A changes
the sky callback's resource declarations without changing its diagnostic
interval. T-002 adds one draw inside that existing interval; it adds no PIX
scope, timestamp, query heap slot, graph pass, resource, dependency, or barrier.
REN-001 moves the cube/daylight/skybox/terrain helpers into the private
renderer D3D12 backend and removes the public D3D12 `Presentation` class; it
does not change presentation operations, diagnostics, pixels, or accounting.
T-003 retains the same marker/timestamp layout while adding three persistent
material arrays, their exact graph reads, and one terrain table bind per frame.
See
[the render-graph contract](RENDER_GRAPH.md) for pass/barrier ownership and
[the presentation contract](GRAPHICS_PRESENTATION.md) for frame-context,
resize, and shutdown ownership. See [the skybox contract](SKYBOX.md) for the
visual and orientation acceptance procedure and
[the DDS cubemap contract](DDS_CUBEMAP.md) for the retained startup texture
contract.
See [the terrain contract](TERRAIN.md) for the canonical surface query,
deterministic tile, material fixture, and diagnostic rendering modes. `T-003`
was completed on July 18, 2026. The next increment is `S-003`, HDR environment
lighting.
