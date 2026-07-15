# Shark Dependency Pins

F-002 establishes one manifest-mode vcpkg restoration path. The curated graph
comes from one immutable registry commit; a small project overlay wraps the
official WARP NuGet archive because the curated registry has no WARP port.
Preview packages are excluded.

## Microsoft binary releases

| Package | Exact version | Official source | Purpose |
|---|---:|---|---|
| `Microsoft.Direct3D.D3D12` | `1.619.4` | NuGet | Retail DirectX 12 Agility SDK runtime and headers |
| `Microsoft.Direct3D.DXC` | `1.9.2602.24` | GitHub release archive | Retail DirectX Shader Compiler and headers |
| `Microsoft.Direct3D.WARP` | `1.0.20` | NuGet | Retail software rasterizer for development smoke tests |
| `WinPixEventRuntime` | `1.0.240308001` | NuGet | PIX marker runtime for later GPU instrumentation |

The curated `directx12-agility` and `winpixevent` ports consume Microsoft's
official NuGet packages. DXC is overridden to the `2026-05-27` port revision,
which downloads Microsoft's official `v1.9.2602.24` GitHub release archive; the
baseline's newer DXC port builds a source snapshot rather than the declared
retail tool. The local `directx-warp` overlay downloads `1.0.20` from NuGet.org
and verifies its SHA-512 before extracting anything.

WARP is licensed for Windows development, testing, and internal use and is not
redistributable by this project. G-001 gives the overlay an imported runtime
target and copies its DLL beside `SharkSandbox.exe`; the explicit WARP path loads
that absolute app-local file before DXGI enumeration. The restored DLL, its PDB,
and every deployed copy remain under ignored build output and must be excluded
from any packaged product.

## vcpkg registry

The default registry is pinned to Microsoft vcpkg commit
[`f87344cac03158cbf1467264565f1fd36b382a24`](https://github.com/microsoft/vcpkg/commit/f87344cac03158cbf1467264565f1fd36b382a24).
At that baseline, Shark's direct ports resolve to:

| Port | Baseline version | Selected features |
|---|---:|---|
| `catch2` | `3.15.2` | default |
| `directx-dxc` | `2026-05-27` | host tool; deliberate retail override |
| `directx-headers` | `1.619.4` | default |
| `directx12-agility` | `1.619.4` | default |
| `directx-warp` | `1.0.20` | project overlay |
| `directxmath` | `2026-06-12` | default |
| `directxtex` | `2026-05-07` | `dx12`, `tools` |
| `spdlog` | `1.17.0#1` | default |
| `winpixevent` | `1.0.240308001` | default |

The registry commit fixes the complete port graph, including transitive ports.
The `x64-windows-shark` overlay triplet selects the MSVC 14.50 LTS family with
the dynamic CRT; the current verified compiler is 14.50.35717. `directx-dxc` is
a host tool. F-003 links the compiled spdlog target privately behind Shark's
public logging API, and vcpkg deploys its spdlog/fmt runtime DLLs only under
ignored build output. G-001 links the DirectX Headers, GUIDs, Agility import
library, and DXGI into the engine; the executable exports SDK version `619` and
path `.\\D3D12\\`, then copies the matching Core and SDK Layers to that folder.
WARP is copied and loaded but not linked. DXC, DirectXTex, and WinPix remain
restored but unconsumed; their owning increments decide the final runtime
boundaries. The vcpkg
executable comes from Visual Studio 2026, `VCPKG_ROOT`, or a complete
installation on `PATH`; its version is reported during setup but is not
misrepresented as a project-pinned dependency.

Package versions change only in a dedicated dependency increment that updates
the manifest, this record, performs a fresh restore, and rebuilds/tests Debug
and Release. Restored packages and caches stay under ignored build or user cache
directories and are never committed. A future packaging increment must include
the preserved spdlog and fmt MIT notices alongside the other required
third-party notices; WARP remains excluded entirely from distributable product
packages.
