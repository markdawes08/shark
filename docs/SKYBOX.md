# Static Cubemap Skybox Contract

- **Completed through:** `T-001`
- **Last verified:** July 17, 2026

S-002 turns the validated S-001 DDS cubemap into Shark's second visible
rendering feature. Every submitted frame draws the existing textured cube and
then a named `Skybox` render-graph pass. Camera rotation changes the sampled
direction, camera translation does not move the sky, and the existing
reversed-Z depth buffer keeps the cube in front.
T-001 now draws terrain before the cube, so both opaque features remain in
front of the same far-depth sky.

The source remains the project-owned `8x8` diagnostic orientation cubemap, not
a photographed or procedural atmosphere. Its six faces use red-channel tags
`32, 64, 96, 128, 160, 192` for `+X, -X, +Y, -Y, +Z, -Z`; green increases
across each stored row and blue down each stored column. The T-001 default
camera still samples the `-Z` face with tag `192` at its center.

For a calmer background while terrain work begins, the pixel shader temporarily
mixes the decoded fixture RGB 96% toward a fixed sky-blue shader-space color.
The resulting frame is predominantly blue with only a subtle diagnostic trace;
the unchanged source texture and CPU tests remain the orientation authority.

## Rotation-only camera matrix

Shark keeps its right-handed, row-vector convention: `+Y` is up, camera forward
is `-Z`, matrices are row-major, and HLSL uses `mul(vector, matrix)`.
`build_camera_matrices` publishes both:

```text
view_projection      = view * projection
sky_view_projection  = view_without_translation * projection
```

For a row-vector view matrix, translation occupies elements `[3][0..2]`.
Shark copies the normal view, zeros exactly those three elements, and retains
the rotation and projection. Moving the camera therefore changes the cube
matrix but leaves the sky matrix unchanged; yaw, pitch, or aspect ratio changes
the sky projection. CPU tests prove translation invariance, rotation response,
finite construction, and aspect propagation.

One 256-byte frame allocation contains the 64-byte scene matrix, the 64-byte
sky matrix, and the retained frame probe beginning at byte 128. The same root
CBV at `b0` exposes both matrices, and the existing frame-context fence protects
the complete allocation until its submission retires.

## Shader, geometry, and descriptor binding

`shaders/sky/skybox.hlsl` is compiled at build time with the same pinned retail
DXC, HLSL 2021, Shader Model 6.0, row-major, strict, warning-as-error, debug-data,
and configuration-specific optimization contract as the cube shaders.

The skybox allocates no second mesh. Its PSO reads only `POSITION` from the
existing 24-vertex cube and reuses the same 36 indices. The vertex shader:

1. evaluates `mul(float4(position, 1), sky_view_projection)`;
2. preserves clip `x`, `y`, and `w` while forcing clip `z` to `0`; and
3. passes the original cube position as the texture-cube sample direction.

For Shark's reversed-Z convention, clip depth zero is the farthest depth. The
pixel shader samples `TextureCube<float4>` at `t0` through the existing
point-filtered wrap sampler at `s0`. The texture-cube SRV retains its
`DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` format, so D3D12 performs the required sRGB
decode when sampling. The temporary presentation treatment is evaluated in
that decoded shader domain:

```text
output.rgb = 0.04 * decoded_cubemap.rgb
           + 0.96 * float3(0.36, 0.62, 0.90)
output.a   = 1
```

This is not a display-sRGB color guarantee: the current UNORM presentation
path still has no final transfer function or tone map. Keeping the 4% source
contribution prevents the real TextureCube sampling path from being replaced
by a constant-color shader.

Both immutable PSOs share the existing root signature and persistent two-slot
shader-visible heap:

```text
slot 0  procedural checker Texture2D SRV  -> TexturedCube
slot 1  startup DDS TextureCube SRV       -> Skybox
```

The one-entry SRV root table is pointed at slot 0 for `TexturedCube` and slot 1
for `Skybox`. This remains a focused proof heap, not a general descriptor
allocator or material layout.

## Reversed-Z background policy

The `D32_FLOAT` depth texture clears to `0`. `Terrain` owns that clear, then
terrain and `TexturedCube` use `GREATER_EQUAL` with depth writes enabled,
producing depths greater than zero where either opaque feature is visible.
`Skybox` then uses:

```text
clip depth       0
comparison       GREATER_EQUAL
depth writes     disabled
DSV              READ_ONLY_DEPTH
resource state   DEPTH_READ
```

At untouched pixels, `0 >= 0` passes. At opaque terrain or cube pixels, `0` is
less than the nearer stored depth and the sky fails. The sky therefore fills
only the far background and cannot overwrite opaque color or depth.

The CPU-only DSV heap holds two views of the same extent-matched texture: the
normal writable DSV at slot 0 and a `D3D12_DSV_FLAG_READ_ONLY_DEPTH` view at
slot 1. Both views are recreated with the depth resource after an effective
resize. Minimized windows create no zero-sized resource and submit no pass.

## Exact frame graph

The frame graph imports the current back buffer, depth texture, persistent
checker/cubemap, cube vertex/index buffers, and terrain vertex/index buffers in
their exact states. It compiles these passes in order:

1. `Terrain` writes the color target as `render_target`, writes depth as
   `depth_write`, reads its buffers, clears both attachments, and draws the
   surface plus bounds.
2. `TexturedCube` preserves the attachments, writes color/depth, reads its
   buffers and checker, and draws 36 indices.
3. `Skybox` writes the same color target as `render_target`, reads depth as
   `depth_read`, reads the cubemap as `pixel_shader_read`, binds descriptor slot
   1, and draws the reused 36 indices without clearing.

The hazards produce dependencies `Terrain -> TexturedCube -> Skybox`. One
frame compiles:

```text
imports                 8
passes                  3
dependencies            2
emitted transitions     4
elided transitions      18
```

The emitted barriers are:

```text
BackBuffer  PRESENT       -> RENDER_TARGET  before Terrain
DepthBuffer DEPTH_WRITE   -> DEPTH_READ     before Skybox
BackBuffer  RENDER_TARGET -> PRESENT        after Skybox
DepthBuffer DEPTH_READ    -> DEPTH_WRITE    after Skybox
```

Equal-state attachment, buffer, and texture accesses and final states are
elided. The graph therefore
owns the persistent texture-read declarations as well as all per-frame
attachment transitions, without introducing a texture barrier.

## PIX, timestamps, and smoke accounting

The stable PIX hierarchy is:

```text
StaticSceneUpload                   once during startup
Frame                               once per submitted frame
  Terrain                           once per submitted frame
  TexturedCube                      once per submitted frame
  Skybox                            once per submitted frame
```

Each frame context owns eight query slots and 64 readback bytes:

```text
0 frame_begin
1 terrain_begin
2 terrain_end
3 textured_cube_begin
4 textured_cube_end
5 skybox_begin
6 skybox_end
7 frame_end
```

Three contexts therefore use one fixed 24-query heap and a 192-byte mapped
readback buffer, with query bases `0, 8, 16` and byte offsets `0, 64, 128`.
Samples are consumed only after the existing context-completion fence; T-001
adds no timing-only wait, normal-frame drain, or immediate GPU readback.

After the fixed smoke's shutdown drain, every submitted frame must account for:

```text
resource imports             8
graph pass executions        3
graph dependencies           2
recorded transitions         4
elided transitions           18
PIX frame/pass events        1 / 3
timestamp writes/resolves    8 / 1
terrain/bounds draws         1 / 1
cube/sky draws               1 / 1
cube/sky indices             36 / 36
texture bindings             2
camera uploads               1
depth clears                 1
```

Global query capacity is `24`, per-context high-water is `8`, and the final
sample count equals frame submissions. Frame duration encloses the probe copy,
all four graph barriers, and all three passes. `Terrain`, `TexturedCube`, and
`Skybox` retain separate total/minimum/maximum/last tick aggregates. Timing magnitude is
diagnostic, not a performance threshold.

Hardware and normal packaged WARP each run 1,000 successful presents, with
resize, minimize/restore, and scripted yaw at the 250-, 500-, and 750-frame
checkpoints. The focused packaged-WARP GPU-validation run uses the same
resize and scripted-yaw checkpoints at frames 30 and 90, intentionally skips
the already-covered minimize/restore interval, completes at 120 successful
presents, has a 180-second internal deadline, and has a 240-second CTest
timeout. Every path requires one terrain surface draw, one bounds draw, one
36-index textured-cube draw, one 36-index skybox draw, two texture bindings,
three graph pass events, and eight timestamps per submission; matching
writable/read-only DSV creation per depth-resource generation; no rendering
progress while minimized on the normal paths that exercise it; full fence
retirement; zero DirectX errors/corruption; and no live presentation children.

## Manual visual acceptance

Run the interactive hardware path from the repository root:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe
```

Then verify:

1. The initial frame shows terrain and the checker cube surrounded by a
   predominantly sky-blue background, not the old solid clear color or
   saturated fixture.
2. Move with `W`, `A`, `S`, `D`, `Q`, and `E` without right-dragging. Opaque
   perspective changes, but the sky remains translation-invariant.
3. Hold the right mouse button and drag. The subtle fixture contribution follows
   the changed sample direction and the sky remains behind the opaque scene.
4. Resize from a wide to a visibly different aspect. The sky fills the entire
   background without stretching, gaps, or validation messages.
5. Minimize, restore, and close. No stale frame, depth artifact, or live-object
   error is allowed.

The 4% contribution is intentionally too subtle to serve as a reliable visual
face test. For an exact orientation check, inspect the source TextureCube in
PIX rather than the blended output pixel. Dragging right from the default view
moves from `-Z` toward `+X`; continuing to a half turn reaches `+Z`. Restarting
and dragging left reaches `-X`; dragging upward/downward reaches `+Y`/`-Y`.
The source-asset tags are:

| View direction | DDS face | Red tag |
|---|---|---:|
| `+X` | `+X` | 32 |
| `-X` | `-X` | 64 |
| `+Y` | `+Y` | 96 |
| `-Y` | `-Y` | 128 |
| `+Z` | `+Z` | 160 |
| default `-Z` | `-Z` | 192 |

No axis swap, Z negation, face reorder, row flip, or column flip is applied by
the shader before the temporary RGB treatment. CPU DDS tests remain the
authoritative byte-level check for the green left-to-right and blue
top-to-bottom stored gradients.

## Explicit non-goals

S-002 adds no HDR conversion, image-based lighting, tone mapping, sun or
atmosphere model, clouds, exposure, cubemap mip generation, reflection probes,
texture streaming, general material loading, descriptor allocator, scene/ECS
layer, pixel readback, or golden-image comparison. T-001 supplies only the
current diagnostic terrain tile; S-003 later owns HDR environment lighting.
