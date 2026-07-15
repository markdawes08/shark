# Building Shark

Shark currently supports Windows 11 x64 with Visual Studio 2026, the MSVC
14.50 LTS toolset, CMake 4.2 or newer, and Windows SDK 10.0.26100 exactly.
Run the prerequisite checker before configuring:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-prerequisites.ps1
```

The `F-002 gate` must report `READY`. PIX is a later graphics-diagnostics
requirement and does not block this build scaffold.

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
dependency, including the retail Microsoft packages consumed by later graphics
increments.

The checked-in toolchain also scopes `UCRTContentRoot` to the complete Windows
SDK payload for the CMake process. This avoids a Visual Studio 2026 installation
case where a partial 64-bit Windows Kits registry entry otherwise hides the
actual UCRT libraries under `Program Files (x86)`; it does not modify the
registry or the machine environment.

Expected first-party outputs are:

```text
out/build/windows-vs2026/bin/Debug/SharkSandbox.exe
out/build/windows-vs2026/bin/Debug/SharkTests.exe
out/build/windows-vs2026/bin/Release/SharkSandbox.exe
out/build/windows-vs2026/bin/Release/SharkTests.exe
out/build/windows-vs2026/lib/Debug/SharkEngine.lib
out/build/windows-vs2026/lib/Release/SharkEngine.lib
```

vcpkg deploys the spdlog/fmt runtime DLLs beside executables that need them;
those generated files stay under ignored `out/`. WARP is not linked or deployed.

`SharkSandbox` now opens one native Win32 window and waits efficiently for
window or input events. Resize, minimize, restore, keyboard, mouse, and close
records are visible in the Debug console log. Close the title bar or press
Alt+F4 to exit cleanly.

For a noninteractive lifecycle check, run:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --platform-smoke
```

The smoke mode shows a real nonactivating window, performs a native resize,
minimize, and restore, waits idle, then wakes through an asynchronously posted
close request and confirms destruction. CTest registers this mode separately
with a ten-second timeout, so the normal interactive executable is never
allowed to block an automated test. See [the platform contract](PLATFORM.md)
for the event and ownership rules.

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
