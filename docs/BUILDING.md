# Building Shark

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
boundary.

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
out/build/windows-vs2026/generated/shaders/Release/cube.vertex.dxil
out/build/windows-vs2026/generated/shaders/Release/cube.pixel.dxil
out/build/windows-vs2026/generated/shaders/Release/skybox.vertex.dxil
out/build/windows-vs2026/generated/shaders/Release/skybox.pixel.dxil
```

vcpkg deploys the spdlog/fmt, DirectXTex, and WinPixEventRuntime DLLs beside
executables that need them. Shark's post-build rules place the pinned Agility
Core and SDK Layers under `D3D12/`, the pinned development-only WARP DLL beside
`SharkSandbox.exe`, and the tracked orientation DDS under `content/sky/`. All
generated binaries stay under ignored `out/`. The WARP NuGet binary is for
local development and testing only and must never enter a packaged product.

With no arguments, `SharkSandbox` initializes the highest-priority eligible
hardware device, opens the native Win32 window, and continuously draws the
procedural-checker cube followed by the static DDS skybox as named
`TexturedCube` and `Skybox` graph passes. S-002 keeps the triple frame-resource
lifecycle and resize-safe reversed-Z target, and reports separate PIX/timestamp
intervals for both passes. Use `W`/`S` along the camera
forward axis, `A`/`D` to strafe, `Q`/`E` to move down/up, hold `Shift` to move
faster, and hold the right mouse button while dragging to look around.
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

The build compiles the cube and skybox `VSMain` entry points as `vs_6_0` and
their `PSMain` entry points as `ps_6_0`, using HLSL 2021, row-major layout,
strict mode, and warnings as errors. DXIL, generated C++
byte arrays, PDBs, and dependency files are configuration-specific and stay
under ignored `out/build/windows-vs2026/generated/shaders/`. The dependency
files make edits to the shared camera include rebuild every dependent cube and
sky stage.

CTest adds three build checks: both shader depfiles must name the primary source
and shared include, a build-tree include edit must actually regenerate the
compiled shader, malformed HLSL must fail under the pinned compiler, and an
HLSL warning must fail because `-WX` is active. The expected-failure checks also
verify that the failure is the intended compiler diagnostic rather than a
missing tool or broken build target.

Run only the normal shader target and focused build checks with:

```powershell
& $cmake --build --preset windows-debug --target SharkCubeShaders SharkSkyboxShaders
& $ctest --preset windows-debug -R '^build\.shader_'
```

For the visual acceptance check, run `SharkSandbox` without arguments. A
clearly textured cube must retain correct hidden-surface occlusion and
perspective while the diagnostic cubemap fills the background. Translation
must not move the sky, right-drag rotation must change it, and resizing from a
wide to a non-wide aspect must not stretch either feature. The exact face check
is documented in [the skybox contract](SKYBOX.md).

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

Run the fixed cube-and-sky presentation contract on hardware and packaged WARP with:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp
```

Each command shows a real PMv2-aware window, presents exactly 1,000 successful
two-pass frames, changes the physical client area from `1280x720` to
`960x600`, proves the projection and `D32_FLOAT` depth extent follow the
aspect-changing resize, proves no frame is submitted while minimized, restores,
shuts down the presentation objects before the window, and checks new
D3D12/DXGI messages plus live D3D12 device children. Submission or presentation
removal failures also emit bounded DRED diagnostics.

The run also exercises three contexts selected by DXGI's back-buffer index.
Each attempt writes one 256-byte record containing scene and sky matrices,
binds it through a root CBV,
copies it to the existing GPU probe, and stages one descriptor in a CPU-only
heap. A context waits only when its own allocator cannot yet be reused because
its preceding submission fence has not completed; that checkpoint protects the
allocator, camera bytes, and probe destination.

Creation records one `StaticCubeUpload` PIX event, one static direct-queue
upload submission, and one bounded wait for the 24-vertex/36-index cube,
deterministic `8x8` checker, and all six faces of the app-local `8x8` sRGB DDS
cubemap. Every submitted frame records one outer `Frame` event with nested
`TexturedCube` and `Skybox` events. Its four-import graph declares exact
checker/cubemap pixel-shader reads, executes both passes, one dependency, four
recorded transitions, and six elided transitions. It
issues two 36-index draws, two texture bindings, and one reversed-Z depth clear;
the sky uses the read-only DSV and writes no depth.

The direct queue reports its timestamp frequency once. One 18-entry query heap
and one 144-byte persistently mapped readback buffer are split into three
six-query frame-context slices: frame begin, cube begin/end, sky begin/end, and
frame end. The frame interval includes the diagnostic probe copy, four graph
barriers, and both passes but excludes its own query resolve. Each pass interval
covers only its callback commands and excludes graph barriers. Results are read
only after the owning context fence completes, using an existing reuse wait or
resize/shutdown drain; timing adds no normal-frame drain.

The summary reports graph pass/barrier counts, PIX event counts, query
high-water/capacity, timing sample count, average/maximum frame,
`TexturedCube`, and `Skybox` milliseconds, both draw/index counts,
scene/sky matrix changes, `depth_clear_count`, depth-resource/read-only-view
creation counts, other resource creation counts,
context reuse, blocking reuse waits, queue drains, and upload/descriptor
high-water marks. The fixed diagnostics invariants require six queries and one
resolve per frame, one completed timing sample per retired submission, and a
six-query per-context high-water. Duration magnitude is deliberately not a
performance gate because adapter speed and timestamp resolution vary.

Run the focused planner, D3D12 executor, and GPU timestamp-state unit coverage
directly with:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[render-graph]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[timestamps]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[skybox]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[assets][dds][cubemap]"
```

CTest registers hardware and WARP device and presentation paths as separate
serial processes, plus a focused packaged-WARP GPU-validation presentation
path. That focused case uses the 120-frame/180-second contract and a 240-second
CTest timeout. The retained presentation case names now cover S-002:
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
tracked fixture, strict loader, app-local deployment, startup upload, and
persistent texture-cube SRV.

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
