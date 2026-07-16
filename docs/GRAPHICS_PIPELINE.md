# First HLSL Graphics Pipeline Contract

- **Completed through:** `G-004`
- **Last verified:** July 16, 2026

G-004 proves one reproducible path from project-owned HLSL to a real Direct3D
12 draw. It is intentionally a first-pipeline proof, not yet a general shader
asset system or renderer abstraction.

## Pinned compiler boundary

Configuration requires the manifest-restored `directx-dxc` host package. Shark
accepts only the `dxc.exe` under the active vcpkg host triplet, requires its
adjacent `dxcompiler.dll` and `dxil.dll`, and verifies retail compiler version
`1.9.2602.24`. It never searches `PATH` for an alternative compiler.

DXC is a build-time process only. `SharkEngine` and `SharkSandbox` do not link
the DXC library, and no DXC binary is copied into the application runtime
directory.

## Shader source and generated artifacts

The first program consists of:

```text
shaders/triangle/triangle.hlsl
shaders/shared/triangle_data.hlsli
```

The build compiles `VSMain` as `vs_6_0` and `PSMain` as `ps_6_0`. Both stages
use:

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

Debug adds `-Od`; Release adds `-O3`. Each configuration emits standalone
DXIL, a generated C++ byte-array header, a PDB, and a depfile under:

```text
out/build/windows-vs2026/generated/shaders/<Config>/
```

`SharkSandbox` compiles the generated headers into the executable. Runtime does
not open or locate the adjacent `.dxil` files. All generated artifacts remain
under ignored `out/` and must not be committed.

DXC dependency generation is a separate compiler invocation because the pinned
compiler's `-MD` mode emits dependency information instead of the normal
header/bytecode artifact set. CMake consumes the resulting depfiles, so changing
either the primary `.hlsl` or shared `.hlsli` rebuilds both stages.

## Permanent build-failure checks

CTest owns three shader build checks:

- both depfiles must name `triangle.hlsl` and `triangle_data.hlsli`, and a
  separate build-tree include edit must regenerate its compiled shader;
- `malformed.hlsl` must fail with its expected undeclared-identifier
  diagnostic; and
- `warning.hlsl` must fail with its expected warning promoted by `-WX`.

The negative checks first verify the exact pinned DXC executable and version,
then invoke isolated build targets and require a nonzero result containing the
fixture name, expected sentinel, and compiler error. They also reject a failure
that leaves valid-looking header or DXIL outputs. These targets are not part of
the normal build.

## Runtime bytecode boundary

The public D3D12 presentation API accepts COM-free `ShaderBytecodeView` records:
a pointer plus byte count for each stage. Presentation checks for the DXIL
container signature, then borrows those arrays synchronously while creating the
graphics PSO. Direct3D performs full bytecode and stage compatibility validation
during `CreateGraphicsPipelineState`; Presentation retains no caller pointer.

The vertex shader uses `SV_VertexID` to select three clip-space positions and
colors from static shader arrays. The pixel shader writes the interpolated
color. There is no vertex buffer, index buffer, constant buffer, texture,
sampler, or bound descriptor.

## Root signature and pipeline state

Presentation creates and names one empty version-1 root signature. It permits
input-assembler use and denies unused hull, domain, and geometry shader root
access. There are no root parameters or static samplers.

One immutable, named graphics PSO is created during presentation startup with:

- the generated `vs_6_0` and `ps_6_0` bytecode;
- no input elements and triangle primitive topology;
- solid fill with culling disabled;
- opaque writes to `DXGI_FORMAT_R8G8B8A8_UNORM`;
- depth and stencil disabled;
- one sample and no multisampling; and
- no stream output, cached PSO, or optional shader stages.

PSO creation cannot occur unexpectedly in the frame loop. The PSO and root
signature survive swap-chain resize because neither depends on the back-buffer
instances. Explicit presentation shutdown drains and retires all frames before
releasing the command list, PSO, and root signature.

## Draw contract

Every non-minimized frame:

1. resets the shared command list with the triangle PSO;
2. preserves the existing per-context diagnostic upload copy;
3. transitions and clears the current back buffer;
4. sets the root signature, physical-pixel viewport/scissor, and triangle-list
   topology;
5. issues exactly `DrawInstanced(3, 1, 0, 0)`; and
6. transitions, submits, signals the context fence, and presents through the
   established G-003 lifecycle.

The fixed presentation smoke requires one draw per submitted frame and exactly
three vertices per draw. Hardware, packaged WARP, and packaged WARP with
GPU-based validation must each complete 1,000 successful presents with zero
DirectX corruption/errors and no live D3D12 presentation children.

The smoke does not read back or compare pixels. Visual acceptance is the
centered red/green/blue interpolated triangle over Shark's dark clear color;
command submission, draw accounting, compiler checks, debug-layer validation,
and WARP provide the permanent automated contract.

## Explicit non-goals

G-004 adds no general shader artifact database, reflection, root-signature
convention for resources, shader-visible descriptor heap, PSO hash/cache,
runtime compilation, hot reload, vertex/index buffers, texture loading, camera,
depth buffer, render graph, or image comparison. G-005 owns the first camera,
reversed-Z depth target, resource-bound root signature, and textured cube.

## Primary references

- [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler)
- [Using a root signature](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature)
- [Managing graphics pipeline state](https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12)
- [Creating a basic Direct3D 12 component](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-basic-direct3d-12-component)
