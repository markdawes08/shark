# Camera, Reversed-Z Depth, Cube, and Skybox Contract

- **Camera/cube capability completed through:** `G-005`
- **Renderer integration verified through:** `PHY-004`
- **Last verified:** July 21, 2026

G-005 turns the first shader pipeline into Shark's first real 3D scene. One
engine-owned free-fly camera drives a resource-bound cube pipeline, a finite
reversed-Z depth target establishes the permanent depth convention, and a
procedural checker texture exercises the first shader-visible descriptor.
G-006 keeps those visible and input conventions unchanged while executing the
frame through the first render graph. S-002 adds a translation-free sky camera
matrix and uses the same geometry, root signature, depth texture, and controls
to render the static cubemap behind the cube.
T-001 retains those contracts while changing the initial camera pose to frame
the first terrain tile and drawing terrain before the cube.
S-002A keeps that translation-free cube and far-depth technique but replaces
the diagnostic cubemap's visible contribution with a continuous procedural
daylight gradient, sun disk, and halo. The same fixed direction-to-sun now
provides simple ambient-plus-Lambert terrain illumination. T-002 preserves
every camera, matrix, depth, and input convention while adding a cyan terrain
normal pin whose anchor and geometric direction come from the canonical
surface query. REN-001 preserves those results while moving public frame input
to `shark::renderer::RenderFrameData` and the D3D12 scene helpers behind the
private renderer backend. T-003 additionally passes finite camera world
position for terrain specular evaluation; it does not change the camera,
controller, matrix, cube, depth, or sky-motion contracts. S-003 preserves
those conventions while adding HDR IBL, a material sphere, and final tone
mapping; `F3` switches the environment mode without changing camera state.
PHY-001 preserved the camera and cube contracts while driving the existing
material sphere from an interpolated fixed-step body position. PHY-002 leaves
those conventions unchanged while stopping that sphere on canonical terrain.
PHY-003 retains them while publishing four interpolated positions and
rebinding the existing translation constants for four sphere draws.
PHY-004 adds shortest-path interpolated orientations and rotates those same
four sphere draws without changing camera, view-projection, or depth state.

This remains a deliberately narrow proof. It establishes conventions and
lifetime rules that later sky, terrain, rain, and water passes can reuse; it is
not a general scene, asset, material, or descriptor system.

## Coordinate and matrix conventions

The camera and shader share these fixed conventions:

- the world is right-handed;
- `+X` is right, `+Y` is up, and camera forward is `-Z`;
- positions and movement are measured in meters and angles in radians;
- matrices are row-major on both CPU and GPU;
- vectors are row vectors and HLSL uses `mul(vector, matrix)`; and
- a model vertex reaches clip space through `world * view * projection`; the
  proof cube uses the identity world transform and uploads `view * projection`.

T-004 also uses that ordinary `view_projection` on the CPU to extract the six
Direct3D clip half-spaces for terrain-chunk culling. It never uses
`sky_view_projection`, whose removed translation is valid only for the
far-depth environment.
T-005 uses the existing finite camera world position to compute the shortest
3D distance to each visible chunk's closed AABB. That render-only distance
selects LOD0 or coarse through the inclusive
`error <= 0.008 * distance` rule; it adds no camera matrix or controller state.

DirectXMath supplies the private implementation math. Engine-owned camera and
frame records remain independent of Win32, DXGI, D3D12, WRL, and COM types.
Matrix construction rejects non-finite values and invalid projection
parameters, while motion updates ignore invalid time/speed input instead of
publishing a matrix containing NaN or infinity.

The finite perspective projection uses Direct3D's normalized depth interval
`[0, 1]` with reversed-Z:

```text
camera near plane -> depth 1
camera far plane  -> depth 0
```

The near distance is positive, the far distance is finite and greater than the
near distance, and the aspect ratio is derived from the current nonzero physical
client extent. A usable resize therefore rebuilds the projection before the
next submitted frame. Unit tests cover the default basis, view transform,
near/far mapping, matrix order, resize aspect change, local movement, pitch
clamping, sky translation invariance and rotation response, and rejection of
invalid or non-finite input.

The sandbox defaults are a position of `(0, 4, 10)`, zero yaw, `-0.35` radians
of pitch, a 60-degree
vertical field of view, a `0.1 m` near plane, and a `100 m` far plane. The cube
occupies `[-1, 1]` on each world axis; the elevated, downward-pitched default
pose frames it with the terrain tile.

## Free-fly camera controls

The sandbox composition root owns the camera and the small controller that
interprets the existing platform event records:

| Input | Camera action |
|---|---|
| `W` / `S` | Move forward / backward along the full camera forward axis |
| `A` / `D` | Strafe left / right |
| `Q` or `Control` / `E` or `Space` | Move down / up along world `Y` |
| Hold `Shift` | Increase translation speed while held |
| Hold right mouse and drag | Change yaw and pitch |
| `F4` | Toggle terrain chunk bounds and query-marker diagnostics |

`F5` and `F6` are separate simulation controls owned by the sandbox:

| Input | Simulation action |
|---|---|
| `F5` | Toggle the fixed 60 Hz body simulation between paused and running |
| `F6` | Advance exactly one fixed tick while paused |

Mouse coordinates remain absolute client coordinates at the platform boundary.
The right-button press coordinates establish the drag baseline, so movement
before the press is ignored and the first drag delta cannot inherit an old
cursor location. Dragging right turns right, dragging up looks up, and pitch is
clamped below vertical to preserve a finite orthonormal basis. Key-repeat
records do not multiply a held action, and releasing a key or right mouse
button ends that action.

T-006 historically scaled the default translation speed to `32 m/s` with the
existing `4x` Shift multiplier; T-008 retains it. Mouse sensitivity remains
`0.0025` radians per pixel.
Movement is scaled by elapsed render time and clamps one update to at most
`0.1 s`, preventing a long minimize or debugger stall from producing a large
camera jump. `F4` diagnostics are off at startup. `WindowFocusChangedEvent`
drives the controller's focus hook;
focus loss, minimize, close request, and final close clear held input. This is
an interactive render-time camera proof; it remains independent of PHY-001's
fixed 60 Hz simulation clock. The controller owns only the documented camera
bindings, while the sandbox consumes `F5`/`F6` as simulation policy. The
platform layer continues to publish raw window and input events without
acquiring camera or gameplay policy. If the bounded platform queue reports any
dropped event, the composition root also clears held controller state so a
lost release or focus record cannot leave movement or right-drag latched.

## Cube geometry and procedural texture

The proof scene contains exactly one cube:

- 24 vertices, allowing independent UVs on each face;
- one `R32G32B32_FLOAT` position and one `R32G32_FLOAT` texture coordinate per
  vertex;
- 36 `uint16_t` indices forming 12 non-degenerate triangles; and
- one `DrawIndexedInstanced(36, 1, 0, 0, 0)` per submitted frame.

The vertex and index buffers are immutable committed default-heap resources.
The texture is an `8x8`, one-mip, `DXGI_FORMAT_R8G8B8A8_UNORM` checker generated
deterministically in memory. Adjacent texels alternate between
`RGBA(224,224,224,255)` and `RGBA(32,32,32,255)`. It has no source file, asset
identifier, color-space metadata, compression, mip generation, or streaming
behavior.

The private D3D12 renderer backend records the cube and packed
surface/bounds/query-marker terrain
vertex/index buffers, checker, and S-001 cubemap copies into one
`StaticSceneUpload` direct-queue submission during startup. The same submission
also copies three terrain material arrays and all 79 derived HDR environment
subresources. Thirteen barriers establish final input/shader-read states, then
it signals the normal monotonic direct fence and performs one bounded startup
wait before releasing temporary upload storage. No copy queue, background
transfer, or per-frame static upload exists.

## Resource binding and shaders

The cube root signature contains:

- one vertex/pixel-visible root constant-buffer view at `b0` for the current
  scene, sky, and daylight constants;
- one descriptor table containing one pixel-shader SRV at `t0`; and
- one point-filtered wrap static sampler at `s0`.

The sky has its own focused root signature with the shared frame CBV,
environment-mode constants, one HDR radiance-cube table, and a linear-clamp
sampler.

Unused hull, domain, and geometry shader root access remains denied. One
persistent ten-slot shader-visible CBV/SRV/UAV heap holds the checker at slot
0, retained DDS cubemap at slot 1, three terrain material arrays at slots 2-4,
three IBL maps at slots 5-7, HDR radiance at slot 8, and resize-owned HDR scene
color at slot 9. `TexturedCube` points its table at slot 0; `Skybox` points its
focused table at slot 8. Slot 1 remains only the S-001 startup proof. This is
not a general persistent allocator or bindless heap.

Each frame writes one 256-byte-aligned record into the acquired frame context's
existing upload arena. Its first 64 bytes are the engine-owned
`view_projection`, its next 64 bytes are `sky_view_projection`, six
float4-compatible daylight rows occupy bytes `128..223`, and the G-003 frame
probe begins at byte `224`. The same fence checkpoint that protects the command
allocator protects the complete record until the draw retires. The cube vertex
shader reads position and UV attributes, evaluates
`mul(position, view_projection)`, and passes UVs to the pixel shader. Its pixel
shader samples the checker and writes the opaque color target. The sky vertex
shader uses the translation-free matrix, forces reversed-Z clip depth to zero,
and passes cube position as a world direction. Its pixel shader normalizes that
direction and samples the generated HDR radiance cube or evaluates the
procedural fallback. Cube, sky, terrain, and sphere write linear color to the
`R16G16B16A16_FLOAT` scene target. The final `ToneMap` pass applies a fixed
ACES-fitted curve and explicit linear-to-sRGB transfer to the UNORM back
buffer.

PHY-004's current material-sphere `b2` contract contains seven 32-bit values:
a unit quaternion followed by world position. The sandbox interpolates
immutable previous/current rigid snapshots and passes each transform through
`RenderFrameData`. The material-sphere vertex shader rotates authored-center-
relative positions and normals before the existing `view_projection`; camera
matrices, geometry buffers, descriptors, and draw count remain unchanged.

The focused cube and sky root signatures plus immutable cube/skybox PSOs are
created synchronously from pinned build-time DXIL. They survive swap-chain
resize and are released only after explicit renderer shutdown drains and
retires every submission.

## Reversed-Z depth resource

The private D3D12 renderer backend owns one two-descriptor CPU-only DSV heap
and one committed
`DXGI_FORMAT_D32_FLOAT` texture for the current physical extent. Slot 0 is the
normal writable DSV and slot 1 is a `READ_ONLY_DEPTH` view. The resource:

- has `D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL`;
- starts and ends each frame in `D3D12_RESOURCE_STATE_DEPTH_WRITE`;
- clears to exactly `0.0F`;
- uses depth writes with `D3D12_DEPTH_WRITE_MASK_ALL`;
- compares with `D3D12_COMPARISON_FUNC_GREATER_EQUAL`; and
- has stencil disabled.

Every submitted frame binds the writable DSV and clears it once for `Terrain`.
Terrain and `TexturedCube` write reversed-Z depth. The graph then transitions
the resource to `DEPTH_READ`, and
`Skybox` binds the read-only DSV with `GREATER_EQUAL`, forced depth zero, and
depth writes disabled. The final graph transition restores `DEPTH_WRITE`.
All depth-using scene PSOs declare `DXGI_FORMAT_D32_FLOAT` and use viewport
depth `[0, 1]`.

Back-face culling is disabled for this first proof, making the depth target
solely responsible for hiding the cube's farther triangles.

On an effective nonzero resize, `Renderer::resize` makes the private backend
drain the direct queue, retire the reusable contexts, release the old back
buffers, depth texture, and HDR scene target, resize the swap chain, reacquire
the back-buffer RTVs, and create matching depth/DSV plus HDR scene RTV/SRV
resources. Minimization never creates a zero-size resource and never submits a
draw or clear. A restore carrying the existing extent is a no-op.

## Automated and manual acceptance

The fixed presentation smoke requires 1,000 successful presents on automatic
hardware, 600 on normal packaged WARP, and 120 on the focused packaged-WARP path
with GPU-based validation. Hardware and normal WARP change from `1280x720` to
`960x600`; focused GPU validation alone uses `640x360 -> 480x300`. Both
sequences intentionally change aspect from `16:9` to `1.6`, and each applies
`1.25` radians of scripted yaw at three quarters.
The interactive camera starts at T-008's scenario-owned
`(-128,3.34375,-20)` eye with pitch `-0.1` radians and a 1,500-meter far plane,
overlooking the dry basin. Presentation smoke deliberately retains the
separate deterministic `(0,28,112)` start with pitch `-0.25`. Its initial and
resized views expose 93 terrain chunks at a `0/93`
LOD0/coarse split. The turned overview exposes 72 at `0/72` from three quarters
through seven eighths. For the final eighth, presentation smoke alone moves to
`(16, -1, 0)` with the same yaw/pitch and exposes 61 at `1/60`, keeping both
terrain index ranges live without replacing the scenario-owned interactive start.
Hardware and normal WARP minimize/restore at halfway; focused validation
intentionally skips that already-covered interval.

The permanent accounting contract requires:

- with `V0` visible LOD0 chunks and `Vc` visible coarse chunks, `V0`
  1,536-index and `Vc` 864-index terrain draws, four 1,584-index material
  spheres, one 36-index cube, and one 36-index skybox; `F4` optionally adds
  `V0+Vc` 24-index magenta chunk-bounds draws and one six-index cyan query
  marker; plus one six-vertex water draw, one fullscreen tone-map draw, five
  texture-table bindings, one frame-constant upload, and one depth clear;
- 225 chunk tests per submitted frame, visible-plus-culled conservation, exact
  `93 -> 72 -> 61` visibility, and exact `0/93 -> 0/72 -> 1/60` LOD0/coarse
  splits across the deterministic smoke poses;
- one graph compilation/execution, five pass executions, 15 imports, five
  dependencies, six recorded transitions, and 34 elided transitions per frame;
- frame submissions equal successful plus occluded present attempts;
- all three DXGI-selected frame contexts are acquired and reused;
- every submission retires before shutdown;
- `static_upload_submissions == 1`, `geometry_buffer_creations == 4`,
  `checker_texture_creations == 1`, `cubemap_texture_creations == 1`, four
  environment textures, and ten persistent texture descriptors; the startup
  submission completes through its bounded fence wait before the first frame;
- depth-resource and read-only DSV-view creations each equal
  `resize_count + 1`;
- minimized intervals prove no query-marker or other draw, texture bind,
  frame-constant upload, graph work, or depth clear occurs while minimized;
- exact scene/sky matrix-change counts of `4/3`: both cover the initial matrix,
  aspect-changing resize, and scripted yaw, while only the scene matrix changes
  for the final translation-only near pose;
- upload accounting requires `upload_allocations == frame_submissions`,
  256 bytes written per allocation, and `upload_high_water_bytes == 256`;
- descriptor accounting requires one allocation per submitted frame and
  `descriptor_high_water_count == 1`;
- `full_queue_drains == resize_count + 1`; the static-upload fence wait is
  deliberately not counted as a full-queue drain; and
- final DirectX validation reports no corruption, errors, discarded diagnostic
  messages, or live renderer-owned D3D12 children.

The smoke does not read back or compare pixels. Manual acceptance requires a
clearly textured cube, a continuous HDR environment with coherent terrain and
sphere response, clean `F3` fallback switching, unchanged sky direction under
translation, correct near/far occlusion, and perspective that does not stretch
after resize. Press `F4` and verify the magenta bounds and cyan terrain pin
appear, with the pin anchored to the displayed surface and pointing along its
exact geometric normal; press it again and verify both disappear. Resize,
minimize/restore, and shutdown must remain clean.
The validation cube must remain outside the analytic basin support, above the
`-4`-meter future waterline, and unburied. All four material spheres must meet
those conditions at their initial paused spawns; after `F5`, bodies 1 and 2
collide airborne while primary body 0 settles on the visible canonical LOD0
face without hover or penetration. T-008
adds the dry indentation and metadata but creates no water. The scenario-owned
camera must begin on dry ground overlooking the basin without changing the sky
under translation.
When running `--present-smoke`, its final reported state must be 61 visible
chunks at `LOD0=1, coarse=60`; this final near pose is not an interactive camera
default.
No cube face, edge, or corner may appear as a painted wall. See
[the sky procedure](SKYBOX.md#manual-acceptance).

CPU coverage is discovered through the existing `unit.` CTest prefix from
`math_tests.cpp`, `camera_tests.cpp`, `camera_controller_tests.cpp`,
`d3d12_cube_scene_data_tests.cpp`, `d3d12_skybox_scene_data_tests.cpp`, and
`d3d12_daylight_scene_data_tests.cpp`, plus the S-003 environment-lighting and
material-sphere scene-data suites.
Graphics coverage is registered as
`integration.gpu.hardware_cube_present`,
`integration.gpu.warp_cube_present`, and
`integration.gpu.warp_cube_present_validation`.

## Explicit non-goals

S-003 retains procedural daylight beside the retained S-001 startup asset
proof. It adds no general
DDS/WIC/glTF importer, runtime mip generation, compression, texture streaming,
file-backed HDR conversion, arbitrary material graph/system, shadow map,
atmospheric scattering, cloud, automatic exposure, time of day, terrain
streaming, additional LOD levels, or content database.

The support-sample marker, CPU chunk culling, stateless LOD choice, F4
diagnostic gate, and PHY-001 through PHY-004 sphere transforms add no camera matrix,
GPU resource, PSO, graph pass, dependency, barrier, PIX event, or timestamp.
W-001 retains 15 imports, five passes, five dependencies, six barriers, 34
elisions, four geometry buffers, and 12 timestamps as the current exact
contract.

It also adds no general mesh/resource/descriptor manager, typed GPU handles,
placed-resource pool, copy queue, deferred uploader, shader reflection, runtime
shader compilation, hot reload, PSO cache, scene graph, ECS,
multiple cameras, controllable entity, general scene entities, angular contact
dynamics, animation, shadows, MSAA,
additional terrain LOD levels, LOD hysteresis/morphing, instancing, raw mouse
input, cursor lock, configurable action map, gamepad support, pixel readback,
or golden-image testing.

The graph remains frame-local and limited to imported whole resources, now
with ordered `Terrain`, `TexturedCube`, `Skybox`, `Water`, and `ToneMap`
passes. See
[the render-graph contract](RENDER_GRAPH.md) for its exact ordering/barriers,
[the DDS cubemap contract](DDS_CUBEMAP.md) for the source texture, and
[the skybox contract](SKYBOX.md) for the HDR environment and procedural
fallback rules.
See [the terrain contract](TERRAIN.md) for its separate geometry, chunk
culling, bounded LOD, and canonical query/material/diagnostic rendering
contract. T-006's camera start, 1,500-meter far plane, and 32 m/s navigation
remain the historical capacity-scale foundation. `T-007` completed the
fixed-seed natural rolling surface on July 19, 2026 without changing cube, sky
motion, depth, or canonical query conventions. Its historical smoke added only a
final translation-only near pose to exercise both terrain index ranges;
Debug/Release hardware, normal WARP, and focused GBV validation passed. The
evidence remains historical.

`T-008` publishes the interactive spawn and basin metadata without changing
cube geometry, sky motion, depth, input, or the deterministic smoke schedule.
`W-001` adds only presentation-time water input and preserves those camera and
cube contracts. `PHY-004` retains canonical terrain support and the CPU-only
four-sphere pair pass while adding independently interpolated orientations;
see
[the simulation contract](SIMULATION.md). This component page
no longer duplicates the rolling project
queue; [ENGINE_PLAN.md](ENGINE_PLAN.md) is the roadmap source of truth. Rain
remains deferred under the San Andreas-class ceiling.
