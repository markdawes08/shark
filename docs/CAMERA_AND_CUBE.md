# Camera, Reversed-Z Depth, Cube, and Skybox Contract

- **Completed through:** `S-002`
- **Last verified:** July 16, 2026

G-005 turns the first shader pipeline into Shark's first real 3D scene. One
engine-owned free-fly camera drives a resource-bound cube pipeline, a finite
reversed-Z depth target establishes the permanent depth convention, and a
procedural checker texture exercises the first shader-visible descriptor.
G-006 keeps those visible and input conventions unchanged while executing the
frame through the first render graph. S-002 adds a translation-free sky camera
matrix and uses the same geometry, root signature, depth texture, and controls
to render the static cubemap behind the cube.

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

The sandbox defaults are a position of `(0, 0, 4)`, zero yaw/pitch, a 60-degree
vertical field of view, a `0.1 m` near plane, and a `100 m` far plane. The cube
occupies `[-1, 1]` on each world axis, so the default camera looks directly at
its center.

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

Mouse coordinates remain absolute client coordinates at the platform boundary.
The right-button press coordinates establish the drag baseline, so movement
before the press is ignored and the first drag delta cannot inherit an old
cursor location. Dragging right turns right, dragging up looks up, and pitch is
clamped below vertical to preserve a finite orthonormal basis. Key-repeat
records do not multiply a held action, and releasing a key or right mouse
button ends that action.

The defaults are `4 m/s`, a `4x` Shift multiplier, and `0.0025` radians per
mouse pixel. Movement is scaled by elapsed render time and clamps one update to
at most `0.1 s`, preventing a long minimize or debugger stall from producing a
large camera jump. `WindowFocusChangedEvent` drives the controller's focus hook;
focus loss, minimize, close request, and final close clear held input. This is
an interactive camera proof, not the fixed 60 Hz simulation clock planned for
physics. The controller owns only the documented bindings; the platform layer
continues to publish raw window and input events without acquiring camera or
gameplay policy. If the bounded platform queue reports any dropped event, the
composition root also clears held controller state so a lost release or focus
record cannot leave movement or right-drag latched.

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

Presentation records the vertex-buffer, index-buffer, checker, and S-001
cubemap copies into one direct-queue static-upload submission during startup.
It transitions the buffers to their input-assembler states and both textures to
`PIXEL_SHADER_RESOURCE`, signals the normal monotonic direct fence, and
performs one bounded startup wait before releasing the temporary upload
storage. No copy queue, background transfer, or per-frame static upload exists.

## Resource binding and shaders

The G-005 root signature contains:

- one root constant-buffer view at `b0` for the current scene and sky matrices;
- one descriptor table containing one pixel-shader SRV at `t0`; and
- one point-filtered wrap static sampler at `s0`.

Unused hull, domain, and geometry shader root access remains denied. One
persistent shader-visible CBV/SRV/UAV heap holds the checker SRV at slot 0 and
the cubemap SRV at slot 1. The one-entry root table points at slot 0 for
`TexturedCube` and slot 1 for `Skybox`. This is not the stable-index persistent allocator or bindless
heap planned for later renderer infrastructure.

Each frame writes one 256-byte-aligned record into the acquired frame context's
existing upload arena. Its first 64 bytes are the engine-owned
`view_projection`, its next 64 bytes are `sky_view_projection`, and the G-003
frame probe begins at byte 128. The same fence checkpoint that protects the command
allocator protects those constants until the draw retires. The vertex shader
reads position and UV attributes, evaluates `mul(position, view_projection)`,
and passes UVs to the pixel shader. The pixel shader samples the checker and
writes the opaque color target. The sky vertex shader uses the translation-free
matrix, forces reversed-Z clip depth to zero, and passes cube position as the
texture-cube direction; its pixel shader samples cubemap slot 1.
The current visual treatment retains 4% of that decoded sample and mixes 96%
toward a fixed sky-blue shader-space color; it does not alter the camera,
descriptor, or TextureCube sampling contracts.

The shared root signature and immutable cube/skybox PSOs are created
synchronously from pinned build-time DXIL. They survive swap-chain resize and are released only
after explicit presentation shutdown drains and retires every submission.

## Reversed-Z depth resource

Presentation owns one two-descriptor CPU-only DSV heap and one committed
`DXGI_FORMAT_D32_FLOAT` texture for the current physical extent. Slot 0 is the
normal writable DSV and slot 1 is a `READ_ONLY_DEPTH` view. The resource:

- has `D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL`;
- starts and ends each frame in `D3D12_RESOURCE_STATE_DEPTH_WRITE`;
- clears to exactly `0.0F`;
- uses depth writes with `D3D12_DEPTH_WRITE_MASK_ALL`;
- compares with `D3D12_COMPARISON_FUNC_GREATER_EQUAL`; and
- has stencil disabled.

Every submitted frame binds the writable DSV and clears it once for
`TexturedCube`. The graph then transitions the resource to `DEPTH_READ`, and
`Skybox` binds the read-only DSV with `GREATER_EQUAL`, forced depth zero, and
depth writes disabled. The final graph transition restores `DEPTH_WRITE`.
Both PSOs declare `DXGI_FORMAT_D32_FLOAT` and use viewport depth `[0, 1]`.

Back-face culling is disabled for this first proof, making the depth target
solely responsible for hiding the cube's farther triangles.

On an effective nonzero resize, presentation drains the direct queue, retires
the reusable contexts, releases the old back buffers and depth texture, resizes
the swap chain, reacquires the RTVs, and creates a new matching depth texture
a writable and read-only DSV. Minimization never creates a zero-size depth resource and never submits
a draw or clear. A restore carrying the existing extent is a no-op.

## Automated and manual acceptance

The fixed presentation smoke requires exactly 1,000 successful presents on
automatic hardware and normal packaged WARP. The focused packaged-WARP path
with GPU-based validation requires 120 successful presents, a 180-second
internal deadline, and a 240-second CTest timeout. Each run changes from
`1280x720` to `960x600` at its quarter checkpoint, intentionally changes the
aspect ratio, and applies scripted yaw at three quarters. Only the normal
1,000-frame paths minimize/restore at halfway; focused validation intentionally
skips that already-covered interval.

The permanent accounting contract requires:

- one cube draw, one skybox draw, 36 indices per draw, two texture bindings,
  one camera upload, and one depth clear per submitted frame;
- one graph compilation/execution, two pass executions, four imports, one
  dependency, four recorded transitions, and six elided transitions per frame;
- frame submissions equal successful plus occluded present attempts;
- all three DXGI-selected frame contexts are acquired and reused;
- every submission retires before shutdown;
- `static_upload_submissions == 1`, `geometry_buffer_creations == 2`,
  `checker_texture_creations == 1`, `cubemap_texture_creations == 1`, and
  `texture_srv_creations == 2`; the startup submission completes through its
  bounded fence wait before the first frame;
- depth-resource and read-only DSV-view creations each equal
  `resize_count + 1`;
- the normal paths prove no draw, texture bind, camera upload, graph work, or
  depth clear occurs while minimized;
- scene and sky matrix-change counts are each at least three, covering the
  initial matrix, aspect-changing resize, and scripted yaw at the
  three-quarter checkpoint (frame 750 normally or frame 90 under focused
  GPU validation);
- upload accounting requires `upload_allocations == frame_submissions`,
  256 bytes written per allocation, and `upload_high_water_bytes == 256`;
- descriptor accounting requires one allocation per submitted frame and
  `descriptor_high_water_count == 1`;
- `full_queue_drains == resize_count + 1`; the static-upload fence wait is
  deliberately not counted as a full-queue drain; and
- final DirectX validation reports no corruption, errors, discarded diagnostic
  messages, or live presentation children.

The smoke does not read back or compare pixels. Manual acceptance requires a
clearly textured cube, the temporary sky-blue treatment behind it, unchanged
sample direction under translation, correct near/far occlusion, perspective
that does not stretch after resize, and clean minimize/restore and shutdown.
See [the source face-orientation procedure](SKYBOX.md#manual-visual-acceptance).

CPU coverage is discovered through the existing `unit.` CTest prefix from
`math_tests.cpp`, `camera_tests.cpp`, `camera_controller_tests.cpp`,
`d3d12_cube_scene_data_tests.cpp`, and `d3d12_skybox_scene_data_tests.cpp`.
Graphics coverage is registered as
`integration.gpu.hardware_cube_present`,
`integration.gpu.warp_cube_present`, and
`integration.gpu.warp_cube_present_validation`.

## Explicit non-goals

S-002 adds only the static diagnostic cubemap sky plus its temporary blue
presentation treatment. It adds no general
DDS/WIC/glTF importer, runtime mip generation, compression, texture streaming,
HDR conversion, material/PBR system, lighting, terrain, or content database.

It also adds no general mesh/resource/descriptor manager, typed GPU handles,
placed-resource pool, copy queue, deferred uploader, shader reflection, runtime
shader compilation, hot reload, PSO cache, scene graph, ECS,
multiple cameras, controllable entity, physics, animation, shadows, MSAA,
frustum culling, instancing, raw mouse input, cursor lock, configurable action
map, gamepad support, fixed simulation clock, pixel readback, or golden-image
testing.

The graph remains frame-local and limited to imported whole attachments, now
with ordered `TexturedCube` and `Skybox` passes. See
[the render-graph contract](RENDER_GRAPH.md) for its exact ordering/barriers,
[the DDS cubemap contract](DDS_CUBEMAP.md) for the source texture, and
[the skybox contract](SKYBOX.md) for the visible sampling and orientation rules.
