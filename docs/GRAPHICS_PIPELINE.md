# HLSL Graphics Pipeline Contract

- **Completed through:** `T-008`
- **Last verified:** July 19, 2026

Shark compiles all production HLSL at build time with a pinned retail DXC and
creates immutable Direct3D 12 pipeline state during renderer startup. T-008
retains T-006's cube, terrain, sky, material-sphere, and tone-map programs while
composing only the CPU-generated heights and scenario metadata in the bounded
225-chunk fixture. The
pipeline still includes S-003's shared image-based-lighting helpers,
material-sphere proof, linear-HDR scene target, and final tone-map program.
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

The executable embeds all ten generated stage arrays and opens no runtime DXIL
file. Depfiles track the primary HLSL and shared includes. Permanent build
checks prove a shared-include edit rebuilds its consumers, malformed HLSL
fails, and a warning fails under `-WX`.

Run the normal shader targets and build checks with:

```powershell
& $cmake --build --preset windows-debug --target SharkCubeShaders SharkSkyboxShaders SharkTerrainShaders SharkMaterialSphereShaders SharkToneMapShaders
& $ctest --preset windows-debug -R '^build\.shader_'
```

## Public bytecode boundary

`shark::renderer::RendererConfig` accepts COM-free `ShaderBytecodeView`
records, each containing a pointer and byte count. It carries the cube, sky,
terrain, material-sphere, and tone-map vertex/pixel arrays. The private D3D12
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

The material-sphere shader consumes deterministic position/normal geometry and
uses the same sun and IBL functions on a glossy neutral dielectric. It is a
lighting proof, not a general material instance.

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
  static sampler. The material sphere reuses it.
- The sky signature contains the frame CBV, environment-mode constants, one
  radiance SRV table, and a linear-clamp static sampler.
- The tone-map signature contains one scene-color SRV table.

Per submitted frame those focused tables produce exactly four texture-table
binds: terrain/IBL, checker, sky radiance, and HDR scene color.

## Pipeline-state contract

Renderer startup creates:

- one textured-cube PSO;
- one skybox PSO;
- terrain solid, terrain wireframe, and terrain diagnostic-line PSOs;
- one material-sphere PSO; and
- one tone-map PSO.

Cube, terrain, material-sphere, and sky color output is
`DXGI_FORMAT_R16G16B16A16_FLOAT`. Scene geometry uses the established
`D32_FLOAT` reversed-Z depth target and `GREATER_EQUAL`. Terrain and cube write
depth; bounds/query lines and sky do not. Sky binds the read-only DSV after the
graph transitions depth to `DEPTH_READ`.

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
3. composes the exact 15-import/four-pass HDR frame graph;
4. executes `Terrain`, including one selected LOD0/coarse surface per visible
   chunk plus the sphere; default-off `F4` diagnostics additionally draw each
   visible chunk's magenta bounds and the query marker;
5. executes `TexturedCube`;
6. executes the far-depth `Skybox`;
7. executes `ToneMap` to the current back buffer;
8. restores declared graph final states; and
9. submits, signals the context fence, and presents without an unconditional
   post-frame drain.

For `V0` visible LOD0 chunks, `Vc` visible coarse chunks, and `V=V0+Vc`, the
submitted-frame draw contract is:

```text
LOD0 terrain chunks      V0 * DrawIndexedInstanced(1,536, ...)
coarse terrain chunks    Vc * DrawIndexedInstanced(864, ...)
material sphere          DrawIndexedInstanced(1,584, ...)
visible chunk AABBs      F4 ? V * DrawIndexedInstanced(24, ...) : 0
terrain query marker     F4 ? DrawIndexedInstanced(6, ...) : 0
textured cube            DrawIndexedInstanced(36, ...)
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
Both packed terrain index ranges are therefore live. Exact per-frame graph accounting remains
15 imports, four passes, three dependencies, six transitions, and 31 elisions.
Diagnostics retain ten timestamps per context: frame begin/end plus begin/end
for each pass.

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

Manual acceptance requires coherent environment response on terrain and the
glossy sphere, a translation-invariant HDR sky, clean `F3` switching to the
procedural fallback, stable reversed-Z occlusion, and a finite tone-mapped
back-buffer result through resize/minimize/restore. T-008's dry basin must use
the existing terrain material pipeline, and no water shader, PSO, draw, or pixel
may exist.

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

`T-008` adds no HLSL source, compiled shader, root-signature parameter, PSO,
input element, texture binding, water draw, or water pixel. The composite
terrain retains the exact `0/93 -> 0/72 -> 1/60` smoke schedule. The full Debug
build and all `150/150` tests passed in 195.60 seconds; the Release build and
all `150/150` passed in 157.45 seconds. Registered shader and graphics gates
retained exact smoke accounting in both active configurations. Rain remains
deferred and the San Andreas-class ceiling is unchanged. The next increment is
`W-001`: clip a static water plane to T-008's immutable analytic upper support
at the published waterline; canonical-terrain depth testing determines the
visible shoreline, terrain remains unchanged, and no fluid simulation is
claimed.
