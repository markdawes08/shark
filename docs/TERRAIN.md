# Canonical Terrain-Tile Contract

- **Completed through:** `REN-001`
- **Last verified:** July 18, 2026

T-001 verification snapshot: Debug and Release each passed the full `94/94`
CTest suite, including the 1,000-frame hardware and packaged-WARP runs plus the
120-frame packaged-WARP GPU-validation run. A separate direct Debug NVIDIA
1,000-frame smoke also passed exact accounting. The `94` count records that
historical T-001 revision's discovered suite size; it is not the current suite
count or a permanent acceptance constant.

T-001 introduced Shark's deterministic rendered height tile. T-002 now makes
the same CPU data an authoritative, platform-independent query surface with
exact height, geometric-normal, bounds, and nearest-ray operations. The
sandbox compares a direct sample with a downward ray and draws their shared
result as a cyan normal pin on the visible LOD0 surface.

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
buffers preserves four total geometry buffers across the cube and terrain,
one static upload submission, and six startup transitions.

## Frame graph and diagnostics

T-002 preserves the three-pass order. REN-001 moves its production declaration
to the renderer-owned `frame_pipeline` composer without changing it:

1. `Terrain` clears color/depth, draws the selected solid or wireframe surface,
   then draws the magenta AABB and cyan query marker.
2. `TexturedCube` preserves the attachments and draws the checker cube.
3. `Skybox` reads depth and fills only the far background with procedural
   daylight.

The graph still imports:

```text
BackBuffer
DepthBuffer
CheckerTexture
CubeVertexBuffer
CubeIndexBuffer
TerrainVertexBuffer
TerrainIndexBuffer
```

The exact per-frame graph contract remains:

```text
imports                 7
passes                  3
dependencies            2
emitted transitions     4
elided transitions      16
```

The marker adds no resource, graph pass, dependency, barrier, PIX scope, or
timestamp. `Terrain` contains three indexed draws and its existing timing
interval includes all of them. A submitted frame now records five indexed
draws total:

```text
terrain surface          6,144 indices
terrain AABB                 24 indices
terrain query marker          6 indices
textured cube                 36 indices
skybox                        36 indices
```

The stable PIX hierarchy and timestamp allocation remain:

```text
Frame
  Terrain
  TexturedCube
  Skybox

8 timestamps per frame-context slice
24 timestamps across 3 frame contexts
```

Hardware and normal packaged-WARP presentation smoke paths require 1,000
successful presents; focused packaged WARP with GPU-based validation requires
120. Every path switches from solid to wireframe halfway through its required
successful-present count. After shutdown, smoke accounting requires one
surface, bounds, marker, cube, and sky draw per frame; exact corresponding
index totals; six marker vertices/indices in static storage; unchanged graph
and timestamp counts; and one static upload producing four geometry buffers.
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
both solid and `F1` wireframe modes and does not move with the camera. Move,
rotate, resize, minimize, restore, and close; the daylight terrain, checker
cube, sky, diagnostics, and Direct3D validation must remain clean.

## Explicit non-goals and continuation

T-002 adds no terrain collision response, rigid bodies, material/PBR textures,
UVs, tangents, chunks, frustum culling, visual LOD, seams, spatial acceleration,
streaming, virtual texturing, mesh shaders, weather interaction, erosion,
water, editor, dynamic mouse picking, general debug-draw service, or general
scene/mesh resource system. The fixed ray proof and static pin are diagnostics,
not gameplay or an editor selection tool.

REN-001 moves `TerrainRenderMode`, mesh upload configuration, statistics, the
production frame composer, and the terrain D3D12 scene helper behind
`shark::renderer::Renderer`. `HeightTileSurface` remains platform-independent
and authoritative; there is no public D3D12 `Presentation` class. REN-001 was
completed on July 18, 2026 without changing pixels, resources, the
`7/3/2/4/16` graph contract, five draws, four geometry buffers, PIX/timestamps,
or smoke accounting. The upcoming increment is `T-003`, layered PBR terrain
materials.
