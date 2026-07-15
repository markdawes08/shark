# Direct3D 12 Device Contract

G-001 establishes adapter discovery, diagnostics, capability reporting, and
device ownership independently of frame submission. It intentionally creates
no command queue, allocator, command list, fence, resource, swap chain, or
rendered frame.

## Public boundary

`shark::rhi::d3d12::Device` is move-only and hides all Windows, DXGI, D3D12,
WRL, and COM types behind a PIMPL. Public callers receive owned engine records:

- `AdapterInfo` for session index, name, LUID, PCI IDs, memory, flags, and
  required-baseline probe results;
- `RendererCaps` for the maximum Feature Level and Shader Model plus optional
  binding, barrier, DXR, mesh, VRS, sampler-feedback, work-graph, and memory
  capabilities; and
- `DebugMessageCounts` for the initialization info queue.

This is a narrow D3D12 RHI boundary, not a cross-API abstraction. Raw COM
pointers remain private until later renderer code receives typed resource and
queue operations.

The process permits one authoritative `Device` startup. D3D12 debug-layer,
GPU-validation, and DRED settings are process-global and must be fixed before
the first probe; a second startup attempt returns an invalid-state error. GPU
integration cases therefore run as separate `SharkSandbox` processes.

## Initialization order

One device startup follows this fixed order:

1. Validate the tagged adapter selection and diagnostics configuration.
2. For WARP mode only, load the absolute app-local `d3d10warp.dll` and retain
   its module for the full device lifetime.
3. Enable the D3D12 debug layer and optional GPU-based validation.
4. Force DRED automatic breadcrumbs, page-fault tracking, and breadcrumb
   contexts before any device probe.
5. Create a debug DXGI factory.
6. Enumerate all adapters in `HIGH_PERFORMANCE` preference order, probe Feature
   Level 12_0 and Shader Model support, and log every candidate.
7. Select the exact requested adapter with no fallback.
8. Create and name the authoritative Feature Level 12_0 device.
9. Bound and inspect its D3D12 info queue.
10. Query required and optional capabilities, then publish the immutable report.

G-001 arms DRED before device creation. Extracting breadcrumbs and page-fault
details is deferred until G-002 introduces a submission/present path that can
actually encounter device removal.

If a debugger is attached, corruption and error messages also request a debug
break. Unattended processes count and report those severities instead of
trapping. Any initialization corruption or error fails startup; warnings are
reported but do not masquerade as errors.

## Adapter selection

The command-line contract is:

```text
SharkSandbox [--platform-smoke | --gpu-smoke | --capabilities]
             [--warp | --adapter <index>]
             [--gpu-validation]
```

Selection rules are strict:

- With no selector, choose the first local, non-software adapter in DXGI
  high-performance order that meets the required baseline.
- `--adapter N` chooses exactly candidate `N` from the current startup report.
  It never falls through to another GPU or to WARP.
- `--warp` chooses the packaged software adapter explicitly.
- Automatic hardware selection never uses a software or remote adapter.
- An unavailable or insufficient explicit adapter is an error.
- Adapter indices are diagnostic session values, not persistent identities.

`--platform-smoke` remains GPU-independent and rejects GPU selectors. Both
`--gpu-smoke` and `--capabilities` create no window and exit after device
verification. The default interactive path keeps the device alive while the
existing Win32 shell runs.

## Required and optional capabilities

The only startup capability requirements are:

- Direct3D Feature Level 12_0 or newer; and
- Shader Model 6.0 or newer.

The retail Shader Model query begins at 6.9 for the pinned Agility/DXC family
and descends only when an older runtime rejects the requested enum. A missing
optional feature is valid and remains visible as `no`, `not supported`, or
`unavailable` in the report. Versioned tiers use semantic values such as `1.2`.
Enhanced barriers, DXR, mesh shaders, VRS, sampler feedback, work graphs, UMA,
and cache-coherent UMA never decide G-001 startup eligibility.

The selected adapter's name, LUID, vendor/device IDs, memory, Feature Level,
Shader Model, resource tiers, and optional tiers are logged. This gives later
feature experiments an evidence-based capability gate without changing the
baseline silently.

## Runtime layout and provenance

`SharkSandbox.exe` owns the required Agility exports:

```text
D3D12SDKVersion = 619
D3D12SDKPath    = .\D3D12\
```

The build copies the pinned runtime into:

```text
<target>/SharkSandbox.exe
<target>/d3d10warp.dll
<target>/D3D12/D3D12Core.dll
<target>/D3D12/d3d12SDKLayers.dll
```

Startup logs the loaded `D3D12Core.dll` path. Explicit WARP startup loads and
verifies the app-local path, so the smoke test cannot pass accidentally against
the inbox WARP binary. Generated runtime files remain under ignored `out/`.

The WARP NuGet binary is restricted to Windows development, testing, and
internal use and is not redistributed. D3D12 SDK Layers are a development
diagnostic and must be removed from a future production package. See
[Microsoft's WARP guide](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp)
and [Agility SDK integration guide](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/).

## Permanent checks

CTest keeps three independent integration processes:

- the GPU-independent Win32 lifecycle smoke;
- automatic hardware device creation; and
- packaged WARP device creation.

Unit tests cover the exact Feature Level/Shader Model baseline and the complete
CLI conflict/malformed-index boundary. A focused manual WARP command covers
GPU-based-validation setup. Debug and Release must both build and pass all
registered tests with zero D3D12 corruption or error messages before G-001 is
accepted.
