# Direct3D 12 GPU Diagnostics Contract

- **Completed through:** `S-001`
- **Last verified:** July 16, 2026

G-007 adds the first bounded GPU diagnostics to the existing G-006
presentation path. Every submitted frame and its `TexturedCube` render-graph
pass now have stable PIX names and direct-queue timestamp intervals. The
one-time geometry, checker-texture, and cubemap upload also has a PIX name.

S-001 does not change the visible scene, graph declarations, or exact
two-transition frame contract. It extends only the already-named startup scope
with the first DDS cubemap resource while retaining the deterministic
1,000-frame diagnostics contract.

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

`TexturedCube` is nested inside `Frame`. The graph-owned
`PRESENT -> RENDER_TARGET` transition occurs before the pass callback, and the
`RENDER_TARGET -> PRESENT` transition occurs after it, so neither transition is
inside the `TexturedCube` marker. Both transitions remain inside `Frame`.

Markers use a small RAII command-list scope so an early result-return closes an
event that has already begun. The static event is counted after its command
list executes; frame/pass events and query/resolve accounting are committed
only after the frame receives its normal completion-fence checkpoint.

## Fixed timestamp storage

The presentation object owns one direct-queue timestamp query heap with 12
slots and one persistently mapped 96-byte readback buffer. The storage is
partitioned into three non-overlapping slices, one for each swap-chain frame
context:

| Frame context | Query indices | Query base | Readback byte range | Readback offset |
|---:|---|---:|---|---:|
| 0 | `0..3` | 0 | `0..31` | 0 |
| 1 | `4..7` | 4 | `32..63` | 32 |
| 2 | `8..11` | 8 | `64..95` | 64 |

Each result is one 64-bit timestamp, so a four-query context slice occupies 32
bytes. Each `FrameResourceState` also owns a fixed-capacity four-slot timestamp
arena. A frame must allocate its entire four-query local slice at offset zero;
a fifth allocation fails instead of growing or overwriting storage.

`timestamp_query_capacity` reports the global 12-query heap capacity.
`timestamp_query_high_water` reports the largest per-context arena use and is
therefore exactly four in a successful smoke.

## Four-query frame layout

Each submitted frame writes exactly four timestamps in this order:

| Local index | Name | Position |
|---:|---|---|
| 0 | `frame_begin` | Immediately after the `Frame` PIX marker begins and before the per-frame diagnostic probe copy |
| 1 | `pass_begin` | Immediately after the `TexturedCube` PIX marker begins and before render-target/depth setup and clears |
| 2 | `pass_end` | Immediately after `DrawIndexedInstanced` and before the `TexturedCube` marker ends |
| 3 | `frame_end` | After graph execution, including its final back-buffer transition, and before timestamp resolution |

The four results are resolved as one batch to the current context's 32-byte
readback slice on the same direct command list:

```text
reset frame command list
  -> begin PIX "Frame"
  -> write frame_begin
  -> copy diagnostic frame probe
  -> graph transition: PRESENT -> RENDER_TARGET
  -> begin PIX "TexturedCube"
       -> write pass_begin
       -> clear/bind/draw
       -> write pass_end
     end PIX "TexturedCube"
  -> graph transition: RENDER_TARGET -> PRESENT
  -> write frame_end
  -> resolve four timestamps to this context's readback slice
  -> end PIX "Frame"
  -> close, execute, signal the context completion fence, present
```

CPU validation requires the nested ordering:

```text
frame_begin <= pass_begin <= pass_end <= frame_end
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
3. consumes its four mapped results only after the completion value covers the
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
- both render-graph back-buffer transitions;
- the complete `TexturedCube` pass; and
- command processing between those timestamp writes.

It excludes timestamp resolution, command-list close, CPU recording time,
queue submission, the fence signal, swap-chain `Present`, display latency, and
the one-time static upload.

The pass duration is `pass_end - pass_begin`. It includes the render-target and
depth clears, viewport/scissor and pipeline/resource setup, and indexed cube
draw. It excludes graph transition barriers and callback resource-access
validation.

These are stable measurement boundaries, not fixed performance expectations.
Exact values depend on the selected adapter, driver, power state, validation
mode, window state, and concurrent system work.

## Public statistics and smoke invariants

`PresentationStats` exposes marker counts, bounded query accounting, the direct
queue frequency, timing sample count, and frame/pass total, minimum, maximum,
and last-consumed durations in ticks. "Last" follows context retirement order;
it is not a chronological newest-submission identifier.

After the smoke's final shutdown drain, the following equalities must hold:

```text
pix_static_upload_events == static_upload_submissions == 1
pix_frame_events == frame_submissions
pix_pass_events == render_graph_pass_executions == frame_submissions

gpu_timestamp_frequency_hz > 0
timestamp_query_capacity == 12
timestamp_query_high_water == 4
timestamp_queries_written == frame_submissions * 4
timestamp_resolve_batches == frame_submissions
gpu_timing_samples == frame_submissions
```

The aggregate checks also require:

```text
gpu_frame_total_ticks >= gpu_pass_total_ticks
gpu_frame_last_ticks >= gpu_pass_last_ticks
gpu_frame_max_ticks >= gpu_pass_max_ticks
gpu_frame_min_ticks <= gpu_frame_max_ticks
gpu_pass_min_ticks <= gpu_pass_max_ticks
```

While the window is minimized, no frame submission, graph execution, frame or
pass PIX marker, timestamp allocation, resolve batch, consumed sample, draw,
camera update, or depth clear may advance. The existing smoke additionally
retains its exact two graph transition barriers per submitted frame.

The successful smoke summary contains these reproducible fields:

```text
pix-events(static/frame/pass)=...
timestamp-queries(high/capacity)=4/12
gpu-samples=...
gpu-frame-ms(avg/max)=.../...
gpu-TexturedCube-ms(avg/max)=.../...
```

"Reproducible" means the same named boundaries, bounded counts, and aggregation
rules are exercised every run. It does not mean GPU durations must be
bit-for-bit equal across runs or adapters.

## Verification and manual PIX acceptance

The unit tests cover:

- exact three-context query/readback partitioning;
- valid nested and zero-length timestamp intervals;
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

For manual PIX acceptance:

1. Launch the hardware `SharkSandbox.exe` from the current stable,
   non-preview PIX release.
2. Capture a rendered frame after the window appears.
3. Confirm the GPU event hierarchy contains one `Frame` scope with one nested
   `TexturedCube` scope and that the draw is inside `TexturedCube`.
4. Confirm the two graph back-buffer transitions bracket `TexturedCube` while
   remaining inside `Frame`.
5. Use a startup-inclusive capture or timing capture to verify the one-time
   `StaticCubeUpload` scope. A frame capture started after initialization is not
   expected to contain that startup-only event.
6. Confirm the application smoke log reports `4/12` query high-water/capacity,
   one timing sample per submitted frame after shutdown, and finite frame/pass
   average and maximum durations.
7. Confirm the D3D12 debug layer reports no errors or corruption messages.

## Explicit non-goals

G-007 adds no live diagnostics HUD, Dear ImGui integration, automated PIX
capture control, capture-file management, CPU profiler, pipeline-statistics
queries, occlusion queries, or static-upload timestamp measurement.

It adds no general-purpose multi-pass profiler, dynamic query-heap growth,
graph-wide automatic pass instrumentation, cross-queue clock calibration,
compute/copy queue timing, async compute, parallel command recording, or
cross-queue fence policy. It also adds no new queue drain or normal-frame
synchronous GPU readback.

The render graph remains frame-local and serial, its resource declarations are
unchanged, and the visible camera/checker-cube scene remains unchanged. See
[the render-graph contract](RENDER_GRAPH.md) for pass/barrier ownership and
[the presentation contract](GRAPHICS_PRESENTATION.md) for frame-context,
resize, and shutdown ownership. See
[the DDS cubemap contract](DDS_CUBEMAP.md) for the S-001 work enclosed by the
startup marker without changing frame timing.
