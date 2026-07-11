# Shark Windows Development Setup

- **Increment:** `F-001`
- **Last verified:** July 11, 2026
- **Target:** Windows 11 x64, Direct3D 12, C++20

This document defines the workstation contract for Shark. The accompanying
checker is read-only: it installs nothing, downloads nothing, and does not
change files, the registry, execution policy, or `PATH`.

## F-002 build gate

The following items must pass before the reproducible CMake scaffold in `F-002`
can be configured and built.

| Requirement | Shark policy | Why |
|---|---|---|
| Operating system | Updated Windows 11 x64 | Shark is a native Win32/Direct3D 12 engine |
| PowerShell | Windows PowerShell 5.1 or newer | Runs repository setup and verification scripts |
| Git | Git for Windows `2.55.0(2)` or newer | Maintained build with the 2026 NTLM-leak security fixes |
| Visual Studio | Stable Visual Studio 2026 Community or Build Tools | Supplies MSBuild, the Windows C++ environment, and the VS 18 generator target |
| MSVC | `v145` x64/x86 toolset, specifically the `14.50` LTS family | Three-year LTS compiler line rather than the shorter-lived default toolset |
| CMake | `4.2.0` or newer; current stable `4.4.0` recommended | CMake added the `Visual Studio 18 2026` generator in 4.2 |
| Windows SDK | Complete `10.0.26100.0` family or newer | Supplies Win32, D3D12, DXGI, UCRT headers, and x64 import libraries |
| Direct3D runtime | `%WINDIR%\System32\d3d12.dll` | Required system D3D12 loader/runtime |

The checker verifies actual compiler, header, and library payloads. Registry
entries or an Installer entry alone do not count as a usable installation.

## Install the required toolchain

No installation command in this section is run automatically.

### 1. Visual Studio and MSVC

For an individual developer, install the current Stable-channel **Visual Studio
Community 2026**. The command-line-only Build Tools edition also works.

In Visual Studio Installer:

1. Select **Desktop development with C++**.
2. Under **Individual components**, explicitly select the MSVC `v145` x64/x86
   build tools whose version begins with **14.50**. This is the LTS family. Do
   not rely only on the **Latest** component, which can select a shorter-lived
   compiler such as 14.51.
3. Select **C++ CMake tools for Windows**.
4. Select a supported **Windows 11 SDK (10.0.26100 family or newer)**.
5. Keep the installer on the Stable channel; preview/Insiders components do not
   satisfy the project gate.

Recommended but non-blocking Visual Studio components are C++ AddressSanitizer,
C++ test tools, and vcpkg. Shark does not need MFC, ATL, C++/CLI, UWP, ARM,
legacy toolsets, or the June 2010 DirectX SDK.

After modifying Visual Studio, close and reopen PowerShell before rerunning the
checker.

### 2. CMake

Visual Studio may provide a suitable bundled CMake. The checker accepts either
that copy or a standalone installation. If the bundled version is older than
4.2, install the current stable x64 CMake and allow its installer to add CMake to
`PATH`.

An optional user-run installation command is:

```powershell
winget install --id Kitware.CMake --exact
```

### 3. Git

Git for Windows must be callable as `git.exe`. Shark requires `2.55.0(2)` or
newer before dependency restoration. The installed `2.50.1` predates 2026 fixes
that prevent clones from disclosing Windows NTLM credentials to an
attacker-controlled server. Git for Windows supports its latest release line,
so this is a security gate rather than a command-syntax preference.

For this workstation, the user-run upgrade command is:

```powershell
winget upgrade --id Git.Git --exact
```

## Required by later graphics increments

These do not block the code-free `F-002` scaffold, so the checker reports a
warning instead of a failure when they are absent.

### Windows Graphics Tools - required before `G-001`

The Windows **Graphics Tools** optional feature supplies the system D3D12 debug
layer (`d3d12SDKLayers.dll`). It is distinct from Visual Studio's similarly
named graphics debugger. The D3D12 debug layer is a hard acceptance gate once
device work begins.

Install it from **Settings > System > Optional features > View features >
Graphics Tools**. An administrator may alternatively run this command manually:

```powershell
Add-WindowsCapability -Online -Name 'Tools.Graphics.DirectX~~~~0.0.1.0'
```

The checker only calls the corresponding read operation; it never calls
`Add-WindowsCapability`.

### PIX on Windows - required before `G-007`

PIX is Microsoft's Direct3D 12 frame-debugging and profiling application. Use
the current non-preview main release; at the time of this contract that is
`2603.25`. Preview PIX releases are reserved for isolated DirectX preview
experiments.

An optional user-run installation command is:

```powershell
winget install --id Microsoft.PIX --exact
```

### Ninja - optional

Ninja is convenient for some local workflows but is not required. Shark's
initial presets will use CMake's multi-configuration `Visual Studio 18 2026`
generator. A Visual Studio-bundled Ninja is accepted when present.

### Graphics drivers

Keep NVIDIA and Intel drivers current. The checker inventories adapter names and
driver versions, but WMI cannot prove Direct3D feature level, Shader Model, or
binding tier. `G-001` will perform the authoritative runtime capability query.

## Project-restored tools and libraries

Do **not** install these globally for Shark and do not add them to `PATH`.
`F-002` will select and restore exact project-local packages or manifest
baselines. F-001 intentionally does not choose those package versions:

| Package/tool | F-002 ownership |
|---|---|
| DirectX 12 Agility SDK (`Microsoft.Direct3D.D3D12`) | Select and pin a retail release |
| DirectX Shader Compiler (`Microsoft.Direct3D.DXC`) | Select and pin a retail release |
| Microsoft WARP (`Microsoft.Direct3D.WARP`) | Select and pin a retail release |
| WinPixEventRuntime | Select and pin a retail release |
| DirectX headers/math/texture helpers, spdlog, Catch2 | Select an exact vcpkg baseline/manifest version |

This separation prevents a developer's global DXC or SDK copy from silently
changing shader output or runtime behavior.

## Run the checker

From the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-prerequisites.ps1
$LASTEXITCODE
```

`-ExecutionPolicy Bypass` applies only to that child PowerShell process; it does
not alter the machine or user execution policy.

Result meanings:

| Result | Meaning |
|---|---|
| `PASS` | The detected payload satisfies that phase's contract |
| `FAIL` | A requirement that blocks `F-002` is absent, incomplete, or too old |
| `WARN` | A later-phase or optional item is absent |
| `INFO` | Inventory or project-managed dependency information |

Exit codes:

| Code | Meaning |
|---:|---|
| `0` | The `F-002` machine-tool gate passes; later warnings may remain |
| `1` | One or more machine prerequisites block `F-002` |
| `2` | The checker itself encountered an internal error |

The checker performs no network requests. Its only executable probes are local
`--version` calls, `vswhere`, local Windows capability inspection, and local WMI
hardware inventory.

## Planning-time result on this workstation

The verified `F-001` run found:

- **PASS:** Windows 11 build 26200, x64 PowerShell 5.1, the system D3D12 runtime,
  an NVIDIA RTX 4070 Laptop GPU, and Intel UHD Graphics;
- **FAIL:** outdated Git for Windows 2.50.1, no usable Visual Studio 2026/MSVC
  14.50 LTS payload, no CMake 4.2+, and no complete Windows SDK payload;
- **WARN:** Windows Graphics Tools, PIX, and Ninja are absent; and
- **EXPECTED:** no global DXC is present because Shark will restore it locally.

The checker therefore returns exit code `1` until the four blocking toolchain
items are resolved. That is a valid, tested missing-state result, not a checker
failure.

## Official references

- [Visual Studio 2026 downloads](https://visualstudio.microsoft.com/downloads/)
- [Git for Windows maintained release](https://git-scm.com/install/windows)
- [Git for Windows security policy and advisories](https://github.com/git-for-windows/git/security)
- [Install C++ support in Visual Studio](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation)
- [MSVC release cadence and 14.50 LTS policy](https://devblogs.microsoft.com/cppblog/new-release-cadence-and-support-lifecycle-for-msvc-build-tools/)
- [CMake Visual Studio 18 2026 generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html)
- [CMake downloads](https://cmake.org/download/)
- [Windows SDK downloads](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads)
- [Direct3D 12 programming environment](https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-12-programming-environment-set-up)
- [PIX main and preview downloads](https://devblogs.microsoft.com/pix/download/)
- [DirectX 12 Agility SDK package](https://www.nuget.org/packages/Microsoft.Direct3D.D3D12)
- [DirectX Shader Compiler package](https://www.nuget.org/packages/Microsoft.Direct3D.DXC)
- [Microsoft WARP package](https://www.nuget.org/packages/Microsoft.Direct3D.WARP)
- [WinPixEventRuntime](https://www.nuget.org/packages/WinPixEventRuntime)
