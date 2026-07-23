# Building Shark

- **Completed through:** `PHY-009`
- **Last verified:** July 22, 2026

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
continuously draws its deterministic terrain with bounded distance-selected
LOD, four material spheres, procedural-checker cube, and HDR environment through
named
`Terrain`, `TexturedCube`, `Skybox`, `Water`, and `ToneMap` graph passes. The
first four render into a resize-owned `R16G16B16A16_FLOAT` scene target;
`ToneMap` writes the final back buffer. The triple frame-resource lifecycle
reports separate PIX/timestamp intervals for all five passes. REN-001 routes this through the
public `shark::renderer::Renderer`; the sandbox creates the D3D12 `Device` and
passes it to `Renderer::create` only at the composition root. Press `F1` to
toggle terrain fill between solid and wireframe. Press `F2` to cycle shaded,
ground/rock weight, and mapped world-normal views. Press `F3` to toggle between
HDR image-based lighting and the retained procedural-daylight fallback. Press
`F4` to toggle primary body 0's canonical support-normal preview and magenta
chunk bounds, which are off by default. The preview is available while the
sphere is airborne; it is not an active-contact indicator. All four spheres
start paused at their scenario-owned spawns. Press `F6` to advance all of them
by exactly one 60 Hz
simulation tick while paused, or press `F5` to resume/pause continuous
fixed-step motion. Bodies 1 and 2 collide while airborne, while primary body 0
settles on canonical LOD0 terrain. Isolated body 3 receives a constant torque;
its small brown local-axis cap makes the normalized rotation visible. Use
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

The build compiles the cube, skybox, terrain, water, material-sphere, and tone-map
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
& $cmake --build --preset windows-debug --target SharkCubeShaders SharkSkyboxShaders SharkTerrainShaders SharkWaterShaders SharkMaterialSphereShaders SharkToneMapShaders
& $ctest --preset windows-debug -R '^build\.shader_'
```

For the visual acceptance check, run `SharkSandbox` without arguments. A solid
height tile must show tiled ground and rock materials blended by slope and
height, mapped surface detail, and direct-sun plus environment response. The
four glossy neutral material spheres must reflect the same environment used by
the terrain. The interactive camera starts at scenario-owned eye
`(-128,3.34375,-20)` with pitch `-0.1`, overlooking a smoothly irregular
approximately `112x96`-meter lake. The surface must stay bounded to the basin,
meet the terrain naturally at its depth-tested shoreline, transmit and tint
the underlying terrain, reflect the active environment more strongly at
grazing angles, and show subtle animated normal waves and sun glint. It
remains a visual surface with no fluid behavior.
Diagnostics start off; press `F4` and confirm the magenta depth-tested chunk
AABBs match the currently logged visible count and that the cyan query pin
appears. Surface chunks and matching bounds must disappear together when the
camera turns away.
With the simulation initially paused, confirm all four spheres are visible.
After `F5`, bodies 1 and 2 must collide and separate while airborne; primary
body 0 must fall to its cyan support site and remain on canonical terrain
without hover or penetration. Body 3's local brown cap must accelerate around
the sphere under constant torque, including after linear terrain support.

The separate deterministic `--present-smoke` path must start at
`93 / 225 visible (LOD0=0, coarse=93)`, retain that split after resize, reach
`72 / 225 visible (LOD0=0, coarse=72)` after its yaw, then
finish from the smoke-only `(16, -1, 0)` pose with the same yaw/pitch at
`61 / 225 visible (LOD0=1, coarse=60)`. Those smoke-only poses do not replace
the scenario-owned interactive spawn. The broad landscape must appear natural, mostly flat, and
nonperiodic without spikes or abrupt boundary artifacts. The dry basin must
read as a smooth irregular depression rather than a perfect circle, sharp
crater, repeated stamp, or stepped terrace. In `F1` wireframe, shared chunk edges must
remain connected without cracks, skirts, or missing corners. The cyan query pin
must begin on the canonical LOD0 surface
and extend along its exact triangle normal. `F1` must keep the enabled pin
connected and visible. `F2` must cycle
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

This focused path completes 120 successful presents, with resize at frame 30,
scripted yaw at frame 90, and the smoke-only near translation at frame 105. It
intentionally skips the minimize/restore interval already covered by the normal
paths. Its 180-second internal deadline is bounded by a 240-second CTest
timeout. Hardware uses resize/minimize-or-restore/yaw/near checkpoints at
frames 250/500/750/875 in its 1,000-frame gate; normal WARP uses
150/300/450/525 in its 600-frame gate.

Every device path fails if either the D3D12 or DXGI initialization info queue
contains an error or corruption message. See
[the graphics-device contract](GRAPHICS_DEVICE.md) for selection, capability,
ownership, and runtime rules.

## Presentation checks

Run the fixed terrain/cube/sky/water presentation contract on hardware and
packaged WARP with:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp
```

The hardware command presents exactly 1,000 successful five-pass frames. The
normal packaged-WARP command presents 600; the focused packaged-WARP
GPU-validation command below presents 120:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp --gpu-validation
```

Hardware and normal WARP change the physical client area from `1280x720` to
`960x600`. Only the focused GPU-validation path uses `640x360 -> 480x300` to
keep its 120-frame run bounded; both sequences preserve the same
`16:9 -> 1.6` aspect change. Each command shows a real PMv2-aware window and
proves the projection and `D32_FLOAT` depth extent follow the
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
terrain-LOD/chunk-bounds/marker/sphere vertex/index buffers, deterministic `8x8`
checker, all six faces of the app-local `8x8` sRGB DDS cubemap, the three
two-layer `32x32` full-mip terrain
material arrays, and four derived HDR environment textures. The deterministic
`64x32` linear-HDR source is CPU-only. Its GPU derivatives are a `32x32`
six-mip radiance cube, `8x8` one-mip diffuse-irradiance cube, `32x32` six-mip
GGX-prefiltered specular cube, and `32x32` split-sum BRDF LUT: 79 subresources
and 284,608 meaningful RGBA32-float bytes. T-008 retains T-006's capacity
topology:
`241x241` samples/`240x240` cells at four-meter spacing over `960x960` meters,
58,081 shared vertices, and 225
row-major `16x16`-cell chunks. Its contiguous ranges contain 115,200 LOD0
triangles/345,600 indices (1,536 per chunk) followed by 64,800
boundary-preserving coarse triangles/194,400 indices (864 per chunk), with a
measured active `0.603515625`-meter maximum coarse error. The 540,000 surface indices are
followed by 1,800
bounds vertices/5,400 bounds indices, the query marker, and the
266-vertex/1,584-index material sphere in the same terrain buffers, so startup
still creates four geometry buffers. Packed index offsets are 0 for LOD0,
345,600 for coarse, 540,000 for bounds, 545,400 for the marker, and 545,406 for
the sphere; the buffer contains 546,990 indices. The retained DDS
cubemap remains a separate asset/upload proof. The startup list ends with 13
initialization barriers.

The historical T-007 height generator uses seed `0x4FFB0830`, five Q23/Q30 fixed-point
value-noise bands, and Q8 output. Its row-major float-byte FNV-1a checksum is
`0xC0FB1097EBCB8B7B`; relief is 25.82421875 meters, all 115,200 triangles are
at or below 12 degrees, and the maximum slope is 11.251308698 degrees. T-008
keeps that generator as a base oracle and applies a bounded Q8 basin
post-process. The active composite checksum is `0x4890DE3E1AA063A9`, maximum
slope is `18.681598` degrees, and maximum adjacent X/Z steps are
`1.16015625/1.33203125` meters. Its validated spawn-side support component is centered at
`(-128,-128)`, spans approximately `112x96` meters, and publishes the
`-4`-meter waterline consumed by W-001 plus dry spawn ground
`(-128,1.34375,-20)`.

The meaningful terrain surface payload is 1,393,944 vertex bytes plus 1,080,000
index bytes, or 2,473,944 bytes. Bounds and marker diagnostics add 54,156
bytes; the packed sphere adds another 9,552. The vertex/index resource widths are
1,443,672/1,093,980 bytes (2,537,652 logical bytes), and their committed D3D12
allocation is 2,621,440 bytes. The logged CPU-build boundary covers fixture
construction through chunk/LOD generation and query proof. T-006 historically
measured 6,049.240 ms in Debug and 82.738 ms in Release. T-007 measured
8,098.750 ms on Debug hardware, 87.203 ms on Release hardware, and 7,699.463 ms
on Debug WARP; focused WARP+GBV measured 6,008.669 ms. These observations are
not cross-machine performance gates. No separate T-008 construction time is
promoted as an acceptance threshold; the active suite and presentation-gate
timings are recorded below.

Every submitted frame records one outer `Frame` event with nested `Terrain`,
`TexturedCube`, `Skybox`, `Water`, and `ToneMap` events. Its exact graph has
15 imports, five passes, five dependencies, six recorded transitions, and 34 elided
transitions. With `V0` visible LOD0 chunks, `Vc` visible coarse chunks, and
`V=V0+Vc`, it issues `V0` 1,536-index terrain surfaces, `Vc` 864-index terrain
surfaces, four draws of the 1,584-index material sphere, one 36-index textured
cube, one 36-index skybox, and one non-indexed six-vertex water quad.
`F4` additionally enables `V` matching 24-index chunk bounds and the six-index
query marker; those diagnostics are off by default. One non-indexed
fullscreen-triangle tone-map draw follows. The initial and resized smoke poses
show 93 chunks at `V0/Vc=0/93`; the turned overview shows 72 at `0/72` from
three quarters through seven eighths; and the final eighth moves only the smoke
camera to `(16, -1, 0)` with the same yaw/pitch, exposing 61 chunks at `1/60`.
Those smoke-only poses do not replace T-008's scenario-owned interactive
spawn. The frame retains five
texture-table bindings and one reversed-Z depth clear. Terrain owns the clear,
cube preserves it, sky first uses the read-only DSV, water then composites
transparently with the same view, and `ToneMap` reads the HDR scene target while
writing the swap-chain back buffer.

The direct queue reports its timestamp frequency once. One 36-entry query heap
and one 288-byte persistently mapped readback buffer are split into three
12-query frame-context slices: frame begin, terrain begin/end, cube
begin/end, sky begin/end, water begin/end, tone-map begin/end, and frame end.
The frame interval
includes the diagnostic probe copy, six graph barriers, and all five passes but
excludes its own query resolve. Each pass interval
covers only its callback commands and excludes graph barriers. Results are read
only after the owning context fence completes, using an existing reuse wait or
resize/shutdown drain; timing adds no normal-frame drain.

The summary reports graph pass/barrier counts, PIX event counts, query
high-water/capacity, timing sample count, average/maximum frame,
`Terrain`, `TexturedCube`, `Skybox`, `Water`, and `ToneMap` milliseconds, terrain
solid/wireframe/bounds/query-marker counts, chunk last/min/max/total,
LOD0/coarse last/draw/index counts, measured maximum geometric error,
tested/visible/culled counts, all draw/index counts,
scene/sky matrix changes, `depth_clear_count`, depth-resource/read-only-view
creation counts, other resource creation counts,
context reuse, blocking reuse waits, queue drains, and upload/descriptor
high-water marks. The fixed diagnostics invariants require 12 queries and one
resolve per frame, one completed timing sample per retired submission, and a
12-query per-context high-water. Duration magnitude is deliberately not a
performance gate because adapter speed and timestamp resolution vary.

Historical T-006 presentation evidence is:

- Debug and Release hardware, 1,000 frames: 225,000/87,500/137,500 chunks
  tested/visible/culled, 3,250/84,250 LOD0/coarse draws, and
  4,992,000/72,792,000 LOD0/coarse indices;
- Debug packaged WARP, 600 frames: 135,000/52,500/82,500 chunks,
  1,950/50,550 draws, and 2,995,200/43,675,200 indices; and
- Debug packaged WARP with GPU validation, 120 frames:
  27,000/10,500/16,500 chunks, 390/10,110 draws, and
  599,040/8,735,040 indices.

Each path exercised a 30-frame diagnostic interval with 2,790 bounds draws and
30 query-marker draws, then finished with zero Direct3D errors and zero live
child objects. Debug and Release builds passed, and each configuration passed
`135/135` unit CTests. The full Debug run covered 302,707 assertions across
those 135 cases. Build/shader
labels passed `3/3` and the platform/device integration subset passed `3/3` in
each configuration. The latest focused capacity contract measured 5.98 seconds
in Debug and 0.09 seconds in Release; the large-fixture LOD smoke measured 5.91
and 0.09 seconds.

Historical T-007 deterministic acceptance totals are:

- hardware, 1,000 frames: 225,000/86,375/138,625 chunks
  tested/visible/culled, 125/86,250 LOD0/coarse draws,
  192,000/74,520,000 LOD0/coarse indices (74,712,000 total),
  46,500/39,875 solid/wireframe draws, and
  30,969/30,969/24,437 shaded/weights/normals draws;
- packaged WARP, 600 frames: 135,000/51,825/83,175 chunks,
  75/51,750 draws, 115,200/44,712,000 indices (44,827,200 total),
  27,900/23,925 solid/wireframe draws, and
  18,600/18,600/14,625 material-view draws; and
- packaged WARP with GPU validation, 120 frames:
  27,000/10,365/16,635 chunks, 15/10,350 draws,
  23,040/8,942,400 indices (8,965,440 total),
  5,580/4,785 solid/wireframe draws, and
  3,720/3,720/2,925 material-view draws.

For `Q=F/8`, these follow `Q * (2*A + 4*B + C + D)` with
`A/B/C/D=93/93/72/61`; only D contributes the `Q` LOD0 draws. Every gate ends
with visibility last/min/max `61/61/93`, final LOD split `1/60`, and exact
scene/sky matrix-change counts `4/3`.

Each path requires 2,790 bounds draws, 66,960 bounds indices, 30 marker draws,
and 180 marker indices. T-007 Debug/Release hardware, normal WARP, and focused
GBV passed these totals with zero corruption/errors and zero live child objects.
Its final Debug and Release `SharkTests` runs each passed 135 cases and 303,048
assertions, including direct relief, near-camera clearance, and smoke-pose
checks; Debug `ctest -L unit` also passed `135/135`.

T-008 retains those exact four smoke poses, visibility/LOD splits, resource
counts, and expected aggregate totals. Focused Debug results pass
56,792 lake-basin assertions across three cases, 4,732 scenario assertions across
three cases, 23 culling assertions across two cases, and 367 LOD assertions
across two cases. Active T-008 validation passed the Debug build and all
`150/150` tests in 195.60 seconds; hardware, WARP, and WARP+GBV presentation
took 8.61, 83.60, and 66.85 seconds. The Release build and all `150/150` tests
passed in 157.45 seconds; the same gates took 2.06, 78.66, and 60.14 seconds.
Both configurations retained exact smoke accounting.

The separate final Debug RTX 4070 hardware smoke submitted 1,000 frames with
86,375 visible and 138,625 culled chunks, 125/86,250 LOD0/coarse draws, and
192,000/74,520,000 LOD0/coarse indices. It retained the 0.603516-meter logged
coarse error, measured GPU frame average/maximum 0.125/0.681 ms and Terrain
average/maximum 0.105/0.285 ms, and reported zero corruption, zero errors, zero
live child objects, and only the two expected `ReportLiveDeviceObjects`
warnings.

The earlier all-coarse focused WARP+GBV attempt retained the original full resolution and
reached 83 clean frames before its existing 180-second deadline. It exposed a
focused-test cost, not a product failure. Its predecessor 120-frame rerun at the
bounded focused resolution passed in about 69 seconds. The subsequent
four-phase T-007 focused rerun also passed. Shutdown retained the usual two device-only RLDO
warnings; they were not renderer-owned live-child failures.

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
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][height-tile][lod]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][height-tile][capacity]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][lake-basin]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[world][scenario][environment-lab]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[renderer][terrain][culling]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[renderer][terrain][lod]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[renderer][terrain][lod][smoke]"
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
semantics, full-resolution chunks, bounded visual LOD, frustum culling,
material fixture, render-mesh separation, and diagnostic rules. `T-007`
completed the fixed-seed, Q8 natural rolling-height contract on July 19, 2026
while retaining T-006's topology, resource budgets, canonical queries, and
global `R16_UINT` indices. Its historical construction timings and graphics
evidence are recorded above.
See [the fixed-step simulation contract](SIMULATION.md) for the 60 Hz clock,
pause/single-step controls, semi-implicit linear/angular state, normalized
orientation interpolation, canonical one-sample sphere support, and the fixed
four-body deterministic collision pass, plus PHY-005's pure capsule
closest-feature contacts and PHY-006's pure oriented-box SAT manifolds.

W-001 consumes that scenario-owned waterline in a dedicated transparent pass
after `Skybox`. The vertex shader expands a six-vertex quad from `SV_VertexID`
over the local domain centered at `(-128,-4,-128)` with X/Z half-extents
`64/56`. The pixel shader intersects that domain with warped `rho <= 1`, thereby
selecting the intended spawn-side component; canonical reversed-Z depth testing
forms the visible shoreline. No water buffer, texture, descriptor, or static
upload was added, so the four geometry buffers and ten persistent descriptors
remain fixed.

For renderer increments, iterate with affected targets and focused tests,
then run the full unit suite once after the implementation stabilizes. Use
Debug hardware/WARP/GBV presentation gates plus a Release hardware smoke for
ordinary pass/shader work; reserve the full Debug/Release graphics matrix for
RHI, synchronization, lifetime, or milestone changes.

PHY-004 leaves the W-001 lake presentation-only and adds no water resource,
fluid state, or coupling. Four existing sphere draws reuse one geometry range,
PSO, and `b2` binding; only the bounded root constants and sphere vertex shader
interpretation change. Render-pass, descriptor, geometry-buffer, and per-frame
upload budgets remain fixed. Rain remains deferred; the active increment queue
is maintained in [ENGINE_PLAN.md](ENGINE_PLAN.md).

PHY-005 is CPU-only. It adds a shared quaternion-vector rotation, finite
capsule collider, bounded canonical terrain segment query, and pure contact
generation against terrain, spheres, and capsules. It does not change the
sandbox executable's body count, renderer inputs, shaders, root signature,
draws, resources, descriptors, passes, uploads, or smoke accounting. Iterate
with the `[terrain][segment]` and `[physics][capsule]` test tags before the full
unit presets; one Debug hardware presentation smoke is the proportionate
graphics regression gate for this non-rendering increment.

At PHY-005 completion, the capsule suite passes `3,242` assertions across 11
cases and the terrain segment suite passes `442` assertions across seven cases
in both Debug and Release. Both full unit presets pass `202/202`. The unchanged
Debug hardware presentation smoke passes 1,000 frames with zero D3D12
corruption/errors or live child objects.

PHY-006 is also CPU-only. It adds checked oriented-box world geometry,
deterministic box/box and box/canonical-terrain SAT, fixed-capacity contact
manifolds with at most four points, and Terrain's inclusive bounded candidate
query for exact triangles. It does not change the sandbox body count, renderer
inputs, shaders, root signature, draws, resources, descriptors, passes,
uploads, or smoke accounting.
Iterate with the `[physics][box]` and `[terrain][triangle-bounds]` filters before
the full unit presets; one Debug hardware presentation smoke remains the
proportionate graphics regression gate.

At PHY-006 completion, the box suite passes `4,282` assertions across 15 cases
and the terrain triangle-bounds suite passes `351` assertions across seven cases
in both Debug and Release. Both complete suites pass `393,840` assertions
across `224/224` cases. The unchanged Debug hardware presentation smoke passes
1,000 frames, records 4,000 existing sphere draws, and reports zero D3D12
corruption/errors or live child objects.

PHY-007 adds the bounded shared contact solver and migrates the existing
sphere/terrain and sphere-pair adapters. It changes CPU simulation response and
sandbox smoke assertions, but adds no body, shader, root-signature value, draw,
resource, descriptor, pass, or upload. Iterate with the
`[physics][contact-constraint]`, `[physics][sphere][terrain]`, and
`[physics][sphere][body-collision]` filters before running both complete unit
presets; one Debug hardware presentation smoke remains the proportionate
graphics regression gate.

At PHY-007 completion, those three focused suites pass `389/12`, `7,423/9`, and
`3,696/12` assertions/cases respectively in Debug and Release. The complete
Physics label passes `21,215` assertions across 71 cases, and both complete unit
configurations pass `394,277` assertions across `236/236` cases. Strict Debug
and Release sandbox builds pass. The Debug hardware smoke passes 1,000 frames,
records the unchanged 4,000 existing sphere draws and GPU accounting, and
reports zero D3D12 corruption/errors or live child objects. That result remains
the cold-solver baseline for PHY-008.

PHY-008 is CPU-only at runtime. It adds the bounded contact cache/warm-start
transaction, checked generic diagonal and solid-box inertia, and a permanent
three-cube stability scenario assembled from the existing pure box contacts.
It adds no sandbox body, shader, root-signature value, draw, resource,
descriptor, pass, or upload. Iterate with the `[physics][persistent-contact]`,
`[physics][box][dynamics]`, and `[physics][rigid-body]` filters before running
both complete unit configurations; one Debug hardware presentation smoke is
the proportionate graphics regression gate.

At PHY-008 completion, the persistent-contact suite passes `55,841` assertions
across 13 cases and the box-dynamics suite passes `55,637` across six cases in
both Debug and Release. The complete Physics selection passes `89,119`
assertions across 90 cases; both complete unit configurations pass `462,181`
assertions across `255/255` cases. Strict Debug and Release builds pass. The
unchanged Debug hardware presentation smoke passes 1,000 frames, records the
existing 4,000 sphere draws, and reports zero D3D12 corruption/errors or live
child objects.

PHY-009 adds the allocation-free, shape-neutral fixed-X sweep-and-prune path.
Its complete fixed capacities are 64 proxies and 2,016 candidates. The existing
four-sphere adapter now submits stable slot IDs and conservative outward-rounded
closed AABBs, then applies its unchanged exact narrow phase and shared solver in
canonical order. Iterate with `[physics][broad-phase]` and
`[physics][sphere][body-collision]` before both complete unit presets. Physics
reports deterministic proxy/possible/X-overlap/candidate/narrow/contact work
counts; it intentionally contains no wall-clock timer, so use an external CPU
profiler when timing is required.

The permanent broad-phase suite covers brute-force-oracle equivalence, full
capacity, seeded/permuted fixtures, touching and axis ties, duplicate/reused IDs,
invalid bounds, transactional failure, moving generations, and fixed-rate
invariance. Both Debug and Release complete test runs pass `477,236` assertions
across `267/267` cases. The 1,000-frame RTX 4070 Laptop GPU presentation smoke
records exact structural totals `4000/6000/255/3/3/2` for proxies/possible/
X-overlaps/candidates/narrow/contacts, retains the existing 4,000 sphere draws,
and reports zero D3D12 corruption/errors and zero live child objects. The next
increment is `PHY-010`, body islands and sleeping.

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
