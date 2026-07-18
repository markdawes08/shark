# HLSL Graphics Pipeline Contract

- **Completed through:** `S-003`
- **Last verified:** July 18, 2026

Shark compiles all production HLSL at build time with a pinned retail DXC and
creates immutable Direct3D 12 pipeline state during renderer startup. S-003
extends the existing cube, terrain, and sky programs with shared
image-based-lighting helpers, one material-sphere proof, a linear-HDR scene
target, and a final tone-map program. This remains a focused scene contract,
not a general shader asset, material-graph, or pipeline-cache system.

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

- four immutable geometry buffers for cube and packed
  terrain/bounds/query-marker/material-sphere data;
- checker and retained DDS cubemap textures;
- 36 terrain-material subresources; and
- 79 derived HDR environment subresources containing 284,608 meaningful
  RGBA32-float bytes.

Thirteen initialization barriers establish final input/shader states before a
bounded fence wait releases temporary upload storage.

Each non-minimized frame then:

1. stages one 256-byte constants/probe record;
2. composes the exact 15-import/four-pass HDR frame graph;
3. executes `Terrain`, including surface, sphere, bounds, and marker draws;
4. executes `TexturedCube`;
5. executes the far-depth `Skybox`;
6. executes `ToneMap` to the current back buffer;
7. restores declared graph final states; and
8. submits, signals the context fence, and presents without an unconditional
   post-frame drain.

The submitted-frame draw contract is:

```text
terrain surface          DrawIndexedInstanced(6,144, ...)
material sphere          DrawIndexedInstanced(1,584, ...)
terrain AABB             DrawIndexedInstanced(24, ...)
terrain query marker     DrawIndexedInstanced(6, ...)
textured cube            DrawIndexedInstanced(36, ...)
skybox                   DrawIndexedInstanced(36, ...)
tone map                 DrawInstanced(3, 1, 0, 0)
```

The sphere is packed into the existing terrain buffers, so the six indexed
draws still use four geometry buffers. Exact per-frame graph accounting is 15
imports, four passes, three dependencies, six transitions, and 31 elisions.
Diagnostics allocate ten timestamps per context: frame begin/end plus
begin/end for each pass.

## Acceptance and non-goals

The hardware and normal packaged-WARP smoke paths require 1,000 successful
presents; focused packaged WARP with GPU-based validation requires 120. They
exercise both terrain fill modes, all three terrain material views, both
environment modes, resize, camera rotation, frame retirement, and clean
DirectX validation. The smoke validates resources, commands, counts, and
lifetime; it does not compare pixels.

Manual acceptance requires coherent environment response on terrain and the
glossy sphere, a translation-invariant HDR sky, clean `F3` switching to the
procedural fallback, stable reversed-Z occlusion, and a finite tone-mapped
back-buffer result through resize/minimize/restore.

S-003 adds no shader reflection, artifact database, root-signature versioning,
PSO hash/cache, runtime compilation, hot reload, general material graph,
arbitrary environment probes, runtime environment convolution, automatic
exposure, HDR display output, or image-comparison testing. Those omissions
preserve the bounded San Andreas-class product scope while allowing modern HDR
implementation quality.

`S-003` was completed on July 18, 2026. The next increment is `T-004`, terrain
chunk culling, followed by `T-005`, bounded visual LOD.
