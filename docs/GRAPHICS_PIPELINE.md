# HLSL Graphics Pipeline Contract

- **Completed through:** `T-003`
- **Last verified:** July 18, 2026

G-004 established one reproducible path from project-owned HLSL to a real
Direct3D 12 draw. G-005 keeps that build contract and replaces the
`SV_VertexID` triangle with one resource-bound indexed cube using camera
constants, an SRV, a static sampler, and reversed-Z depth. G-006 keeps the
shader, root signature, and PSO unchanged while recording that draw through the
`TexturedCube` render-graph pass. S-002 adds a dedicated build-time skybox
program and immutable PSO for the ordered `Skybox` pass. These remain focused
pipelines, not a general shader asset system or renderer abstraction. T-001
adds a build-time position/normal terrain program, a dedicated root signature,
solid/wireframe surface PSOs, and a bounds-line PSO. S-002A keeps those shader
targets and PSOs while replacing visible cubemap sampling with a procedural
daylight sky and sharing the same sun constants with terrain lighting. T-002
reuses the bounds-line PSO and terrain shader for a cyan line-list pin whose
anchor and direction come from the canonical terrain surface query. REN-001
moves the scene-specific pipeline helpers into the private renderer D3D12
backend without changing shader bytecode, root signatures, PSOs, or draws.
T-003 expands only the terrain pipeline with a bounded three-array material
table, camera/view root constants, world-XZ sampling, normal mapping, and
direct-sun dielectric GGX shading.

## Pinned compiler boundary

Configuration requires the manifest-restored `directx-dxc` host package. Shark
accepts only the `dxc.exe` under the active vcpkg host triplet, requires its
adjacent `dxcompiler.dll` and `dxil.dll`, and verifies retail compiler version
`1.9.2602.24`. It never searches `PATH` for an alternative compiler.

DXC is a build-time process only. `SharkEngine` and `SharkSandbox` do not link
the DXC library, and no DXC binary is copied into the application runtime
directory.

## Shader source and generated artifacts

The current program consists of:

```text
shaders/cube/cube.hlsl
shaders/sky/skybox.hlsl
shaders/terrain/terrain.hlsl
shaders/shared/camera_constants.hlsli
shaders/shared/daylight.hlsli
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

`SharkSandbox` compiles all six generated stage headers into the executable. Runtime does
not open or locate the adjacent `.dxil` files. All generated artifacts remain
under ignored `out/` and must not be committed.

DXC dependency generation is a separate compiler invocation because the pinned
compiler's `-MD` mode emits dependency information instead of the normal
header/bytecode artifact set. CMake consumes the resulting depfiles, so changing
either the primary `.hlsl` or shared `.hlsli` rebuilds both stages.

## Permanent build-failure checks

CTest owns three shader build checks:

- normal shader depfiles must name their primary HLSL and shared camera include,
  and a separate build-tree include edit must regenerate its compiled shader;
- `malformed.hlsl` must fail with its expected undeclared-identifier
  diagnostic; and
- `warning.hlsl` must fail with its expected warning promoted by `-WX`.

The negative checks first verify the exact pinned DXC executable and version,
then invoke isolated build targets and require a nonzero result containing the
fixture name, expected sentinel, and compiler error. They also reject a failure
that leaves valid-looking header or DXIL outputs. These targets are not part of
the normal build.

## Runtime bytecode boundary

The public `shark::renderer::RendererConfig` accepts COM-free
`ShaderBytecodeView` records: a pointer plus byte count for each cube, skybox,
and terrain stage. The private D3D12 renderer backend checks for the DXIL
container signature, then borrows those arrays synchronously while creating
the graphics PSO. Direct3D performs full bytecode and stage compatibility
validation during `CreateGraphicsPipelineState`; `Renderer` retains no caller
pointer.

The vertex shader consumes one position and one texture coordinate from the
cube vertex buffer. The proof cube uses an identity world transform, so the
shader reads the row-major `view_projection` matrix from root CBV `b0` and
transforms with Shark's row-vector convention:

```hlsl
mul(float4(position, 1.0F), view_projection)
```

The cube pixel shader samples the procedural checker through SRV `t0` and
sampler `s0`. The sky vertex shader uses `sky_view_projection`, forces clip
depth to reversed-Z far zero, and passes cube position as a world direction.
The sky pixel shader normalizes that direction and evaluates a continuous LDR
horizon/zenith/nadir gradient plus a soft sun disk and restrained halo. It has
no texture or sampler binding. Generated byte arrays are borrowed only during
synchronous PSO creation; `Renderer` retains no caller-owned source or
bytecode pointer.

The terrain vertex shader consumes `POSITION` at byte offset 0 and `NORMAL` at
byte offset 12 from a 24-byte interleaved stream. It transforms position by the
same scene matrix and passes world position plus the area-weighted smooth
normal to the pixel shader. The terrain pixel shader derives deterministic
slope/height ground and rock weights, samples three two-layer arrays through
world-XZ UVs, builds a derivative cotangent frame, applies the blended normal,
and evaluates hemisphere ambient plus an unshadowed direct-sun dielectric GGX
BRDF. Albedo is sampled from an sRGB resource; normal and roughness are linear.
Negative-Y sentinel normals bypass material sampling and lighting: `{1, -1,
1}` keeps the bounds magenta, while `{0, -1, 1}` keeps T-002's exact
surface-normal pin cyan. These sentinel values are presentation data and are
not terrain surface normals.

The shared root-CBV layout is exact:

```text
0..63     view_projection
64..127   sky_view_projection
128..143  direction_to_sun.xyz, sun_disk_outer_cosine
144..159  sun_color.xyz, sun_disk_inner_cosine
160..175  zenith_color.xyz, sky_gradient_exponent
176..191  horizon_color.xyz, ambient_strength
192..207  nadir_color.xyz, sun_halo_outer_cosine
208..223  sky_ambient_color.xyz, sun_intensity
224..255  retained FrameProbe
```

## Root signature and pipeline state

The private D3D12 renderer backend creates and names one version-1 cube root
signature. It permits
input-assembler use, denies unused hull, domain, and geometry shader root
access, and contains:

- one `D3D12_SHADER_VISIBILITY_ALL` root constant-buffer view at `b0`;
- one pixel-visible descriptor table covering a single SRV at `t0`; and
- one pixel-visible point-filtered wrap static sampler at `s0`.

The shader-visible CBV/SRV/UAV heap has five persistent slots. `TexturedCube`
points the one-entry root table at checker slot 0. The startup TextureCube SRV
remains in slot 1 as the S-001 asset/upload proof. Terrain points a contiguous
three-entry table at slots 2-4 for albedo, normal, and roughness
`Texture2DArray` SRVs. The procedural sky binds no descriptor. This fixed heap
does not establish a general persistent descriptor allocator or bindless
convention.

Terrain uses a focused version-1 root signature containing the shared root CBV
at `b0`, four 32-bit constants at `b1` (camera world position plus material
view), a pixel-visible three-SRV table at `t0..t2`, and one anisotropic-wrap
static sampler at `s0`. The procedural sky uses a separate b0-only root
signature so T-003 does not expose unused material bindings to it.

One immutable named cube PSO, one immutable named skybox PSO, and three
immutable named terrain PSOs are created during renderer startup. The
applicable surface PSOs use opaque single-sample output, triangle topology,
culling disabled, their declared root signature, and pinned generated
`vs_6_0`/`ps_6_0` bytecode. The cube PSO uses:

- the generated `vs_6_0` and `ps_6_0` bytecode;
- position and texture-coordinate input elements and triangle primitive
  topology;
- solid fill with culling disabled;
- opaque writes to `DXGI_FORMAT_R8G8B8A8_UNORM`;
- `DXGI_FORMAT_D32_FLOAT` depth, writes enabled, `GREATER_EQUAL` comparison,
  and stencil disabled;
- one sample and no multisampling; and
- no stream output, cached PSO, or optional shader stages.

The skybox PSO consumes only the position input from the reused cube buffer,
uses the same color/depth formats, compares `GREATER_EQUAL`, disables depth
writes, uses the b0-only sky root signature, and binds the read-only
DSV after the graph transitions depth to `DEPTH_READ`.

The solid and wireframe terrain surface PSOs use position/normal input,
triangle topology, culling disabled, opaque color writes, reversed-Z
`GREATER_EQUAL`, and depth writes enabled; they differ only in fill mode. The
terrain-bounds PSO uses line topology and disables depth writes while retaining
the same depth comparison. T-002 uses that same line PSO for both the bounds
and query-marker draws; no fourth terrain PSO is created.

PSO creation cannot occur unexpectedly in the frame loop. All PSOs and both
root signatures survive swap-chain resize because none depends on the
back-buffer instances. Explicit renderer shutdown drains and retires all
frames before releasing the command list, PSOs, and root signatures.

## Draw contract

The static scene upload occurs once before the first frame. One direct-queue
submission copies cube and terrain vertex/index data, the deterministic `8x8`
one-mip checker, all six startup-cubemap faces, and all 36 material-array
subresources into immutable default-heap resources. Nine initialization
barriers establish shader/input states before
the monotonic direct fence and bounded startup wait release temporary storage.

Every non-minimized frame then:

1. writes one 256-byte-aligned frame record containing 224 bytes of scene,
   sky, and daylight constants followed by retained probe data at byte 224 in
   the acquired context, then preserves its diagnostic GPU copy;
2. compiles one frame-local graph importing color, depth, checker, cube
   vertex/index buffers, terrain vertex/index buffers, and the three terrain
   material arrays, with exact reads declared by each consumer;
3. resets the shared command list with the solid terrain PSO;
4. executes the graph's pre-terrain back-buffer transition;
5. invokes `Terrain`, which clears the current attachments, selects solid or
   wireframe, binds its material constants and three-SRV table, draws 6,144
   triangle indices in the selected shaded/weight/normal view, then draws 24
   depth-tested bounds indices and six depth-tested cyan query-marker indices
   without writing depth;
6. invokes `TexturedCube`, which preserves the current attachments, binds the
   RTV/DSV pair, root signature, checker descriptor heap, root CBV, SRV table,
   physical-pixel viewport/scissor, vertex/index buffers, and triangle-list
   topology, then issues exactly `DrawIndexedInstanced(36, 1, 0, 0, 0)`;
7. transitions depth to read-only state and invokes `Skybox`, which binds the
   sky PSO, b0-only root signature, read-only DSV, and reused cube buffers
   before a second `DrawIndexedInstanced(36, 1, 0, 0, 0)`;
8. restores back-buffer/depth final states; and
9. submits, signals the context fence, and presents through the established
   G-003 lifecycle.

The fixed presentation smoke requires five indexed draws (terrain surface,
terrain bounds, terrain query marker, textured cube, and skybox), one
frame-constant upload, one depth clear, one checker-texture binding, three pass
executions, ten imported resources, two dependencies, four graph transitions,
and 22 elided transitions per frame. Each frame also binds the terrain
three-SRV table, so total texture-table bindings equal two per submission. The
smoke exercises both fill modes and all three material views.
Hardware and normal packaged WARP must each complete 1,000 successful presents.
The focused packaged-WARP GPU-validation path completes 120 successful presents
with resize and rotation checks at frames 30 and 90, intentionally skips the
already-covered minimize/restore interval, and uses a 180-second internal
deadline plus a 240-second CTest timeout. Every path requires zero DirectX
corruption/errors and no live renderer-owned D3D12 children.

The smoke does not read back or compare pixels. Visual acceptance is the
layered, normal-mapped shaded terrain plus its material-weight/world-normal
views, solid/wireframe fill, depth-tested magenta bounds, and cyan query-derived
normal pin; a procedurally textured cube with stable perspective and correct
hidden-surface occlusion; and a translation-invariant, rotation-responsive
procedural daylight sky.
Command submission,
indexed-draw/depth accounting, compiler checks, debug-layer validation, and
WARP provide the permanent automated contract.

## Explicit non-goals

T-003 adds no general shader artifact database, reflection, root-signature
versioning system, persistent descriptor allocator, PSO hash/cache, runtime
compilation, hot reload, authored texture/material loading, runtime mip
generation, arbitrary material graph/layer system, or image comparison. Its
focused root signatures, five-slot heap, static samplers, procedural material
fixture, static geometry, and fixed PSOs remain specific to the
terrain/cube/sky proof. The graph provides only frame-local
pass/access/barrier orchestration; it is not a shader asset, pipeline-layout,
or material abstraction.

T-003 changes neither the shader-stage set nor the terrain PSO count. Its three
arrays add three imports, six same-state elisions, three SRVs, one terrain table
bind per frame, and three one-time initialization transitions. The current
contract is ten imports, three passes, two dependencies, four barriers, 22
elisions, four geometry buffers, five persistent descriptors, two per-frame
texture-table binds, and eight timestamps.

REN-001 relocates `cube_scene_data`, `daylight_scene_data`,
`skybox_scene_data`, and `terrain_scene_data` from the D3D12 RHI into
`engine/renderer/src/d3d12`; the scene-named timestamp layout/accumulator moves
with them. Generic frame-resource and legacy-transition helpers remain in the
RHI. There is no public D3D12 `Presentation` class.

See [the camera and textured-cube contract](CAMERA_AND_CUBE.md) for the
coordinate, input, geometry, texture, depth, resize, and acceptance rules. See
[the minimal render-graph contract](RENDER_GRAPH.md) for the pass declaration
and barrier execution around this pipeline. See
[the DDS cubemap contract](DDS_CUBEMAP.md) for the retained startup texture,
and [the skybox contract](SKYBOX.md) for procedural daylight and depth
behavior.
See [the terrain contract](TERRAIN.md) for the canonical surface query,
deterministic surface, material fixture, and diagnostic rendering modes.
`T-003` was completed on July 18, 2026. The next increment is `S-003`, HDR
environment lighting.

## Primary references

- [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler)
- [Using a root signature](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature)
- [Managing graphics pipeline state](https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12)
- [Creating a basic Direct3D 12 component](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-basic-direct3d-12-component)
