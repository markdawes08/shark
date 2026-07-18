# Canonical Terrain-Tile Contract

- **Completed through:** `S-003`
- **Last verified:** July 18, 2026

S-003 verification snapshot: Debug and Release each passed the full `125/125`
CTest suite, including hardware and packaged-WARP presentation plus focused
packaged-WARP GPU validation.

T-001 verification snapshot: Debug and Release each passed the full `94/94`
CTest suite, including the 1,000-frame hardware and packaged-WARP runs plus the
120-frame packaged-WARP GPU-validation run. A separate direct Debug NVIDIA
1,000-frame smoke also passed exact accounting. The `94` count records that
historical T-001 revision's discovered suite size; it is not the current suite
count or a permanent acceptance constant.

T-001 introduced Shark's deterministic rendered height tile. T-002 makes
the same CPU data an authoritative, platform-independent query surface with
exact height, geometric-normal, bounds, and nearest-ray operations. The
sandbox compares a direct sample with a downward ray and draws their shared
result as a cyan normal pin on the visible LOD0 surface. T-003 preserves that
canonical surface and adds the first bounded visual material path: two
project-owned ground/rock layers blended by deterministic slope and height.
S-003 preserves every query result while adding shared HDR image-based
lighting for the terrain and one material-sphere proof.

## Canonical height fixture

`shark::terrain::HeightTile` stores row-major height offsets in `+Z`, then
`+X`. `HeightTileSurface` takes ownership of one validated tile and caches its
world-space AABB. The deterministic fixture has this exact layout:

```text
sample columns / rows       33 / 33
sample spacing              0.5 m
origin                      (-8.0, -2.25, -12.0) m
sample order                row-major in +Z, then +X
vertices                    1,089
cells                       32 x 32
triangles                   2,048
render uint16 indices       6,144
world AABB minimum          (-8.0, -3.171875, -12.0) m
world AABB maximum          ( 8.0, -0.09375,    4.0) m
```

Each stored value is an offset, so a sample's world `Y` is
`origin.y + height_offset`. World `X/Z` sample coordinates use the same
single-precision `origin + float(index) * spacing` calculation for canonical
queries and LOD0 vertex emission. Those float-rounded coordinates, rather than
an idealized higher-precision affine grid, define the surface. The fixture uses
integer arithmetic and power-of-two divisors before conversion to `float`;
rebuilding it consumes no random state, transcendental function, external
content, or platform API.

Creation rejects sample dimensions below `2x2`, nonfinite or nonpositive
spacing, nonfinite origins or heights, storage that does not match the declared
dimensions, nonfinite generated positions, and degenerate or downward
triangulation. Degenerate validation also rejects origins/spacings whose
float-rounded adjacent sample coordinates collapse. This validation belongs to
the canonical surface. The render-mesh-only `uint16` vertex limit is applied
later by `build_lod0_mesh`; it is not a query-surface ownership limit.

## Fixed LOD0 surface

Every cell uses the diagonal from `v00` to `v11` and this `+Y` winding:

```text
triangle 0: (v00, v01, v11)
triangle 1: (v00, v11, v10)
```

Here `v10` is the next `+X` sample and `v01` is the next `+Z` sample. For
cell-local coordinates `u` and `v`:

```text
u <= v  selects (v00, v01, v11)
u >  v  selects (v00, v11, v10)
```

The `u <= v` rule is the deterministic tie policy on the shared diagonal.
Cell bracketing compares the requested coordinate with the actual float-rounded
canonical sample coordinates emitted as LOD0 vertices. `u` and `v` normalize
between the selected cell's actual endpoints; the sampler does not invert an
ideal `origin + index * spacing` grid in higher precision. Exact internal
sample lines select the upper cell, matching the deterministic `floor` policy
on an exactly representable grid. The maximum world `X` and `Z` edges are
inclusive and clamp to the last cell. A point outside the actual rounded
footprint, or either nonfinite coordinate, produces no sample.

`sample_lod0_surface` returns:

- the requested world `X/Z` and the triangle-planar interpolated world `Y`;
- the normalized, `+Y`-facing geometric normal of that exact triangle;
- the cell coordinates and selected `HeightTileTriangle`; and
- barycentric weights in the same order as the selected triangle's named
  vertices.

`sample_lod0_height` and `sample_lod0_normal` are focused views of that same
operation. Heights are planar triangle interpolation, not bilinear heightfield
interpolation. Query normals are constant per triangle and are not the smooth
render normals.

## Bounds and ray queries

`bounds()` returns the cached exact AABB derived from every validated canonical
sample. It is independent of the render mesh, and startup verifies it equals
the AABB derived for the visible LOD0 mesh.

`Ray3` contains a world-space origin and direction. Both
`intersect_bounds(ray, maximum_distance)` and
`raycast_lod0(ray, maximum_distance)` normalize a finite nonzero direction
internally, so returned distances are meters even when the input direction is
not unit length. The maximum distance must be finite and positive.

Invalid ray input returns a structured `invalid_argument` failure. A valid miss
is a successful result containing an empty optional. Bounds intersection uses
the slab method and returns a clipped, forward-only
`BoundsInterval{entry_distance, exit_distance}` within
`[0, maximum_distance]`; origins inside the bounds therefore enter at zero.

`raycast_lod0` first rejects bounds misses, then tests the fixed triangles and
returns the nearest hit within the inclusive distance limit. Hits are
two-sided and may occur at distance zero when the origin is on the surface. The
returned `float` distance reconstructs the returned position along the
internally normalized ray, keeping those two fields mutually consistent.
Boundary-clamped hit `X/Z` is then passed through `sample_lod0_surface` to
assign the exact canonical normal, cell, triangle, and barycentrics. A direct
sample and ray hit therefore agree within documented float tolerances, while
the hit position remains the point defined by its returned ray distance.
Triangle parallelism rejection scales machine epsilon by the product of the
tested edge length and ray-cross-edge length rather than comparing the
determinant with a fixed world-size threshold. Barycentric edge tolerance
remains dimensionless. Valid very small tiles are therefore not misclassified
merely because their edges are short.
The current single-tile implementation deliberately checks the LOD0 triangles
directly; acceleration belongs with later chunk/spatial-index work.

## Render mesh versus query data

`build_lod0_mesh` preserves every canonical sample as one world-space vertex
and emits the same fixed cell indices. It also accumulates each adjacent
triangle's unnormalized cross product and normalizes the sum to produce one
area-weighted smooth normal per vertex. Those normals make the visible surface
readable under daylight, but they are derived render data only.

`HeightTileSurface` never reads the mesh, its smooth normals, GPU buffers, or a
D3D12 resource. Rendering receives derived vertices plus one query-derived
diagnostic. Future physics and fluid systems must query the canonical surface,
not recover data from a visual LOD or render mesh.

## Query-derived cyan normal pin

At startup the sandbox samples world `X=-5.125`, `Z=-3.25` directly, then casts
a downward ray from `Y=10` with a 20-meter limit. Startup fails unless the
sample and ray agree within `0.00001` on position, exact geometric normal,
cell, triangle, barycentrics, and metric distance.

The resulting static marker contains six vertices and six `uint16` indices:

- one 1-meter line from the exact surface hit along its geometric normal; and
- two 0.4-meter crossed lines centered at the normal tip.

Its first endpoint is the exact query position, so the pin rests on the visible
LOD0 triangle. The marker is cyan; the AABB remains magenta. Both use the
terrain line PSO, `LINELIST`, reversed-Z `GREATER_EQUAL`, depth testing, and no
depth writes. Their diagnostic color is encoded through the negative-`Y`
normal sentinel and is independent of daylight.

The surface vertices, eight AABB vertices, and six marker vertices occupy one
immutable terrain vertex buffer. Their 6,144, 24, and six indices occupy one
immutable terrain index buffer. Packing the marker into those existing two
buffers preserves four total geometry buffers across the cube and terrain.
S-003 additionally packs a 266-vertex/1,584-index material sphere after the
marker in those same terrain buffers. T-003's three material arrays and
S-003's four HDR environment textures share the one-time static upload,
raising its initialization transitions to 13 without changing
geometry-resource count.

## Layered material fixture

`shark::terrain::MaterialPalette` owns deterministic procedural test content;
it performs no file I/O and consumes no random state. The exact fixture is:

```text
layers                         2 (ground, rock)
maps                           3 (albedo, normal, roughness)
base dimensions               32 x 32 texels
mips per layer                6 (32, 16, 8, 4, 2, 1)
format                         RGBA8, 4 bytes/texel
subresources per array        12
subresources total            36
meaningful bytes per layer     5,460
meaningful bytes per array    10,920
meaningful bytes total        32,760
```

Subresources are layer-major, then mip-minor. Albedo bytes are authored for
sRGB decoding and mip-filtered in linear color before re-encoding. Normal mips
decode, average, renormalize, and re-encode tangent-space vectors. Roughness
mips average the linear channel. The albedo array is
`DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`; normal and roughness arrays are
`DXGI_FORMAT_R8G8B8A8_UNORM`.

The public renderer upload boundary accepts three matching
`Texture2DArrayUploadView` records. Creation requires exactly two layers,
matching dimensions/mip counts, a complete mip chain, the expected formats,
and coherently described subresources; it rejects dimensions above `256x256`.
The sandbox's project-owned fixture uses only `32x32`. The private D3D12
backend creates three committed arrays and persistent `Texture2DArray` SRVs in
slots 2-4, after checker slot 0 and retained cubemap slot 1.

## Material blending and shading

Terrain UVs tile in world space at `0.5` repeats per meter: `U` follows `+X`
and `V` follows `-Z`. They are independent of mesh topology and camera motion.
The shader derives its tangent frame from position/UV derivatives, so T-003
does not add stored UVs or tangents to the canonical mesh.

The normalized smooth render normal provides a deterministic macro slope.
Rock weight increases on steeper faces and receives a smaller height-based
contribution; ground weight is its complement. Both layers sample the same
world-XZ coordinate. Tangent-space normals are blended and transformed into a
world-space shading normal. Albedo is blended in linear space because the SRV
performs sRGB decode; roughness is blended in squared space and clamped to a
bounded minimum.

The shaded view combines an unshadowed direct-sun dielectric GGX BRDF using a
fixed `F0=0.04` with diffuse irradiance, GGX-prefiltered specular, and the
split-sum BRDF LUT generated by S-003. The material sphere uses that same
direct and image-based light as a glossy neutral-dielectric proof. Both write
linear color to the HDR scene target. `F3` switches the terrain, sphere, and
sky together between HDR IBL and the retained procedural-daylight fallback.
Negative-Y sentinel normals still bypass all material sampling so the magenta
bounds and cyan query pin retain their exact diagnostic colors.

The directional sun is analytic and deliberately excluded from the bounded
environment convolution, preventing a blocky cubemap sun and double-counted
energy. The shader divides sampled diffuse irradiance by pi for
Lambert-normalized response before adding the separate direct sun.

`F1` changes only raster fill between solid and wireframe. `F2` independently
cycles:

1. shaded albedo/normal/roughness;
2. ground/rock material weights; and
3. the mapped world-space shading normal.

## Frame graph and diagnostics

S-003 extends the renderer-owned production declaration to four passes:

1. `Terrain` clears color/depth, draws the selected solid or wireframe surface,
   draws the material sphere, then draws the magenta AABB and cyan query
   marker.
2. `TexturedCube` preserves the attachments and draws the checker cube.
3. `Skybox` reads depth and fills only the far background with procedural
   daylight or HDR radiance.
4. `ToneMap` reads the HDR scene color and writes the swap-chain back buffer.

The graph still imports:

```text
BackBuffer
SceneColor
DepthBuffer
CheckerTexture
CubeVertexBuffer
CubeIndexBuffer
TerrainVertexBuffer
TerrainIndexBuffer
TerrainAlbedoLayers
TerrainNormalLayers
TerrainRoughnessLayers
EnvironmentRadiance
EnvironmentIrradiance
EnvironmentPrefilteredSpecular
EnvironmentBrdfLut
```

The exact per-frame graph contract remains:

```text
imports                15
passes                  4
dependencies            3
emitted transitions     6
elided transitions      31
```

The marker and sphere add no separate graph pass, dependency, barrier, PIX
scope, or timestamp. `Terrain` contains four indexed draws and its timing
interval includes all of them. A submitted frame records six indexed draws:

```text
terrain surface          6,144 indices
material sphere          1,584 indices
terrain AABB                 24 indices
terrain query marker          6 indices
textured cube                 36 indices
skybox                        36 indices
```

`ToneMap` then issues one non-indexed fullscreen-triangle draw.

The stable PIX hierarchy and timestamp allocation remain:

```text
Frame
  Terrain
  TexturedCube
  Skybox
  ToneMap

10 timestamps per frame-context slice
30 timestamps across 3 frame contexts
```

Hardware and normal packaged-WARP presentation smoke paths require 1,000
successful presents; focused packaged WARP with GPU-based validation requires
120. Every path exercises both fill modes, all three material views, and both
environment modes. After shutdown, smoke accounting requires one surface,
sphere, bounds, marker, cube, sky, and tone-map draw per frame; exact
corresponding index totals; six marker vertices/indices in static storage;
`15/4/3/6/31` graph accounting; ten timestamps; four texture-table binds per
frame; and one static upload producing four geometry buffers, three material
arrays, and four HDR environment textures. Startup proves two layers, six
mips, 36 material subresources, 32,760 meaningful material bytes, 79
environment subresources, 284,608 meaningful environment bytes, and ten
persistent texture descriptors including the resize-owned HDR scene SRV.
The minimize interval proves the marker counter, like every other frame-owned
counter, does not advance while no frame is submitted.

## Verification

From the repository root:

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

Focused CPU and contract coverage:

```powershell
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][query]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][material]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[assets][environment][ibl]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[environment][sphere]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[render-graph]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[timestamps]"
```

The permanent query suite covers malformed construction, deterministic bounds,
flat/ramp surfaces, a twisted `2x2` tile that distinguishes both triangles,
the diagonal tie, exact barycentrics and normals, inclusive maximum and
internal edges, outside/nonfinite samples, normalized metric slab intervals,
vertical and oblique hits, nearest-hit selection, maximum-distance limits,
origin-on-surface hits, valid misses, invalid rays, large-origin
float-rounding parity with LOD0 vertices, and scale-relative ray hits on very
small valid cells.

For manual acceptance, run `SharkSandbox` without arguments. Confirm the cyan
pin begins on the terrain and follows its exact triangle normal, while the
magenta AABB still encloses the tile. Verify the pin remains depth-tested in
both solid and `F1` wireframe modes and does not move with the camera. Use `F2`
to confirm the shaded ground/rock blend, complementary layer weights, and
world-normal visualization. Use `F3` to compare HDR IBL with procedural
daylight and confirm the terrain and material sphere switch coherently. Move,
rotate, resize, minimize, restore, and close; world-space material tiling,
checker cube, sky, tone mapping, diagnostics, and Direct3D validation must
remain clean.

## Explicit non-goals and continuation

S-003 adds no terrain collision response, rigid bodies, authored/file-backed
material assets, stored mesh UVs/tangents, arbitrary layer counts, painting,
chunks, frustum culling, visual LOD, seams, spatial acceleration, streaming,
virtual texturing, mesh shaders, shadows, weather interaction, erosion, water,
editor, dynamic mouse picking, general debug-draw service, or general
scene/mesh/material resource system. The fixed ray proof, static pin, and
material sphere are diagnostics, not gameplay or editor selection.

`HeightTileSurface` remains platform-independent and authoritative; material
weights, sampled normals, and shading never feed its height, ray, or collision
answers. S-003 was completed on July 18, 2026 with the exact
`15/4/3/6/31` graph contract, six indexed scene draws plus the tone-map draw,
four geometry buffers, three material arrays, four HDR environment textures,
ten persistent texture descriptors, four per-frame texture-table binds, and
the expanded PIX/timestamp hierarchy. The upcoming increment is `T-004`,
terrain chunk culling, followed by `T-005`, bounded visual LOD.
