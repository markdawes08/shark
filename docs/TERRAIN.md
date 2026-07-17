# Deterministic Terrain-Tile Contract

- **Completed through:** `T-001`
- **Last verified:** July 17, 2026

Verification snapshot: Debug and Release each passed the full `94/94` CTest
suite, including the 1,000-frame hardware and packaged-WARP runs plus the
120-frame packaged-WARP GPU-validation run. A separate direct Debug NVIDIA
1,000-frame smoke also passed exact accounting. The `94` count records this
revision's discovered suite size; it is not a permanent acceptance constant.

T-001 adds Shark's first terrain surface without introducing a material system,
streaming, collision, or simulation. One project-owned procedural height tile
is built on the CPU, uploaded once with the other static scene resources, and
rendered as the first opaque frame-graph pass. The checker cube and
translation-invariant skybox remain visible as the established scene baseline.

## Canonical height fixture

`shark::terrain::HeightTile` owns the canonical CPU height samples. The T-001
fixture has this exact layout:

```text
sample columns / rows       33 / 33
sample spacing              0.5 m
origin                      (-8.0, -2.25, -12.0) m
sample order                row-major in +Z, then +X
vertices                    1,089
cells                       32 x 32
triangles                   2,048
uint16 indices              6,144
world AABB minimum          (-8.0, -3.171875, -12.0) m
world AABB maximum          ( 8.0, -0.09375,    4.0) m
```

Each stored value is a height offset, so a sample's world `Y` is
`origin.y + height_offset`. The deterministic fixture uses integer arithmetic
and power-of-two divisors before conversion to `float`; it consumes no random
state, transcendental function, external asset, or platform API. Rebuilding it
therefore produces the same dimensions, landmark heights, extrema, indices,
and bounds.

Every LOD0 cell uses one fixed diagonal from `v00` to `v11` and this `+Y`
winding:

```text
(v00, v01, v11)
(v00, v11, v10)
```

Here `v10` is the next `+X` sample and `v01` is the next `+Z` sample. This split
is part of the canonical surface contract; a later renderer LOD may approximate
it visually, but camera distance must never change the physics/query surface.

## Presentation mesh versus future queries

`build_lod0_mesh` preserves every canonical sample as one world-space vertex
and emits the fixed cell indices. It also derives one smooth render normal per
vertex by accumulating each adjacent triangle's unnormalized cross product and
then normalizing the sum. This is area-weighted presentation data used only to
inspect the visible surface.

Those smooth normals are not exact triangle normals, and T-001 does not expose
height, normal, bounds-intersection, or ray-query APIs. T-002 owns those spatial
queries and must evaluate the same canonical heights and fixed triangle split;
it must not treat the smoothed presentation normals or a GPU/render mesh as the
source of truth.

Mesh construction rejects invalid dimensions, nonpositive or nonfinite sample
spacing, nonfinite origin or heights, mismatched sample storage, a surface too
large for the `uint16` index contract, nonfinite generated positions, and
invalid or downward triangle winding.

## GPU representation and diagnostic modes

The terrain upload uses one packed 24-byte vertex per surface sample:

```text
POSITION  R32G32B32_FLOAT  byte 0
NORMAL    R32G32B32_FLOAT  byte 12
```

One immutable terrain vertex buffer and one immutable terrain index buffer
contain both the main surface and the bounds geometry. They are copied in the
existing single startup submission and transition to explicit
`vertex_buffer` and `index_buffer` graph states. Terrain has its own small root
signature containing only the current frame constant-buffer view at `b0`.

The main surface has two immutable raster pipelines:

- **Solid** is the interactive default. Its pixel shader normalizes the smooth
  presentation normal and maps `[-1, 1]` to `[0, 1]` RGB, making orientation
  directly inspectable without claiming a lit material.
- **Wireframe** uses the same vertices, indices, shader, camera, and reversed-Z
  depth policy with a wireframe rasterizer. Press `F1` once to toggle between
  solid and wireframe; key-repeat events do not trigger extra toggles.

An eight-vertex, 24-index line box is derived from the exact mesh AABB and
drawn after the main surface in both modes. The bounds lines depth-test against
the scene but do not write depth. They are always present, rather than being a
third user-controlled mode, so a capture or manual run can inspect the tile's
extent alongside either surface view.

## Frame graph and reversed-Z order

T-001 executes this exact pass order:

1. `Terrain` writes the back buffer and writable depth target, clears both once,
   reads the terrain vertex/index buffers, draws the selected surface mode, and
   draws the AABB lines.
2. `TexturedCube` writes the same color/depth targets, reads its vertex/index
   buffers and checker texture, and draws without clearing.
3. `Skybox` writes color, reads depth through the read-only DSV, reads the same
   cube vertex/index buffers and cubemap, and fills only the far background.

The current graph imports:

```text
BackBuffer
DepthBuffer
CheckerTexture
StartupCubemap
CubeVertexBuffer
CubeIndexBuffer
TerrainVertexBuffer
TerrainIndexBuffer
```

The color/depth hazards produce `Terrain -> TexturedCube -> Skybox`. One frame
therefore compiles eight imports, three passes, two dependencies, four emitted
attachment transitions, and 18 equal-state transition elisions. Input-assembler
buffers remain in their declared read states and emit no barriers. The existing
four attachment barriers remain:

```text
BackBuffer  PRESENT       -> RENDER_TARGET  before Terrain
DepthBuffer DEPTH_WRITE   -> DEPTH_READ     before Skybox
BackBuffer  RENDER_TARGET -> PRESENT        after Skybox
DepthBuffer DEPTH_READ    -> DEPTH_WRITE    after Skybox
```

## Diagnostics and smoke contract

The stable per-frame PIX hierarchy is now:

```text
Frame
  Terrain
  TexturedCube
  Skybox
```

`Terrain` includes both its main-surface and bounds draws. Each frame context
owns eight timestamp queries: frame begin/end plus begin/end pairs for all
three passes. Three contexts use a fixed 24-query heap and three 64-byte
readback slices. Fence-complete decoding reports independent frame, terrain,
cube, and skybox timing aggregates without adding a timing-only queue wait.

Hardware and normal packaged-WARP presentation smoke paths still require 1,000
successful presents; focused packaged WARP with GPU-based validation requires
120. Every smoke switches from solid to wireframe halfway through its required
successful-present count, so both terrain PSOs are submitted on every path.
After the shutdown drain, the smoke requires:

- one terrain surface draw, one terrain bounds draw, one cube draw, and one
  skybox draw per submission;
- 6,144 terrain surface indices and 24 bounds indices per corresponding draw;
- nonzero solid and wireframe counts whose sum equals the terrain surface draw
  count;
- one depth clear in `Terrain`, two texture bindings, and one camera upload per
  submission;
- eight graph imports, three pass executions, two dependencies, four emitted
  transitions, and 18 elisions per submission;
- three named pass events, eight timestamp writes, and one timestamp resolve
  per submission; and
- one static upload submission creating four geometry buffers total: cube
  vertex/index plus terrain vertex/index.

The automated smoke proves command recording, accounting, resource lifetime,
resize/minimize behavior, and a clean Direct3D debug state. It intentionally
does not read back or compare final pixels.

## Verification

From the repository root, discover Visual Studio's CMake tools, then configure,
build, and test both supported configurations:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = (& $vswhere -latest -products * -version "[18.0,19.0)" -requires "Microsoft.VisualStudio.Component.VC.14.50.18.0.x86.x64" -property installationPath).Trim()
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
& $cmake --fresh --preset windows-vs2026
& $cmake --build --preset windows-debug
& $ctest --preset windows-debug
& $cmake --build --preset windows-release
& $ctest --preset windows-release
```

Run the three presentation paths directly when investigating a graphics
failure:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp
& .\out\build\windows-vs2026\bin\Debug\SharkSandbox.exe --present-smoke --warp --gpu-validation
```

Focused CPU/contract coverage is available through:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[render-graph]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[timestamps]"
```

For manual visual acceptance, run the Debug sandbox without arguments. Confirm
that the terrain appears in front of the predominantly sky-blue background,
the normal colors vary smoothly across its hills and basin, the depth-tested
AABB overlay follows the tile bounds without showing through occluding
surfaces, and `F1` switches cleanly between filled and wireframe terrain. Move
and rotate the free-fly camera, resize through different aspect ratios, then
minimize, restore, and close; no stale geometry, depth artifact, or Direct3D
validation error is allowed.

## Explicit non-goals

T-001 adds no canonical height/normal/bounds/ray query API, diagnostic
surface-hit marker, terrain collision, PBR texture or material, UVs, tangents,
chunks, frustum culling, visual LOD, seam handling, streaming, virtual
texturing, mesh shaders, weather interaction, erosion, water, editor, or
general mesh/scene resource system. T-002 owns exact canonical CPU spatial
queries; T-003 owns the first layered PBR terrain materials.
