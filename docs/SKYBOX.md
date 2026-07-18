# Procedural Daylight Sky Contract

- **Completed through:** `T-002`
- **Last verified:** July 18, 2026
- **Next planned increment:** `REN-001` - renderer boundary cleanup

S-002A is a bounded visual diversion before terrain work continues. It replaces
the temporary flat-blue treatment of Shark's diagnostic cubemap with a basic
daylight sky and a matching directional light for terrain. It adds no clouds,
shadow maps, physical atmosphere, time-of-day system, or image-based lighting.

## Technique and content decision

Skybox was the correct name for the rendering technique: Shark draws a cube
around the camera using a translation-free view matrix and pins it to the
reversed-Z far plane. The painted-room appearance came from the content, not
the geometry. A nearly constant blue mixed with a six-face orientation texture
made the cube faces perceptible.

The `Skybox` pass now evaluates a continuous procedural daylight model from the
normalized world-space view direction. Cube-face identity and UVs have no
influence on its color, so the reused cube is only a rasterization container.
Camera rotation changes the visible direction; camera translation still cannot
move the sky.

The visual model is deliberately small:

- a pale horizon blending continuously to a deeper blue zenith;
- a darker, desaturated nadir below the horizon;
- a warm soft-edged sun disk;
- a restrained angular halo around the sun; and
- one fixed world-space direction toward the sun, shared with terrain.

The sky is evaluated as bounded LDR linear RGB, clamped once, and explicitly
converted to sRGB values for the current `R8G8B8A8_UNORM` back buffer. This is
an output transfer for the temporary presentation path, not HDR rendering,
exposure control, or tone mapping.

## Per-frame daylight constants

The existing root CBV at `b0` is visible to the vertex and pixel stages. One
256-byte frame allocation contains the two matrices, six packed daylight rows,
and the retained diagnostic frame probe:

```text
bytes     HLSL data
0..63     row_major float4x4 view_projection
64..127   row_major float4x4 sky_view_projection
128..143  direction_to_sun.xyz, sun_disk_outer_cosine
144..159  sun_color.xyz, sun_disk_inner_cosine
160..175  zenith_color.xyz, sky_gradient_exponent
176..191  horizon_color.xyz, ambient_strength
192..207  nadir_color.xyz, sun_halo_outer_cosine
208..223  sky_ambient_color.xyz, sun_intensity
224..255  FrameProbe
```

`direction_to_sun` is a finite normalized world-space vector pointing from the
scene toward the sun. The two disk cosine thresholds are ordered for
`smoothstep(outer, inner, dot(view_direction, direction_to_sun))`, avoiding a
per-pixel inverse trigonometric operation. The halo outer cosine bounds a wider,
low-intensity falloff. Colors and scalar controls are finite and nonnegative;
the gradient exponent is positive.

The allocation remains 256-byte aligned, so frame upload allocation count,
bytes written, descriptor count, and fence ownership do not change. The frame
probe moves to byte 224 but remains inside the same allocation and diagnostic
copy.

## Sky and terrain shading

The sky shader normalizes the interpolated cube direction before using its
vertical component. Directions above the horizon blend from `horizon_color` to
`zenith_color`; directions below it blend from the same horizon to
`nadir_color`. This shared horizon endpoint prevents a discontinuity at world
`Y = 0`.

The soft disk is driven by the ordered cosine thresholds. A broader halo fades
to zero at `sun_halo_outer_cosine` and is excluded from the opaque center of the
disk. The result depends only on normalized direction and the daylight
constants, so there can be no cubemap face seam or camera-position parallax.

Terrain's main surface uses the same `direction_to_sun`, `sun_color`,
`sun_intensity`, and `sky_ambient_color`. Its temporary material is a basic
albedo multiplied by:

```text
sky ambient + sun color * sun intensity *
    max(dot(surface normal, direction_to_sun), 0)
```

This is Lambert diffuse lighting with a nonzero ambient floor. It makes hills
respond coherently to the visible sun while preventing back-facing slopes from
becoming black. It does not cast shadows or claim a production material model.
The terrain AABB remains a constant diagnostic color and is not treated as a
lit surface. The checker cube remains the established texture-binding proof.

## Retained cubemap asset proof

The project-owned `8x8` orientation DDS cubemap remains loaded, validated,
uploaded, and represented by its persistent TextureCube SRV. That preserves
the S-001 asset and upload path for later environment-lighting work.

The procedural sky does not sample or bind that cubemap. Consequently,
`StartupCubemap` is no longer imported into the per-frame render graph and the
`Skybox` pass declares no cubemap read. Retaining the startup asset is not a
claim that it contributes visible color.

## Depth and frame graph

The established reversed-Z background policy is unchanged:

```text
sky clip depth    0
comparison        GREATER_EQUAL
depth writes      disabled
DSV               READ_ONLY_DEPTH
resource state    DEPTH_READ
```

Terrain clears the `D32_FLOAT` depth target to zero and writes nearer depths.
The sky therefore fills untouched pixels and cannot overwrite terrain or cube
color.

The graph imports:

```text
BackBuffer
DepthBuffer
CheckerTexture
CubeVertexBuffer
CubeIndexBuffer
TerrainVertexBuffer
TerrainIndexBuffer
```

It retains the pass order `Terrain -> TexturedCube -> Skybox`. Per submitted
frame the exact compilation contract is:

```text
imports                 7
passes                  3
dependencies            2
emitted transitions     4
elided transitions      16
```

The four barriers remain the back-buffer present/render transitions and the
depth write/read transitions around the sky pass. Removing the cubemap import
removes its two equal-state elisions. Only the checker cube binds a texture, so
the exact per-frame texture-binding count is now one.

T-002 adds one query-marker draw inside `Terrain`, bringing the submitted-frame
total to five indexed draws, but adds no pass, PSO, timestamp interval,
attachment, or queue. The marker is packed into the existing terrain buffers,
so the static scene remains four geometry buffers. The existing PIX hierarchy
and eight-query frame layout remain:

```text
Frame
  Terrain
  TexturedCube
  Skybox
```

Cubemap creation, upload, face, mip, source-byte, SRV, and persistent-descriptor
startup counters remain intact even though per-frame cubemap use is zero.

## Manual acceptance

Run the interactive hardware path from the repository root:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe
```

Verify:

1. The background reads as one open sky, with a recognizable horizon and deeper
   zenith, rather than four blue walls or visible cube-face changes.
2. The warm sun disk and soft halo are visible near their fixed world direction.
3. Hold the right mouse button and look around. The gradient stays continuous,
   the sun remains anchored in the world, and no seam appears at cube edges.
4. Move with `W`, `A`, `S`, `D`, `Q`, and `E` without rotating. The terrain and
   cube perspective changes, but the sky and sun do not translate.
5. Terrain slopes facing the sun are brighter than slopes facing away, while
   shadow-facing slopes remain readable from ambient light.
6. Press `F1`, resize, minimize, restore, and close. Terrain diagnostics, sky
   coverage, reversed-Z occlusion, and Direct3D validation remain clean.

## Explicit non-goals and continuation

S-002A adds no physical Rayleigh or Mie scattering, volumetric or texture
clouds, cloud shadows, shadow maps, cascaded sunlight, dynamic time of day,
weather-driven sky state, HDR framebuffer, exposure, tone mapping, image-based
lighting, cubemap conversion, reflection probes, or final material system.

The procedural daylight sky remains the stable basic background while the
engine grows. T-002 adds canonical terrain queries and a cyan normal pin
without changing this sky contract. Work now proceeds to `REN-001`, which must
move renderer orchestration without changing pixels, pass order, or accounting;
`T-003` terrain materials follow it.
