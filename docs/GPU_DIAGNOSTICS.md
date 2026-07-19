# Direct3D 12 GPU Diagnostics Contract

- **Completed through:** `T-006`
- **Last verified:** July 19, 2026

Shark's GPU diagnostics use fixed-capacity PIX events and direct-queue
timestamps whose readback is delayed until the owning frame-context fence
completes. T-004 retains the exact four-pass, 15-import, six-transition,
31-elision, four-texture-bind, ten-timestamp frame contract while making
terrain surface and bounds draw counts proportional to visible chunks. It adds
no normal-frame queue drain. T-006 preserves those boundaries while scaling
the terrain counters to 225 chunks, recording payload/resource budgets, and
gating bounds/query draws behind the default-off `F4` diagnostic toggle.

## PIX marker contract

The engine links the pinned `WinPixEventRuntime` with `USE_PIX_RETAIL`, so
markers are present in Debug and Release. The standalone PIX application is a
development prerequisite; Shark neither installs nor launches it.

| Marker | Color | Frequency | Boundary |
|---|---:|---:|---|
| `StaticSceneUpload` | 3 | once | cube and packed terrain-LOD/chunk-bounds/marker/sphere buffer copies; checker and retained DDS copies; 36 terrain-material and 79 HDR-environment subresource copies; 13 initialization barriers |
| `Frame` | 1 | per submission | frame timestamp interval, 256-byte probe copy, complete graph execution, and timestamp resolve |
| `Terrain` | 4 | per frame | HDR/depth clear, material/IBL binding, selected LOD0/coarse chunk surfaces, sphere, matching visible chunk bounds, and marker draws |
| `TexturedCube` | 2 | per frame | HDR/depth binding, checker binding, and cube draw |
| `Skybox` | 3 | per frame | HDR/read-only-depth binding, radiance/fallback state, and sky draw |
| `ToneMap` | 5 | per frame | back-buffer binding, HDR scene-color binding, and fullscreen draw |

The four pass events are nested sequentially inside `Frame`. Graph barriers
occur outside pass callbacks but inside `Frame`. RAII event scopes close any
event already begun when a result-returning command fails.

The static marker is counted only after its command list executes. Frame/pass
events and timestamp accounting are committed only after the frame receives
its normal completion-fence checkpoint.

## Fixed query/readback storage

One direct-queue timestamp query heap contains 30 slots. One persistently
mapped 240-byte readback buffer is split across the three swap-chain frame
contexts:

| Context | Queries | Base | Readback bytes | Offset |
|---:|---|---:|---|---:|
| 0 | `0..9` | 0 | `0..79` | 0 |
| 1 | `10..19` | 10 | `80..159` | 80 |
| 2 | `20..29` | 20 | `160..239` | 160 |

Each result is one 64-bit timestamp. A frame reserves its complete ten-query
local slice. An eleventh local query fails rather than growing or overwriting
storage. Successful smoke accounting therefore reports:

```text
timestamp_query_capacity   == 30
timestamp_query_high_water == 10
```

## Ten-query frame layout

Each submitted frame writes:

| Local index | Name |
|---:|---|
| 0 | `frame_begin` |
| 1 | `terrain_begin` |
| 2 | `terrain_end` |
| 3 | `textured_cube_begin` |
| 4 | `textured_cube_end` |
| 5 | `skybox_begin` |
| 6 | `skybox_end` |
| 7 | `tone_map_begin` |
| 8 | `tone_map_end` |
| 9 | `frame_end` |

The command-list sequence is:

```text
begin PIX "Frame"
  frame_begin
  copy diagnostic FrameProbe
  graph: SceneColor -> RENDER_TARGET
  PIX/timestamps "Terrain"
  PIX/timestamps "TexturedCube"
  graph: depth -> DEPTH_READ
  PIX/timestamps "Skybox"
  graph: BackBuffer -> RENDER_TARGET, SceneColor -> PIXEL_SHADER_RESOURCE
  PIX/timestamps "ToneMap"
  graph finals: BackBuffer -> PRESENT, depth -> DEPTH_WRITE
  frame_end
  resolve ten timestamps to this context's readback slice
end PIX "Frame"
close, execute, signal context fence, present
```

CPU decoding requires:

```text
frame_begin <= terrain_begin <= terrain_end
            <= textured_cube_begin <= textured_cube_end
            <= skybox_begin <= skybox_end
            <= tone_map_begin <= tone_map_end
            <= frame_end
```

Equal timestamps and zero-length intervals are valid. Incomplete, reversed, or
aggregate-overflowing samples fail without partially mutating totals.

## Fence-delayed consumption

After submission, the context records its normal direct-fence completion value
and marks timestamp results pending. Before that context can be reused, Shark:

1. checks the direct queue's completed fence;
2. performs the existing bounded reuse wait only if necessary;
3. consumes the ten mapped results only after completion;
4. clears the pending flag; and
5. retires the submission and resets the bounded timestamp arena.

Resize and shutdown use their already-required full queue drains and consume
all completed pending samples. Timing adds no independent fence, wait, drain,
readback allocation, or resource barrier. `gpu_timing_samples` may temporarily
trail submissions during execution; explicit shutdown must make them equal
before final smoke validation.

## Timing boundaries

The direct queue's nonzero timestamp frequency converts native ticks to
milliseconds:

```text
milliseconds = ticks * 1000 / frequency
average milliseconds = total ticks * 1000 / frequency / sample count
```

`frame_end - frame_begin` includes the probe copy, all six graph transitions,
and all four passes. It excludes query resolution, command-list close, CPU
recording, queue submission, fence signal, `Present`, display latency, and the
static upload.

Pass durations are end minus begin for `Terrain`, `TexturedCube`, `Skybox`, and
`ToneMap`. They include commands inside each callback and exclude graph
barriers, CPU-side access validation, terrain frustum/AABB tests, distance
measurement, and LOD selection completed before command recording. These are
stable measurement boundaries, not performance thresholds.

## Public statistics and exact smoke invariants

After the smoke's shutdown drain:

```text
pix_static_upload_events == static_upload_submissions == 1
pix_frame_events == frame_submissions
pix_pass_events == render_graph_pass_executions
pix_terrain_events == frame_submissions
pix_textured_cube_events == frame_submissions
pix_skybox_events == frame_submissions
pix_tone_map_events == frame_submissions

gpu_timestamp_frequency_hz > 0
timestamp_query_capacity == 30
timestamp_query_high_water == 10
timestamp_queries_written == frame_submissions * 10
timestamp_resolve_batches == frame_submissions
gpu_timing_samples == frame_submissions

render_graph_resource_imports == frame_submissions * 15
render_graph_pass_executions == frame_submissions * 4
render_graph_dependencies == frame_submissions * 3
render_graph_transition_barriers == frame_submissions * 6
render_graph_elided_transitions == frame_submissions * 31
texture_bindings == frame_submissions * 4
terrain_material_bindings == frame_submissions
```

Scene and resource invariants include:

```text
terrain_chunk_count == 225
terrain_chunks_tested == frame_submissions * 225
terrain_chunks_visible + terrain_chunks_culled
    == terrain_chunks_tested
terrain_draw_calls == terrain_chunks_visible
terrain_lod0_draw_calls + terrain_coarse_draw_calls
    == terrain_draw_calls
terrain_lod0_indices == terrain_lod0_draw_calls * 1536
terrain_coarse_indices == terrain_coarse_draw_calls * 864
terrain_indices == terrain_lod0_indices + terrain_coarse_indices
terrain_solid_draw_calls + terrain_wireframe_draw_calls
    == terrain_draw_calls
terrain_shaded_draw_calls + terrain_material_weight_draw_calls
    + terrain_shading_normal_draw_calls == terrain_draw_calls

terrain_visible_chunk_min == 71
terrain_visible_chunk_max == 93
terrain_visible_chunk_last == 71
terrain_lod0_chunks_last == 4
terrain_coarse_chunks_last == 67

material_sphere_draw_calls == frame_submissions
terrain_bounds_draw_calls == 2790
terrain_bounds_indices == 66960
terrain_query_marker_draw_calls == 30
terrain_query_marker_indices == 180
cube_draw_calls == frame_submissions
skybox_draw_calls == frame_submissions
tone_map_draw_calls == frame_submissions

terrain_vertex_count == 58081
terrain_index_count == 540000
terrain_lod0_index_count == 345600
terrain_coarse_index_count == 194400
terrain_maximum_geometric_error == 0.5
terrain_bounds_vertex_count == 1800
terrain_bounds_index_count == 5400
terrain_query_marker_vertex_count == 6
terrain_query_marker_index_count == 6
material_sphere_vertex_count == 266
material_sphere_index_count == 1584
material_sphere_indices == frame_submissions * 1584

terrain_surface_vertex_payload_bytes == 1393944
terrain_surface_index_payload_bytes == 1080000
terrain_diagnostic_vertex_payload_bytes == 43344
terrain_diagnostic_index_payload_bytes == 10812
terrain_vertex_resource_bytes == 1443672
terrain_index_resource_bytes == 1093980
terrain_geometry_resource_bytes == 2537652
terrain_geometry_committed_bytes == 2621440

environment_texture_creations == 4
environment_srv_creations == 4
environment_cubemap_srv_creations == 3
environment_subresources_uploaded == 79
environment_source_bytes_uploaded == 284608
environment_hdr_resources == 4
persistent_texture_descriptors == 10

hdr_scene_color_creations == resize_count + 1
hdr_scene_color_rtv_creations == resize_count + 1
hdr_scene_color_srv_creations == resize_count + 1
```

Each smoke starts and resizes with 93 visible chunks at a `3/90` LOD0/coarse
split and reaches 71 at `4/67` after its scripted `1.25`-radian yaw. The same
counts are locked by focused frustum and LOD-selection tests. Surface index
counts are 82,368 and 64,032 at those poses. `terrain_draw_calls` and
LOD/fill/material-view counters count actual D3D12 draws, not one logical
Terrain pass per frame. Bounds and the query marker are off by default; the
smoke enables them for one 30-frame interval, producing 2,790 and 30 draws.
The one-per-pass identity therefore uses `pix_terrain_events`:

```text
pix_terrain_events + cube_draw_calls + skybox_draw_calls
    + tone_map_draw_calls == render_graph_pass_executions
```

The Debug and Release 1,000-frame hardware paths require:

```text
terrain_chunks_tested/visible/culled == 225000/87500/137500
terrain_lod0_draw_calls == 3250
terrain_coarse_draw_calls == 84250
terrain_lod0_indices == 4992000
terrain_coarse_indices == 72792000
terrain_indices == 77784000
```

The Debug 600-frame WARP path requires:

```text
terrain_chunks_tested/visible/culled == 135000/52500/82500
terrain_lod0_draw_calls == 1950
terrain_coarse_draw_calls == 50550
terrain_lod0_indices == 2995200
terrain_coarse_indices == 43675200
terrain_indices == 46670400
```

The focused Debug 120-frame GPU-validation path requires:

```text
terrain_chunks_tested/visible/culled == 27000/10500/16500
terrain_lod0_draw_calls == 390
terrain_coarse_draw_calls == 10110
terrain_lod0_indices == 599040
terrain_coarse_indices == 8735040
terrain_indices == 9334080
```

The statistics field named `environment_source_bytes_uploaded` counts
meaningful bytes of the four derived GPU uploads, not the CPU-only 32,768-byte
latitude-longitude source. D3D12 row-pitch padding is excluded.

The terrain byte fields distinguish meaningful surface and diagnostic payloads,
logical committed-resource widths, and the allocator-reported committed size.
The startup log separately measures the CPU boundary from capacity-fixture
construction through chunk/LOD generation and query proof. The latest hardware
runs measured 6,049.240 ms in Debug and 82.738 ms in Release; those values are
evidence, not portable performance thresholds.

Both environment modes must run at least once and sum to frame submissions:

```text
image_based_lighting_frames > 0
procedural_daylight_frames > 0
image_based_lighting_frames + procedural_daylight_frames
    == frame_submissions
```

For every aggregate, frame total/last must cover the sum of the four pass
intervals; frame maximum must be at least each pass maximum; and every
minimum must be no greater than its maximum. Duration magnitude is not gated.

While minimized, the complete `RendererStats` snapshot must remain unchanged,
covering frame submission, graph work, every draw, uploads, bindings, clears,
PIX events, timestamp writes/resolves/consumption, and HDR resource accounting.

The final log reports:

```text
timestamp-queries(high/capacity)=10/30
gpu-frame-ms(avg/max)=.../...
gpu-Terrain-ms(avg/max)=.../...
gpu-TexturedCube-ms(avg/max)=.../...
gpu-Skybox-ms(avg/max)=.../...
gpu-ToneMap-ms(avg/max)=.../...
```

## Verification and manual PIX acceptance

Focused tests cover the exact three-context partition, complete nested
four-pass order, zero-length intervals, malformed/reversed samples, overflow
without partial mutation, fixed-capacity allocation, fence retirement, and
high-water accounting. Terrain-frustum tests separately lock Direct3D clip
half-spaces, conservative tangent/intersection behavior,
positive-scale-invariant extraction, ordinary and extreme-far reversed-Z
ranges, invalid matrices, and the 93-to-71 smoke poses. Terrain-LOD tests
lock shortest 3D camera-to-closed-AABB distance, the inclusive
`error <= 0.008 * distance` threshold, invalid-input rejection, and the
`3/90 -> 4/67` LOD split. The production frame-pipeline test locks 15 imports,
four access sets, three dependencies, six transitions, 31 elisions, and exact
callback order.

Run the runtime paths with:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp --gpu-validation
```

Hardware requires 1,000 successful presents, normal WARP requires 600, and
focused WARP GPU validation requires 120.

For manual PIX acceptance:

1. Capture a hardware frame and confirm one `Frame` with sequential nested
   `Terrain`, `TexturedCube`, `Skybox`, and `ToneMap` scopes.
2. At the initial pose, confirm `Terrain` contains three 1,536-index LOD0 draws,
   90 864-index coarse draws, and one sphere. Toggle `F4` and confirm 93 matching
   24-index bounds draws plus one marker; they are absent when the toggle is
   off. At the scripted pose, confirm four LOD0 and 67 coarse draws. Cube and
   sky each retain one indexed draw; `ToneMap` retains one fullscreen
   non-indexed draw.
3. Confirm all six graph transitions occur inside `Frame` but outside the
   applicable pass intervals.
4. Confirm material/environment resources remain in pixel-shader-read state.
5. In a startup-inclusive capture, confirm one `StaticSceneUpload` containing
   the packed 345,600-index LOD0 prefix, 194,400-index coarse suffix, 225 chunk
   bounds, environment copies, and 13 initialization barriers.
6. Confirm the log reports `10/30`, one timing sample per retired frame, and
   finite frame plus four-pass aggregates.
7. Confirm no D3D12/DXGI error or corruption message.

## Explicit non-goals

T-006 adds no live HUD, Dear ImGui, automated PIX capture, capture-file
management, CPU profiler, pipeline-statistics or occlusion queries, static
upload timing, dynamic pass profiler, query-heap growth, graph-wide automatic
instrumentation, cross-queue clock calibration, stable-power-state control,
multi-queue timing, or performance threshold.

The diagnostics remain proportional to Shark's bounded San Andreas-class
local-sandbox goal. `T-006` was completed on July 19, 2026 with a bounded
`241x241`-sample capacity fixture, explicit memory/startup evidence, and clean
hardware, WARP, and GPU-validation runs. The next increment is `T-007`: replace
the shallow alternating capacity heights with fixed-seed, mostly flat natural
rolling terrain; no lake is added yet.
