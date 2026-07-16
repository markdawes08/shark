# Direct3D 12 GPU Diagnostics Contract

- **Completed through:** `S-002`
- **Last verified:** July 16, 2026

G-007 adds the first bounded GPU diagnostics to the existing G-006
presentation path. S-002 retains the frame interval and adds separate stable
PIX/timestamp intervals for the `TexturedCube` and `Skybox` graph passes. The
one-time geometry, checker-texture, and cubemap upload also has a PIX name.

The diagnostics remain fixed-capacity and fence-delayed while the visible
contract expands to two passes, four resource imports, four attachment
transitions, and exact checker/cubemap pixel-shader reads.

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
| `StaticCubeUpload` | 3 | Once for the one static upload submission | Begins after the upload command list is reset; contains the vertex/index buffer copies, checker-texture copy, six cubemap face/mip copies, and four initialization transitions; ends before command-list close, execution, fence signal, and the required initialization wait |
| `Frame` | 1 | Once per submitted frame | Begins after the reusable frame command list is reset; contains the frame timestamp interval, diagnostic probe copy, complete render-graph execution, and timestamp resolve; ends before command-list close, execution, fence signal, and `Present` |
| `TexturedCube` | 2 | Once per graph-pass execution | Begins inside the graph callback after declared resource access is validated; contains the pass timestamp interval, attachment binding and clears, pipeline/resource binding, and indexed draw; ends before the callback returns |
| `Skybox` | 3 | Once per graph-pass execution | Begins after its declared color/depth/cubemap access is validated; contains its timestamp interval, read-only DSV and cubemap binding, and reused 36-index draw; ends before the callback returns |

Both pass markers are nested sequentially inside `Frame`. Graph barriers occur
between/outside callbacks: color enters render-target state before
`TexturedCube`, depth enters read state before `Skybox`, and final color/depth
states are restored after `Skybox`. All four barriers remain inside `Frame` but
outside the two pass timing intervals.

Markers use a small RAII command-list scope so an early result-return closes an
event that has already begun. The static event is counted after its command
list executes; frame/pass events and query/resolve accounting are committed
only after the frame receives its normal completion-fence checkpoint.

## Fixed timestamp storage

The presentation object owns one direct-queue timestamp query heap with 18
slots and one persistently mapped 144-byte readback buffer. The storage is
partitioned into three non-overlapping slices, one for each swap-chain frame
context:

| Frame context | Query indices | Query base | Readback byte range | Readback offset |
|---:|---|---:|---|---:|
| 0 | `0..5` | 0 | `0..47` | 0 |
| 1 | `6..11` | 6 | `48..95` | 48 |
| 2 | `12..17` | 12 | `96..143` | 96 |

Each result is one 64-bit timestamp, so a six-query context slice occupies 48
bytes. Each `FrameResourceState` owns a fixed-capacity six-slot timestamp arena.
A frame allocates its entire local slice at offset zero; a seventh slot fails
instead of growing or overwriting storage.

`timestamp_query_capacity` reports the global 18-query heap capacity.
`timestamp_query_high_water` reports the largest per-context arena use and is
therefore exactly six in a successful smoke.

## Six-query frame layout

Each submitted frame writes exactly six timestamps in this order:

| Local index | Name | Position |
|---:|---|---|
| 0 | `frame_begin` | Immediately after the `Frame` PIX marker begins and before the per-frame diagnostic probe copy |
| 1 | `textured_cube_begin` | Immediately after the `TexturedCube` marker begins and before attachment setup/clears |
| 2 | `textured_cube_end` | Immediately after the cube draw and before its marker ends |
| 3 | `skybox_begin` | Immediately after the `Skybox` marker begins and before read-only depth/resource setup |
| 4 | `skybox_end` | Immediately after the skybox draw and before its marker ends |
| 5 | `frame_end` | After graph execution and both final attachment transitions, before resolution |

The six results are resolved as one batch to the current context's 48-byte
readback slice on the same direct command list:

```text
reset frame command list
  -> begin PIX "Frame"
  -> write frame_begin
  -> copy diagnostic frame probe
  -> graph transition: PRESENT -> RENDER_TARGET
  -> begin PIX "TexturedCube"
        -> write textured_cube_begin
        -> clear/bind/draw
        -> write textured_cube_end
      end PIX "TexturedCube"
  -> graph transition: DEPTH_WRITE -> DEPTH_READ
  -> begin PIX "Skybox"
       -> write skybox_begin
       -> bind read-only depth/cubemap and draw
       -> write skybox_end
     end PIX "Skybox"
  -> graph transitions: RENDER_TARGET -> PRESENT, DEPTH_READ -> DEPTH_WRITE
  -> write frame_end
  -> resolve six timestamps to this context's readback slice
  -> end PIX "Frame"
  -> close, execute, signal the context completion fence, present
```

CPU validation requires the nested ordering:

```text
frame_begin <= textured_cube_begin <= textured_cube_end
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
3. consumes its six mapped results only after the completion value covers the
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
in flight. The presentation smoke calls `shutdown` before checking final
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
- the complete `TexturedCube` and `Skybox` passes; and
- command processing between those timestamp writes.

It excludes timestamp resolution, command-list close, CPU recording time,
queue submission, the fence signal, swap-chain `Present`, display latency, and
the one-time static upload.

The cube and sky durations are each their named end minus begin. Cube timing
includes its clears, setup, and indexed draw; sky timing includes read-only DSV,
cubemap setup, and indexed draw. Both exclude graph barriers and callback
resource-access validation.

These are stable measurement boundaries, not fixed performance expectations.
Exact values depend on the selected adapter, driver, power state, validation
mode, window state, and concurrent system work.

## Public statistics and smoke invariants

`PresentationStats` exposes marker counts, bounded query accounting, the direct
queue frequency, timing sample count, and frame/pass total, minimum, maximum,
and last-consumed frame/cube/sky durations in ticks. "Last" follows context retirement order;
it is not a chronological newest-submission identifier.

After the smoke's final shutdown drain, the following equalities must hold:

```text
pix_static_upload_events == static_upload_submissions == 1
pix_frame_events == frame_submissions
pix_pass_events == render_graph_pass_executions == frame_submissions * 2
pix_textured_cube_events == pix_skybox_events == frame_submissions

gpu_timestamp_frequency_hz > 0
timestamp_query_capacity == 18
timestamp_query_high_water == 6
timestamp_queries_written == frame_submissions * 6
timestamp_resolve_batches == frame_submissions
gpu_timing_samples == frame_submissions
```

The aggregate checks also require:

```text
gpu_frame_total_ticks >= gpu_textured_cube_total_ticks + gpu_skybox_total_ticks
gpu_frame_last_ticks >= gpu_textured_cube_last_ticks + gpu_skybox_last_ticks
gpu_frame_max_ticks >= gpu_textured_cube_max_ticks
gpu_frame_max_ticks >= gpu_skybox_max_ticks
gpu_frame_min_ticks <= gpu_frame_max_ticks
gpu_textured_cube_min_ticks <= gpu_textured_cube_max_ticks
gpu_skybox_min_ticks <= gpu_skybox_max_ticks
```

While the window is minimized, no frame submission, graph execution, frame or
pass PIX marker, timestamp allocation, resolve batch, consumed sample, draw,
camera update, or depth clear may advance. The existing smoke additionally
retains exactly four graph barriers and six elided transitions per frame.

The successful smoke summary contains these reproducible fields:

```text
pix-events(static/frame/pass)=...
timestamp-queries(high/capacity)=6/18
gpu-samples=...
gpu-frame-ms(avg/max)=.../...
gpu-TexturedCube-ms(avg/max)=.../...
gpu-Skybox-ms(avg/max)=.../...
```

"Reproducible" means the same named boundaries, bounded counts, and aggregation
rules are exercised every run. It does not mean GPU durations must be
bit-for-bit equal across runs or adapters.

## Verification and manual PIX acceptance

The unit tests cover:

- exact three-context query/readback partitioning;
- valid sequential cube/sky intervals nested within the frame interval,
  including zero-length intervals;
- rejection of incomplete and reversed intervals;
- overflow rejection without partial accumulator mutation; and
- fixed-capacity frame timestamp allocation, fence retirement, and retained
  high-water accounting.

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
   nested `TexturedCube` and `Skybox` scopes, with one indexed draw inside each
   pass scope.
4. Confirm the graph owns all four attachment transitions inside `Frame`: the
   back buffer enters render-target state before `TexturedCube`, depth enters
   read state before `Skybox`, and both attachments return to their imported
   final states after `Skybox`. The checker texture and cubemap remain in exact
   `pixel_shader_read` state and require no transition.
5. Use a startup-inclusive capture or timing capture to verify the one-time
   `StaticCubeUpload` scope. A frame capture started after initialization is not
   expected to contain that startup-only event.
6. Confirm the application smoke log reports `6/18` query high-water/capacity,
   one timing sample per submitted frame after shutdown, and finite frame,
   `TexturedCube`, and `Skybox` average and maximum durations.
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

The render graph remains frame-local and serial. S-002 adds only the explicit
skybox pass and its persistent cubemap/depth declarations; it does not add
general scheduling, transient-resource allocation, or queue selection. See
[the render-graph contract](RENDER_GRAPH.md) for pass/barrier ownership and
[the presentation contract](GRAPHICS_PRESENTATION.md) for frame-context,
resize, and shutdown ownership. See [the skybox contract](SKYBOX.md) for the
visual and orientation acceptance procedure and
[the DDS cubemap contract](DDS_CUBEMAP.md) for the uploaded texture contract.
