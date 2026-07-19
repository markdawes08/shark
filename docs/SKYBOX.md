# Sky and HDR Environment-Lighting Contract

- **Completed through:** `T-005`
- **Last verified:** July 19, 2026
- **Next planned increment:** `R-001` - wind-driven GPU rain

Shark still uses a skybox as the background rasterization technique: a cube is
drawn around the camera with a translation-free view matrix and forced to the
reversed-Z far plane. S-002A established the basic continuous procedural
daylight fallback. S-003 adds a bounded linear-HDR environment, derives the
image-based-lighting data used by the scene, and sends all scene color through
a final tone-map pass.

This is intentionally a small, modern lighting foundation inside Shark's San
Andreas-class feature ceiling. It is not a physical atmosphere, weather model,
dynamic time-of-day system, cloud renderer, or RAGE-scale lighting stack.

## Deterministic HDR source and derived maps

`shark::assets::EnvironmentLighting` owns project-generated CPU data and uses
no file, random state, platform API, or third-party image. Generation begins
with one fixed `64x32` latitude-longitude daylight image in linear
RGBA32-float:

```text
source extent                 64 x 32
source texels                 2,048
source meaningful bytes      32,768
source color space            linear HDR
```

The generator converts and filters that source into:

```text
radiance cubemap              32 x 32, 6 faces, 6 mips
diffuse irradiance cubemap     8 x  8, 6 faces, 1 mip
GGX-prefiltered specular      32 x 32, 6 faces, 6 mips
split-sum BRDF LUT            32 x 32, 1 mip
derived subresources          79
derived texels                17,788
derived meaningful bytes      284,608
derived format                RGBA32 float
```

The low-resolution source deliberately excludes the directional sun. The sky
shader draws its smooth disk/halo analytically from the shared daylight
settings, and terrain/sphere evaluate the same sun as a direct light. This
avoids turning the sun into a bright cubemap texel block and avoids counting
its energy once in the convolution and again as direct lighting.

Cubemap storage is face-major, then mip-minor. The BRDF LUT stores the
split-sum scale and bias in red/green. Shading reconstructs dielectric
specular as `prefiltered * (F0 * scale + bias)`; the LUT has already integrated
the angular Fresnel factor. CPU tests lock the dimensions, mip
counts, byte determinism, finite values, convolution behavior, GGX roughness
chain, and BRDF bounds. The small resolutions are deliberate acceptance
fixtures, not a production content-quality target.

The renderer validates the borrowed upload views synchronously, creates three
`DXGI_FORMAT_R32G32B32A32_FLOAT` TextureCube resources plus one matching
Texture2D LUT, uploads all 79 subresources in the existing one-time
`StaticSceneUpload`, and retains no caller CPU pointer. These four resources
account for 284,608 meaningful uploaded bytes. D3D12 row-pitch padding is
implementation storage and is not included in that meaningful-byte total.

## Sky technique and lighting modes

The `Skybox` vertex stage reuses the cube positions, evaluates the
translation-free `sky_view_projection`, and forces clip depth to zero. The
pixel stage normalizes the world-space view direction. In the default
image-based mode it samples the sun-free radiance cube and then adds the smooth
analytic directional sun disk/halo. In fallback mode it evaluates the retained
analytic horizon/zenith/nadir gradient, sun disk, and halo directly.

`F3` toggles the complete environment mode:

```text
HDR image-based lighting
procedural daylight fallback
```

The choice affects both the visible sky and material lighting. It does not
change the camera, terrain data, material view, graph topology, resource
ownership, or simulation state. `F1` continues to toggle terrain fill and `F2`
continues to cycle shaded, material-weight, and world-normal views.

The retained project-owned `8x8` sRGB DDS orientation cubemap is still loaded
and uploaded as the S-001 asset-path proof. It is separate from the generated
HDR radiance cube and is not imported, bound, or sampled by the normal frame.

## Terrain and material-sphere proof

The shaded terrain samples the same environment through:

- the `8x8` diffuse irradiance cube for diffuse response;
- the six-mip GGX-prefiltered specular cube for roughness-dependent
  reflections; and
- the `32x32` split-sum BRDF LUT for the view/roughness response.

It combines that IBL with the existing direct sun and the T-003 ground/rock
material blend. The canonical `HeightTileSurface` remains authoritative for
height, normal, bounds, and ray queries; environment lighting is visual data
and cannot alter those results.

The irradiance texture stores the cosine-weighted hemisphere integral. The
shared PBR shader divides it by pi for the Lambert BRDF; the direct analytic
sun is added separately.

One deterministic 266-vertex/1,584-index glossy neutral dielectric sphere is
packed into the existing terrain vertex/index resources and drawn inside the
`Terrain` pass. It uses the same direct sun, irradiance, prefiltered specular,
and BRDF LUT as the terrain. This is the acceptance proof that both objects
share one environment-lighting model, not the beginning of a general entity,
mesh, or material system.

## HDR scene target and final presentation

`Terrain`, `TexturedCube`, and `Skybox` write linear scene color to one
resize-owned:

```text
DXGI_FORMAT_R16G16B16A16_FLOAT
```

`ToneMap` then reads that HDR scene target and draws a fullscreen triangle to
the `DXGI_FORMAT_R8G8B8A8_UNORM` swap-chain back buffer. Its pixel stage applies
the fixed ACES-fitted curve and explicit linear-to-sRGB transfer. Scene shaders
do not perform their own output transfer. An effective resize recreates the
HDR texture, RTV, and SRV alongside the depth and swap-chain views after the
normal queue drain.

This establishes a bounded HDR scene/presentation boundary. S-003 adds no
automatic exposure, eye adaptation, bloom, color grading, HDR10 output,
display metadata, or configurable tone-map operator.

## Depth, graph, and diagnostics

The reversed-Z background policy remains:

```text
sky clip depth    0
comparison        GREATER_EQUAL
depth writes      disabled
DSV               READ_ONLY_DEPTH
resource state    DEPTH_READ
```

Terrain clears `D32_FLOAT` depth to zero, terrain and cube write nearer values,
and the sky fills untouched pixels. `ToneMap` uses no depth.

The frame graph imports:

```text
BackBuffer
SceneColor
DepthBuffer
CheckerTexture
CubeVertexBuffer
CubeIndexBuffer
TerrainVertexBuffer
TerrainIndexBuffer
TerrainAlbedoLayers
TerrainNormalLayers
TerrainRoughnessLayers
EnvironmentRadiance
EnvironmentIrradiance
EnvironmentPrefilteredSpecular
EnvironmentBrdfLut
```

Its exact submitted-frame contract is:

```text
pass order              Terrain -> TexturedCube -> Skybox -> ToneMap
imports                 15
passes                   4
dependencies             3
emitted transitions      6
elided transitions      31
texture-table binds      4
```

The six barriers move scene color into render-target state, depth into
read-only state, the back buffer into render-target state for `ToneMap`, scene
color into shader-read state, then restore the back buffer and depth to their
declared final states. Persistent environment and material resources remain in
pixel-shader-read state.

With `V` visible terrain chunks, a submitted frame issues `2V + 4` indexed
draws: `V` selected LOD0/coarse chunk surfaces, the material sphere, `V`
matching magenta chunk bounds, the terrain query marker, textured cube, and
skybox. LOD0 surfaces use 384 indices and coarse surfaces use 240. `ToneMap`
adds one non-indexed fullscreen-triangle draw. Both surface ranges, bounds,
marker, and sphere share the packed terrain buffers, so the static scene still
contains four geometry buffers. Sky remains exactly one indexed draw.

The stable PIX hierarchy is:

```text
Frame
  Terrain
  TexturedCube
  Skybox
  ToneMap
```

Each of the four pass intervals has a begin/end timestamp. Together with frame
begin/end, this requires exactly ten timestamps per frame-context slice and 30
timestamps across three contexts. Timing results remain fence-delayed and add
no normal-frame queue drain.

## Manual acceptance

Run the interactive hardware path:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe
```

Verify:

1. The default background reads as one continuous open HDR daylight
   environment, without visible cube faces, seams, or painted-wall bands.
2. The terrain and glossy material sphere respond coherently to the same
   environment and direct sun.
3. Right-drag rotation changes the visible world direction; translation with
   `W`, `A`, `S`, `D`, `Q`, and `E` does not translate the sky.
4. `F3` switches sky and object lighting together between HDR IBL and the
   procedural fallback. Both modes remain finite and visually usable.
5. `F1`/`F2`, resize, minimize/restore, and clean shutdown preserve depth,
   material diagnostics, tone mapping, and Direct3D validation.

## Explicit non-goals and continuation

S-003 adds no file-backed HDR importer, arbitrary environment probe system,
runtime convolution, dynamic reflection capture, local reflection volumes,
shadow maps, atmosphere scattering, volumetric clouds, weather-driven sky,
time of day, exposure adaptation, bloom, color grading, HDR display output, or
image-comparison test.

Radiance mip downsampling is a bounded face-local fixture, not a claimed
cross-face seam-hardened production filter. The current sky samples radiance
mip zero, and material roughness uses the separately GGX-prefiltered specular
cube, so that limitation is not on the visible S-003 path.

It does not broaden Shark beyond the approved San Andreas-class local-sandbox
ceiling. `T-005` completed one bounded coarse terrain LOD on July 19, 2026
without changing sky pixels, resources, or the sky draw policy. The next
increment is `R-001`: add seeded, bounded GPU rain driven by adjustable
precipitation rate and wind.
