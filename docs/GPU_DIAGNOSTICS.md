# Direct3D 12 GPU Diagnostics Contract

- **Completed through:** `T-005`
- **Last verified:** July 19, 2026

Shark's GPU diagnostics use fixed-capacity PIX events and direct-queue
timestamps whose readback is delayed until the owning frame-context fence
completes. T-004 retains the exact four-pass, 15-import, six-transition,
31-elision, four-texture-bind, ten-timestamp frame contract while making
terrain surface and bounds draw counts proportional to visible chunks. It adds
no normal-frame queue drain. T-005 preserves those boundaries while exposing
the selected LOD0/coarse draws and indices plus the measured maximum geometric
error.

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
terrain_chunk_count == 16
terrain_chunks_tested == frame_submissions * 16
terrain_chunks_visible + terrain_chunks_culled
    == terrain_chunks_tested
terrain_draw_calls == terrain_chunks_visible
terrain_lod0_draw_calls + terrain_coarse_draw_calls
    == terrain_draw_calls
terrain_bounds_draw_calls == terrain_chunks_visible
terrain_lod0_indices == terrain_lod0_draw_calls * 384
terrain_coarse_indices == terrain_coarse_draw_calls * 240
terrain_indices == terrain_lod0_indices + terrain_coarse_indices
terrain_bounds_indices == terrain_chunks_visible * 24
terrain_solid_draw_calls + terrain_wireframe_draw_calls
    == terrain_draw_calls
terrain_shaded_draw_calls + terrain_material_weight_draw_calls
    + terrain_shading_normal_draw_calls == terrain_draw_calls

terrain_visible_chunk_min == 5
terrain_visible_chunk_max == 16
terrain_visible_chunk_last == 5
terrain_lod0_chunks_last == 3
terrain_coarse_chunks_last == 2

material_sphere_draw_calls == frame_submissions
terrain_query_marker_draw_calls == frame_submissions
terrain_query_marker_indices == frame_submissions * 6
cube_draw_calls == frame_submissions
skybox_draw_calls == frame_submissions
tone_map_draw_calls == frame_submissions

terrain_vertex_count == 1089
terrain_index_count == 9984
terrain_lod0_index_count == 6144
terrain_coarse_index_count == 3840
terrain_maximum_geometric_error == 0.140625
terrain_bounds_vertex_count == 128
terrain_bounds_index_count == 384
terrain_query_marker_vertex_count == 6
terrain_query_marker_index_count == 6
material_sphere_vertex_count == 266
material_sphere_index_count == 1584
material_sphere_indices == frame_submissions * 1584

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

Each smoke starts with all 16 chunks visible at an `8/8` LOD0/coarse split and
reaches five at a `3/2` split after its scripted `1.25`-radian yaw. The same
counts are locked by focused frustum and LOD-selection tests. At the two poses,
surface indices are 4,992 and 1,632; complete indexed scene counts are 7,038
and 3,414. `terrain_draw_calls`, LOD/fill/material-view counters, and
`terrain_bounds_draw_calls` count actual D3D12 draws, not one logical Terrain
pass per frame. The one-per-pass identity therefore uses `pix_terrain_events`:

```text
pix_terrain_events + cube_draw_calls + skybox_draw_calls
    + tone_map_draw_calls == render_graph_pass_executions
```

The normal 1,000-frame paths require:

```text
terrain_lod0_draw_calls == 6750
terrain_coarse_draw_calls == 6500
terrain_lod0_indices == 2592000
terrain_coarse_indices == 1560000
terrain_indices == 4152000
```

The focused 120-frame GPU-validation path requires:

```text
terrain_lod0_draw_calls == 810
terrain_coarse_draw_calls == 780
terrain_lod0_indices == 311040
terrain_coarse_indices == 187200
terrain_indices == 498240
```

The statistics field named `environment_source_bytes_uploaded` counts
meaningful bytes of the four derived GPU uploads, not the CPU-only 32,768-byte
latitude-longitude source. D3D12 row-pitch padding is excluded.

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
ranges, invalid matrices, and the 16-to-five smoke poses. Terrain-LOD tests
lock shortest 3D camera-to-closed-AABB distance, the inclusive
`error <= 0.008 * distance` threshold, invalid-input rejection, and the
`8/8 -> 3/2` LOD split. The production frame-pipeline test locks 15 imports,
four access sets, three dependencies, six transitions, 31 elisions, and exact
callback order.

Run the runtime paths with:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp --gpu-validation
```

Hardware and normal WARP require 1,000 successful presents. Focused WARP GPU
validation requires 120, with a 180-second application deadline and
240-second CTest timeout.

For manual PIX acceptance:

1. Capture a hardware frame and confirm one `Frame` with sequential nested
   `Terrain`, `TexturedCube`, `Skybox`, and `ToneMap` scopes.
2. At the initial pose, confirm `Terrain` contains eight 384-index LOD0 draws,
   eight 240-index coarse draws, one sphere, 16 matching 24-index bounds draws,
   and one marker. At the scripted pose, confirm those surface draws become
   three LOD0 and two coarse with five matching bounds. Cube and sky each
   retain one indexed draw; `ToneMap` retains one fullscreen non-indexed draw.
3. Confirm all six graph transitions occur inside `Frame` but outside the
   applicable pass intervals.
4. Confirm material/environment resources remain in pixel-shader-read state.
5. In a startup-inclusive capture, confirm one `StaticSceneUpload` containing
   the packed 6,144-index LOD0 prefix, 3,840-index coarse suffix, 16 chunk
   bounds, environment copies, and 13 initialization barriers.
6. Confirm the log reports `10/30`, one timing sample per retired frame, and
   finite frame plus four-pass aggregates.
7. Confirm no D3D12/DXGI error or corruption message.

## Explicit non-goals

T-005 adds no live HUD, Dear ImGui, automated PIX capture, capture-file
management, CPU profiler, pipeline-statistics or occlusion queries, static
upload timing, dynamic pass profiler, query-heap growth, graph-wide automatic
instrumentation, cross-queue clock calibration, stable-power-state control,
multi-queue timing, or performance threshold.

The diagnostics remain proportional to Shark's bounded San Andreas-class
local-sandbox goal. `T-005` was completed on July 19, 2026. The next increment
is `R-001`, seeded, bounded GPU rain driven by adjustable precipitation rate
and wind.
