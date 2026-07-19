# HLSL Graphics Pipeline Contract

- **Completed through:** `PHY-001`
- **Last verified:** July 19, 2026

Shark compiles all production HLSL at build time with a pinned retail DXC and
creates immutable Direct3D 12 pipeline state during renderer startup. W-001
adds one bounded visual-water program and immutable PSO to the existing cube,
terrain, sky, material-sphere, and tone-map programs. The
pipeline still includes S-003's shared image-based-lighting helpers,
material-sphere proof, linear-HDR scene target, and final tone-map program.
PHY-001 changes only the material-sphere vertex transform: three `b2` root
constants translate its existing geometry to an interpolated body position.
This remains a focused scene contract, not a general shader asset,
material-graph, or pipeline-cache system.

## Pinned compiler and artifacts

Configuration accepts only the `dxc.exe` restored under the active vcpkg host
triplet, requires its adjacent `dxcompiler.dll` and `dxil.dll`, and verifies
retail version `1.9.2602.24`. Shark never falls back to a compiler on `PATH`.
DXC is a build-time process; no DXC binary is linked or copied beside the
sandbox.

The tracked production sources are:

```text
shaders/cube/cube.hlsl
shaders/sky/skybox.hlsl
shaders/terrain/terrain.hlsl
shaders/water/water.hlsl
shaders/material_sphere/material_sphere.hlsl
shaders/tone_map/tone_map.hlsl
shaders/shared/camera_constants.hlsli
shaders/shared/daylight.hlsli
shaders/shared/pbr_ibl.hlsli
```

Each program compiles `VSMain` as `vs_6_0` and `PSMain` as `ps_6_0` with:

```text
-HV 2021
-Zpr
-Ges
-WX
-encoding utf8
-fdiagnostics-format=msvc
-Zi
-Qembed_debug
-Qsource_in_debug_module
```

Debug adds `-Od`; Release adds `-O3`. Each stage emits standalone DXIL, a
generated C++ byte-array header, a PDB, and a depfile under:

```text
out/build/windows-vs2026/generated/shaders/<Config>/
```

The executable embeds all twelve generated stage arrays and opens no runtime DXIL
file. Depfiles track the primary HLSL and shared includes. Permanent build
checks prove a shared-include edit rebuilds its consumers, malformed HLSL
fails, and a warning fails under `-WX`.

Run the normal shader targets and build checks with:

```powershell
& $cmake --build --preset windows-debug --target SharkCubeShaders SharkSkyboxShaders SharkTerrainShaders SharkWaterShaders SharkMaterialSphereShaders SharkToneMapShaders
& $ctest --preset windows-debug -R '^build\.shader_'
```

## Public bytecode boundary

`shark::renderer::RendererConfig` accepts COM-free `ShaderBytecodeView`
records, each containing a pointer and byte count. It carries the cube, sky,
terrain, water, material-sphere, and tone-map vertex/pixel arrays. The private D3D12
backend checks the DXIL container signature and borrows each array only during
synchronous root-signature/PSO creation. `Renderer` retains no caller pointer.

Direct3D validates stage and pipeline compatibility in
`CreateGraphicsPipelineState`. PSO creation never occurs in the frame loop.
Every immutable PSO survives swap-chain resize and is released only after
explicit renderer shutdown drains and retires submitted work.

## Shared shading conventions

The root frame CBV preserves this exact 256-byte layout:

```text
0..63     row-major view_projection
64..127   row-major sky_view_projection
128..143  direction_to_sun.xyz, sun_disk_outer_cosine
144..159  sun_color.xyz, sun_disk_inner_cosine
160..175  zenith_color.xyz, sky_gradient_exponent
176..191  horizon_color.xyz, ambient_strength
192..207  nadir_color.xyz, sun_halo_outer_cosine
208..223  sky_ambient_color.xyz, sun_intensity
224..255  retained FrameProbe
```

CPU and HLSL matrices are row-major, vectors are row vectors, and transforms
use `mul(vector, matrix)`. Scene shaders write nonnegative linear color to the
HDR target. Only the final tone-map shader performs the output transfer.

The terrain shader:

- consumes 24-byte interleaved `POSITION`/`NORMAL` vertices;
- derives world-XZ material UVs and a cotangent frame from derivatives;
- blends deterministic ground/rock albedo, normal, and roughness arrays;
- evaluates direct-sun dielectric GGX with fixed `F0=0.04`;
- evaluates diffuse irradiance plus roughness-selected GGX-prefiltered
  specular through the split-sum BRDF LUT; and
- preserves negative-Y normal sentinels for the magenta bounds and cyan query
  marker.

The bounded environment convolution excludes the directional sun. Sky renders
its disk/halo analytically, and terrain/sphere add the same analytic direct
light separately, avoiding blocky low-resolution sun texels and duplicate
energy. The stored diffuse irradiance is a cosine-weighted hemisphere integral;
the PBR shader divides it by pi for Lambert-normalized diffuse response.
Specular reconstruction uses the fixed dielectric `F0` in
`prefiltered * (F0 * scale + bias)` because the LUT's scale/bias already
integrates angular Fresnel.

The material-sphere shader consumes deterministic position/normal geometry,
adds a three-float `b2` translation to each authored position, and uses the same
sun and IBL functions on a glossy neutral dielectric. The translation is the
only PHY-001 simulation/render bridge; the sphere remains a lighting proof,
not a general material instance.

The cube shader samples the deterministic checker. The sky shader forces
reversed-Z far depth, normalizes the world direction, and either samples the
HDR radiance cube or evaluates the retained procedural-daylight fallback.
`F3` selects the same environment mode for sky, terrain, and sphere.

The tone-map vertex stage synthesizes a fullscreen triangle from
`SV_VertexID`. Its pixel stage loads the `R16G16B16A16_FLOAT` scene target,
applies the fixed ACES-fitted curve, converts linear RGB to sRGB, and writes
opaque color to the `R8G8B8A8_UNORM` swap-chain buffer.

## Root signatures and descriptors

The fixed shader-visible CBV/SRV/UAV heap contains ten persistent slots:

| Slot | SRV |
|---:|---|
| 0 | deterministic checker Texture2D |
| 1 | retained S-001 orientation TextureCube |
| 2 | terrain albedo Texture2DArray |
| 3 | terrain normal Texture2DArray |
| 4 | terrain roughness Texture2DArray |
| 5 | diffuse irradiance TextureCube |
| 6 | GGX-prefiltered specular TextureCube |
| 7 | split-sum BRDF Texture2D |
| 8 | HDR radiance TextureCube |
| 9 | resize-owned HDR scene-color Texture2D |

The fixed slots are scene-specific and do not establish a general persistent
descriptor allocator or bindless convention.

- The cube signature contains the root frame CBV, one checker SRV table, and a
  point-wrap static sampler.
- The terrain signature contains the frame CBV, material/environment root
  constants, one six-SRV table spanning slots 2-7, and an anisotropic-wrap
  static sampler. The material sphere reuses it and reads three vertex-visible
  translation constants from its `b2` root parameter.
- The water signature contains the frame CBV, 20 surface root constants, one
  radiance SRV table, and a linear-clamp static sampler.
- The sky signature contains the frame CBV, environment-mode constants, one
  radiance SRV table, and a linear-clamp static sampler.
- The tone-map signature contains one scene-color SRV table.

Per submitted frame those focused tables produce exactly five texture-table
binds: terrain/IBL, checker, sky radiance, water radiance, and HDR scene color.

## Pipeline-state contract

Renderer startup creates:

- one textured-cube PSO;
- one skybox PSO;
- terrain solid, terrain wireframe, and terrain diagnostic-line PSOs;
- one material-sphere PSO;
- one visual-water PSO; and
- one tone-map PSO.

Cube, terrain, material-sphere, and sky color output is
`DXGI_FORMAT_R16G16B16A16_FLOAT`. Scene geometry uses the established
`D32_FLOAT` reversed-Z depth target and `GREATER_EQUAL`. Terrain and cube write
depth; bounds/query lines, sky, and water do not. Sky first binds the read-only
DSV after the graph transitions depth to `DEPTH_READ`; water reuses that state
after sky and composites with premultiplied transparency.

The water shader has no input layout or geometry resource. `VSMain` expands a
flat six-vertex quad from `SV_VertexID` over the local domain centered at
`(-128,-4,-128)` with X/Z half-extents `64/56`. `PSMain` intersects that
domain with warped `rho <= 1` using semi-axes `56/48`, warp offsets `1152/1568`,
and divisors `512/1024`; this selects the intended spawn-side component without
assuming the inequality is one globally bounded lobe. It emits premultiplied
linear-HDR color using
straight-through terrain transmission, depth-proxy absorption/tint, Schlick
Fresnel, environment reflection/refraction approximations, two animated
normal-only wave bands, and analytic sun glint. The PSO uses
`ONE/INV_SRC_ALPHA` blending, read-only reversed-Z depth, and no depth writes.
Before upload, visual time wraps at the waves' shared `40*pi`-second phase
period, preventing long-session float overflow and a discontinuity at wrap.

The tone-map PSO uses no vertex buffer, input layout, or depth target. It writes
one opaque `DXGI_FORMAT_R8G8B8A8_UNORM` render target from
`DrawInstanced(3, 1, 0, 0)`.

## Upload and draw contract

One startup direct-queue submission copies:

- four immutable geometry buffers for cube and packed shared
  terrain-LOD/chunk-bounds/query-marker/material-sphere data;
- checker and retained DDS cubemap textures;
- 36 terrain-material subresources; and
- 79 derived HDR environment subresources containing 284,608 meaningful
  RGBA32-float bytes.

Thirteen initialization barriers establish final input/shader states before a
bounded fence wait releases temporary upload storage.

Each non-minimized frame then:

1. extracts the current Direct3D frustum, selects visible terrain chunks, and
   chooses each visible chunk's LOD from camera-to-AABB distance;
2. stages one 256-byte constants/probe record;
3. composes the exact 15-import/five-pass HDR frame graph;
4. executes `Terrain`, including one selected LOD0/coarse surface per visible
   chunk plus the sphere translated from PHY-001's immutable interpolated body
   snapshot; default-off `F4` diagnostics additionally draw each visible
   chunk's magenta bounds and the query marker;
5. executes `TexturedCube`;
6. executes the far-depth `Skybox`;
7. composites the local-domain `Water` pass;
8. executes `ToneMap` to the current back buffer;
9. restores declared graph final states; and
10. submits, signals the context fence, and presents without an unconditional
   post-frame drain.

For `V0` visible LOD0 chunks, `Vc` visible coarse chunks, and `V=V0+Vc`, the
submitted-frame draw contract is:

```text
LOD0 terrain chunks      V0 * DrawIndexedInstanced(1,536, ...)
coarse terrain chunks    Vc * DrawIndexedInstanced(864, ...)
material sphere at b2 translation
                         DrawIndexedInstanced(1,584, ...)
visible chunk AABBs      F4 ? V * DrawIndexedInstanced(24, ...) : 0
terrain query marker     F4 ? DrawIndexedInstanced(6, ...) : 0
textured cube            DrawIndexedInstanced(36, ...)
visual water             DrawInstanced(6, 1, 0, 0)
skybox                   DrawIndexedInstanced(36, ...)
tone map                 DrawInstanced(3, 1, 0, 0)
```

Each chunk selects one contiguous LOD0 or coarse first-index/count range into
the shared 58,081-vertex stream; its maximum surface index is 58,080. The
matching bounds draw selects its own packed eight-vertex/24-index range. Fine
and coarse terrain indices occupy `0..345,599` and `345,600..539,999`; bounds,
marker, and sphere begin at 540,000, 545,400, and 545,406. The sphere remains
in those terrain buffers, so the normal `V + 3` indexed draws and optional
`F4` diagnostic draws still use four geometry buffers. Initial/resized smoke
poses select `V0/Vc=0/93`; the turned overview selects `0/72`; and the final
smoke-only `(16, -1, 0)` near pose selects `1/60` with unchanged yaw/pitch.
Both packed terrain index ranges are therefore live. Exact per-frame graph
accounting is 15 imports, five passes, five dependencies, six transitions,
and 34 elisions. Diagnostics reserve 12 timestamps per context: frame
begin/end plus begin/end for each pass.

## Acceptance and non-goals

The hardware smoke path requires 1,000 successful presents, normal packaged
WARP requires 600, and focused packaged WARP with GPU-based validation requires
120. They exercise both terrain fill modes, the exact
`0/93 -> 0/72 -> 1/60` schedule, both D3D12 terrain index ranges, all three
terrain material views, both environment modes, resize, camera rotation,
translation-only near motion, and frame retirement; compact/focused CPU tests
retain broader mixed-LOD coverage. T-007 hardware Debug/Release, normal WARP,
and focused GBV validation passed; T-008 retains these acceptance requirements
and passed them in both active Debug and Release test runs. The focused path
alone uses
`640x360 -> 480x300`; normal paths retain `1280x720 -> 960x600`, with identical
aspect changes. The smoke validates resources, commands, counts, and lifetime;
it does not compare pixels.

Manual acceptance requires coherent environment response on terrain, water,
and the glossy sphere; a translation-invariant HDR sky; clean `F3` switching
to the procedural fallback; stable reversed-Z shoreline occlusion; subtle
normal-wave motion; and a finite tone-mapped result through
resize/minimize/restore. The lake must stay inside the local quad-domain and
`rho <= 1` intersection while canonical terrain remains unchanged.

S-003 adds no shader reflection, artifact database, root-signature versioning,
PSO hash/cache, runtime compilation, hot reload, general material graph,
arbitrary environment probes, runtime environment convolution, automatic
exposure, HDR display output, or image-comparison testing. Those omissions
preserve the bounded San Andreas-class product scope while allowing modern HDR
implementation quality.

T-006 historically completed its hardware, WARP, and focused GPU-validation
presentation gates without adding or changing HLSL, root signatures, or PSOs.
`T-007` completed the fixed-seed natural-height contract on July 19, 2026 and
changed none of those pipeline objects. Hardware Debug/Release, normal WARP,
and focused GBV passed its four-phase graphics-validation contract; that
evidence remains historical.

W-001 adds the water HLSL pair, root signature, premultiplied-alpha PSO, one
environment-radiance binding, six-vertex procedural draw, named PIX scope,
and timestamp pair. It deliberately adds no input element, geometry buffer,
water texture, persistent descriptor, or simulated state. Terrain retains the
exact `0/93 -> 0/72 -> 1/60` smoke schedule. Rain remains deferred and the San
Andreas-class ceiling is unchanged.

PHY-001 adds the material-sphere `b2` translation without adding a shader
stage, PSO, descriptor, geometry buffer, graph pass, water state, or fluid
coupling. The next increment is `PHY-002`, sphere contact with canonical
terrain.
