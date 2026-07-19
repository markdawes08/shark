# Building Shark

- **Completed through:** `T-004`
- **Last verified:** July 19, 2026

Shark currently supports Windows 11 x64 with Visual Studio 2026, the MSVC
14.50 LTS toolset, CMake 4.2 or newer, and Windows SDK 10.0.26100 exactly.
Run the prerequisite checker before configuring:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-prerequisites.ps1
```

The `F-002 gate` must report `READY`. The PIX desktop application is not needed
to compile or run Shark, but it is required to inspect G-007 captures manually.
The pinned WinPixEventRuntime used by the executable is restored by vcpkg.

## Fresh command-line build

The Visual Studio installation supplies CMake and vcpkg without adding them to
the normal PowerShell `PATH`. From the repository root, use this discovery block
once per PowerShell session:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = (& $vswhere -latest -products * -version "[18.0,19.0)" -requires "Microsoft.VisualStudio.Component.VC.14.50.18.0.x86.x64" -property installationPath).Trim()
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
```

Configure with a fresh CMake cache, restore dependencies, then build and test
both supported configurations:

```powershell
& $cmake --fresh --preset windows-vs2026
& $cmake --build --preset windows-debug
& $ctest --preset windows-debug
& $cmake --build --preset windows-release
& $ctest --preset windows-release
```

The configure preset selects the Visual Studio 18 2026 x64 generator, Windows
SDK 10.0.26100, MSVC 14.50 specifically rather than the newer default toolset,
and the pinned vcpkg registry. Shark's overlay triplet applies that same MSVC
14.50 toolset to built dependencies. The configure step restores every declared
dependency. G-001 consumes DirectX Headers/Guids and the Agility runtime;
G-004 consumes DXC as a build-time host tool, G-005 consumes the header-only
DirectXMath package for camera and transform math, and G-007 links the pinned
WinPixEventRuntime so named command-list events remain available in Debug and
Release. G-006's planner and executor remain engine-owned. S-001 privately
links the pinned DirectXTex runtime behind the engine-owned DDS cubemap
boundary. T-003's material fixture and mip generation are first-party CPU code
and add no dependency or runtime content file. S-003's deterministic HDR
latitude-longitude source, environment conversion, convolution, GGX
prefiltering, and BRDF integration are also first-party CPU code with no new
dependency or runtime content file.

The checked-in toolchain also scopes `UCRTContentRoot` to the complete Windows
SDK payload for the CMake process. This avoids a Visual Studio 2026 installation
case where a partial 64-bit Windows Kits registry entry otherwise hides the
actual UCRT libraries under `Program Files (x86)`; it does not modify the
registry or the machine environment.

Expected first-party outputs and G-001 development runtimes are:

```text
out/build/windows-vs2026/bin/Debug/SharkSandbox.exe
out/build/windows-vs2026/bin/Debug/D3D12/D3D12Core.dll
out/build/windows-vs2026/bin/Debug/D3D12/d3d12SDKLayers.dll
out/build/windows-vs2026/bin/Debug/d3d10warp.dll
out/build/windows-vs2026/bin/Debug/DirectXTex.dll
out/build/windows-vs2026/bin/Debug/WinPixEventRuntime.dll
out/build/windows-vs2026/bin/Debug/content/sky/shark_orientation_sky_srgb.dds
out/build/windows-vs2026/bin/Debug/SharkTests.exe
out/build/windows-vs2026/bin/Release/SharkSandbox.exe
out/build/windows-vs2026/bin/Release/D3D12/D3D12Core.dll
out/build/windows-vs2026/bin/Release/D3D12/d3d12SDKLayers.dll
out/build/windows-vs2026/bin/Release/d3d10warp.dll
out/build/windows-vs2026/bin/Release/DirectXTex.dll
out/build/windows-vs2026/bin/Release/WinPixEventRuntime.dll
out/build/windows-vs2026/bin/Release/content/sky/shark_orientation_sky_srgb.dds
out/build/windows-vs2026/bin/Release/SharkTests.exe
out/build/windows-vs2026/lib/Debug/SharkEngine.lib
out/build/windows-vs2026/lib/Release/SharkEngine.lib
out/build/windows-vs2026/generated/shaders/Debug/cube.vertex.dxil
out/build/windows-vs2026/generated/shaders/Debug/cube.pixel.dxil
out/build/windows-vs2026/generated/shaders/Debug/skybox.vertex.dxil
out/build/windows-vs2026/generated/shaders/Debug/skybox.pixel.dxil
out/build/windows-vs2026/generated/shaders/Debug/terrain.vertex.dxil
out/build/windows-vs2026/generated/shaders/Debug/terrain.pixel.dxil
out/build/windows-vs2026/generated/shaders/Debug/material_sphere.vertex.dxil
out/build/windows-vs2026/generated/shaders/Debug/material_sphere.pixel.dxil
out/build/windows-vs2026/generated/shaders/Debug/tone_map.vertex.dxil
out/build/windows-vs2026/generated/shaders/Debug/tone_map.pixel.dxil
out/build/windows-vs2026/generated/shaders/Release/cube.vertex.dxil
out/build/windows-vs2026/generated/shaders/Release/cube.pixel.dxil
out/build/windows-vs2026/generated/shaders/Release/skybox.vertex.dxil
out/build/windows-vs2026/generated/shaders/Release/skybox.pixel.dxil
out/build/windows-vs2026/generated/shaders/Release/terrain.vertex.dxil
out/build/windows-vs2026/generated/shaders/Release/terrain.pixel.dxil
out/build/windows-vs2026/generated/shaders/Release/material_sphere.vertex.dxil
out/build/windows-vs2026/generated/shaders/Release/material_sphere.pixel.dxil
out/build/windows-vs2026/generated/shaders/Release/tone_map.vertex.dxil
out/build/windows-vs2026/generated/shaders/Release/tone_map.pixel.dxil
```

vcpkg deploys the spdlog/fmt, DirectXTex, and WinPixEventRuntime DLLs beside
executables that need them. Shark's post-build rules place the pinned Agility
Core and SDK Layers under `D3D12/`, the pinned development-only WARP DLL beside
`SharkSandbox.exe`, and the tracked orientation DDS under `content/sky/`. All
generated binaries stay under ignored `out/`. The WARP NuGet binary is for
local development and testing only and must never enter a packaged product.

With no arguments, `SharkSandbox` initializes the highest-priority eligible
hardware device, creates the validated canonical `HeightTileSurface`, and
continuously draws its deterministic terrain, query-derived cyan normal pin,
material sphere, procedural-checker cube, and HDR environment through named
`Terrain`, `TexturedCube`, `Skybox`, and `ToneMap` graph passes. The first
three render into a resize-owned `R16G16B16A16_FLOAT` scene target; `ToneMap`
writes the final back buffer. The triple frame-resource lifecycle reports
separate PIX/timestamp intervals for all four passes. REN-001 routes this through the
public `shark::renderer::Renderer`; the sandbox creates the D3D12 `Device` and
passes it to `Renderer::create` only at the composition root. Press `F1` to
toggle terrain fill between solid and wireframe. Press `F2` to cycle shaded,
ground/rock weight, and mapped world-normal views. Press `F3` to toggle between
HDR image-based lighting and the retained procedural-daylight fallback. Use
`W`/`S` along the
camera forward axis, `A`/`D` to strafe, `Q`/`E` to move down/up, hold `Shift`
to move faster, and hold the right mouse button while dragging to look around.
`Control` and `Space` are down/up aliases. Resize or minimize/restore the
window to exercise the projection, swap-chain, depth, frame-local graph
imports, and fence-delayed timing reuse, then close the title bar or press
Alt+F4 to exit cleanly.

## Shader build contract

Configuration resolves only the `dxc.exe` restored by the active vcpkg host
triplet and verifies retail version `1.9.2602.24`. A global DXC on `PATH` is
neither required nor accepted. The compiler's sidecar `dxcompiler.dll` and
`dxil.dll` remain beside that host tool; Shark does not link them or copy them
beside `SharkSandbox.exe`.

The build compiles the cube, skybox, terrain, material-sphere, and tone-map
`VSMain` entry points as `vs_6_0` and their `PSMain` entry points as `ps_6_0`,
using HLSL 2021, row-major layout, strict mode, and warnings as errors. DXIL,
generated C++ byte arrays, PDBs, and dependency files are
configuration-specific and stay
under ignored `out/build/windows-vs2026/generated/shaders/`. The dependency
files make edits to the shared camera and PBR/IBL includes rebuild every
dependent stage. The retained daylight include owns the procedural sky and
directional illumination; final linear-to-sRGB conversion now belongs only to
the tone-map pass.

CTest adds three build checks: the shader depfiles must name the primary source
and shared include, a build-tree include edit must actually regenerate the
compiled shader, malformed HLSL must fail under the pinned compiler, and an
HLSL warning must fail because `-WX` is active. The expected-failure checks also
verify that the failure is the intended compiler diagnostic rather than a
missing tool or broken build target.

Run only the normal shader target and focused build checks with:

```powershell
& $cmake --build --preset windows-debug --target SharkCubeShaders SharkSkyboxShaders SharkTerrainShaders SharkMaterialSphereShaders SharkToneMapShaders
& $ctest --preset windows-debug -R '^build\.shader_'
```

For the visual acceptance check, run `SharkSandbox` without arguments. A solid
height tile must show tiled ground and rock materials blended by slope and
height, mapped surface detail, and direct-sun plus environment response. The
glossy neutral material sphere must reflect the same environment used by the
terrain. The initial pose must show 16 magenta depth-tested chunk AABBs and log
`Terrain chunks: 16 / 16 visible`. Surface chunks and matching bounds must
disappear together when the camera turns away; the deterministic smoke pose
must reach `5 / 16`. The cyan query pin must begin on the visible LOD0 surface
and extend along its exact triangle normal. `F1` must reveal the fixed triangle
split in wireframe without disconnecting or hiding the pin. `F2` must cycle
shaded, complementary material-weight, and mapped world-normal views without
changing the canonical geometry.
The textured cube must retain correct hidden-surface occlusion and perspective.
The background must form one continuous HDR daylight environment without
visible cube-face seams or painted-wall bands. Translation must not move the
sky, right-drag rotation must move the fixed-world environment consistently,
and resizing from a wide to a non-wide aspect must not stretch the scene.
The analytic sun disk must remain smooth rather than appearing as a bright
cubemap texel block, and terrain/sphere must not show duplicated sun energy.
Pressing `F3` must visibly switch both the sky and scene lighting between HDR
IBL and procedural daylight without changing geometry or camera state. The
exact environment contract is documented in
[the skybox contract](SKYBOX.md).

## Graphics device checks

Run the no-window hardware and pinned-WARP device checks directly with:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --gpu-smoke
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --gpu-smoke --warp
```

Automatic selection uses DXGI high-performance order, requires Feature Level
12_0 and Shader Model 6.0, and never falls back to WARP. The startup log assigns
each enumerated candidate a session-local index. Use one of those exact indices
or request a full no-window report with:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --capabilities
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --capabilities --adapter 1
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --capabilities --warp
```

An explicit adapter is never replaced silently if it is absent or below the
baseline. Candidate indices can change after a driver, hardware, or operating
system change; use the logged name, LUID, vendor ID, and device ID when
recording a test result.

The D3D12 debug layer and DRED are enabled before any device probe. GPU-based
validation is deliberately opt-in because it is expensive. The most useful
focused command includes real submission and presentation:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp --gpu-validation
```

This focused path completes 120 successful presents, with resize at frame 30
and scripted yaw at frame 90. It intentionally skips the minimize/restore
interval already covered by the normal paths. Its 180-second internal deadline
is bounded by a 240-second CTest timeout. The normal hardware and WARP
presentation paths below retain the 1,000-frame gate and checkpoints at frames
250, 500, and 750.

Every device path fails if either the D3D12 or DXGI initialization info queue
contains an error or corruption message. See
[the graphics-device contract](GRAPHICS_DEVICE.md) for selection, capability,
ownership, and runtime rules.

## Presentation checks

Run the fixed terrain/cube/sky presentation contract on hardware and packaged
WARP with:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp
```

Each command shows a real PMv2-aware window, presents exactly 1,000 successful
four-pass frames, changes the physical client area from `1280x720` to
`960x600`, proves the projection and `D32_FLOAT` depth extent follow the
aspect-changing resize, proves no frame is submitted while minimized, restores,
shuts down `Renderer` before the window, and checks new
D3D12/DXGI messages plus live D3D12 device children. Submission or presentation
removal failures also emit bounded DRED diagnostics.

The run also exercises three contexts selected by DXGI's back-buffer index.
Each attempt writes one 256-byte record containing the scene and sky matrices
at bytes `0..127`, six float4-compatible daylight rows at bytes `128..223`, and
the existing GPU probe beginning at byte `224`. It binds the record through a
pixel-visible root CBV, copies the probe to its default-heap destination, and
stages one descriptor in a CPU-only heap. A context waits only when its own
allocator cannot yet be reused because its preceding submission fence has not
completed; that checkpoint protects all frame constants and the probe
destination.

Creation records one `StaticSceneUpload` PIX event, one static direct-queue
upload submission, and one bounded wait for the cube and packed shared
terrain/chunk-bounds/marker/sphere vertex/index buffers, deterministic `8x8`
checker, all six faces of the app-local `8x8` sRGB DDS cubemap, the three
two-layer `32x32` full-mip terrain
material arrays, and four derived HDR environment textures. The deterministic
`64x32` linear-HDR source is CPU-only. Its GPU derivatives are a `32x32`
six-mip radiance cube, `8x8` one-mip diffuse-irradiance cube, `32x32` six-mip
GGX-prefiltered specular cube, and `32x32` split-sum BRDF LUT: 79 subresources
and 284,608 meaningful RGBA32-float bytes. The 16 row-major chunks use the
same 1,089 terrain vertices and 16 contiguous 384-index surface ranges. Their
128 bounds vertices and 384 bounds indices, the query marker, and the
266-vertex/1,584-index material sphere remain packed into the same terrain
buffers, so startup still creates four geometry buffers. The retained DDS
cubemap remains a separate asset/upload proof. The startup list ends with 13
initialization barriers.

Every submitted frame records one outer `Frame` event with nested `Terrain`,
`TexturedCube`, `Skybox`, and `ToneMap` events. Its exact graph has 15 imports,
four passes, three dependencies, six recorded transitions, and 31 elided
transitions. With `V` visible chunks it issues `2V + 4` indexed draws: `V`
384-index terrain surfaces, the material sphere, `V` matching 24-index chunk
bounds, the terrain query marker, 36-index textured cube, and 36-index skybox.
One non-indexed fullscreen-triangle tone-map draw follows. The fixed smoke
poses therefore issue 36 indexed draws at `V=16` and 14 at `V=5`. The frame
retains four texture-table bindings and one reversed-Z depth clear. Terrain
owns the clear, cube preserves it, sky uses the read-only DSV, and `ToneMap`
reads the HDR scene target while writing the swap-chain back buffer.

The direct queue reports its timestamp frequency once. One 30-entry query heap
and one 240-byte persistently mapped readback buffer are split into three
ten-query frame-context slices: frame begin, terrain begin/end, cube
begin/end, sky begin/end, tone-map begin/end, and frame end. The frame interval
includes the diagnostic probe copy, six graph barriers, and all four passes but
excludes its own query resolve. Each pass interval
covers only its callback commands and excludes graph barriers. Results are read
only after the owning context fence completes, using an existing reuse wait or
resize/shutdown drain; timing adds no normal-frame drain.

The summary reports graph pass/barrier counts, PIX event counts, query
high-water/capacity, timing sample count, average/maximum frame,
`Terrain`, `TexturedCube`, `Skybox`, and `ToneMap` milliseconds, terrain
solid/wireframe/bounds/query-marker counts, chunk last/min/max/total and
tested/visible/culled counts, all draw/index counts,
scene/sky matrix changes, `depth_clear_count`, depth-resource/read-only-view
creation counts, other resource creation counts,
context reuse, blocking reuse waits, queue drains, and upload/descriptor
high-water marks. The fixed diagnostics invariants require ten queries and one
resolve per frame, one completed timing sample per retired submission, and a
ten-query per-context high-water. Duration magnitude is deliberately not a
performance gate because adapter speed and timestamp resolution vary.

Run the focused renderer composer, planner, D3D12 executor, scene helpers, and
GPU timestamp-state unit coverage directly with:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[renderer]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[render-graph]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[timestamps]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[skybox]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[daylight]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][query]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][height-tile][chunks]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[renderer][terrain][culling]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][material]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[assets][dds][cubemap]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[assets][environment][ibl]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[environment][sphere]"
```

CTest registers hardware and WARP device and presentation paths as separate
serial processes, plus a focused packaged-WARP GPU-validation presentation
path. That focused case uses the 120-frame/180-second contract and a 240-second
CTest timeout. The compatibility presentation case names remain:
`integration.gpu.hardware_cube_present`,
`integration.gpu.warp_cube_present`, and
`integration.gpu.warp_cube_present_validation`. To run all graphics integration
checks for either configuration:

```powershell
& $ctest --preset windows-debug -R '^integration\.gpu\.'
& $ctest --preset windows-release -R '^integration\.gpu\.'
```

For a noninteractive lifecycle check, run:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --platform-smoke
```

The platform smoke mode does not initialize Direct3D. It shows a real
nonactivating window, performs a native resize, minimize, and restore, waits
idle, then wakes through an asynchronously posted close request and confirms
destruction. CTest registers this mode separately with a ten-second timeout,
so the normal interactive executable is never allowed to block an automated
test. See [the platform contract](PLATFORM.md) for the event and ownership
rules.

See the [presentation and frame-resource contract](GRAPHICS_PRESENTATION.md)
for context reuse, bounded staging, fence retirement, depth ownership, and
resize behavior. See the [HLSL pipeline contract](GRAPHICS_PIPELINE.md) for
pinned compilation, generated artifacts, root-signature/PSO state, and indexed
draw behavior. See the
[camera and textured-cube contract](CAMERA_AND_CUBE.md) for controls,
coordinate conventions, reversed-Z, geometry, texture, and explicit limits.
See [the minimal render-graph contract](RENDER_GRAPH.md) for graph declaration,
ordering, validation, barrier mapping, accounting, and non-goals.
See [the GPU diagnostics contract](GPU_DIAGNOSTICS.md) for PIX names,
timestamp boundaries, fence-delayed readback, smoke accounting, and manual
capture acceptance. See [the DDS cubemap contract](DDS_CUBEMAP.md) for the
tracked fixture, strict loader, app-local deployment, retained startup upload,
and deliberately absent per-frame sky read. See
[the terrain contract](TERRAIN.md) for canonical ownership, exact query
semantics, full-resolution chunks, frustum culling, material fixture,
render-mesh separation, and diagnostic rules. `T-004` completed terrain chunk
culling on July 19, 2026. The upcoming increment is `T-005`, one bounded
coarser terrain LOD with crack-free seams and full-resolution canonical
queries.

## Visual Studio

Visual Studio 2026 recognizes `CMakePresets.json` when the repository folder is
opened. Select `Windows x64 - Visual Studio 2026`, allow dependency restoration
to finish, and build either Debug or Release. The generated solution under
`out/build/windows-vs2026` is disposable and must not be committed.

## Clean rebuild

All generated files, restored dependencies, and test output live under `out/`.
To reproduce a completely clean build, close Visual Studio, remove `out`, and
repeat the fresh command-line sequence. Never stage `out/`, package caches,
downloaded tools, or generated Visual Studio projects.
