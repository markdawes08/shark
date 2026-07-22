# Canonical Terrain-Tile Contract

- **Completed through:** `PHY-007`
- **Last verified:** July 22, 2026

T-008 composes the untouched T-007 rolling-height oracle with a bounded
deterministic Q8 lake-basin post-process. The active composite checksum is
`0x4890DE3E1AA063A9`; extrema and world AABB remain unchanged, while maximum
LOD0 slope becomes `18.681598` degrees, maximum adjacent X/Z steps become
`1.16015625`/`1.33203125` meters, and maximum coarse deviation becomes
`0.603515625` meter. The validated spawn-side support component is centered at
`(-128,-128)` with `56/48`-meter nominal semi-axes and dense-sampled continuous
spans of approximately `111.998421x95.998672` meters. Its waterline is `-4`
meters. The
sampled core at `(-124,-10.47265625,-128)` is `6.47265625` meters below that
line; 260 canonical rim samples are fixed at `-2.5` meters, and the footprint
contains 530 canonical samples.

The scenario publishes dry spawn ground at `(-128,1.34375,-20)` and a camera eye
at `(-128,3.34375,-20)` with pitch `-0.1`; sampled support-boundary distance is
about 58.496138 meters. The scenario also publishes four PHY-004 sphere
spawns. Primary body 0 remains at `(-128,-44)` exactly 12 meters above its
canonical LOD0 ground sample; bodies 1 and 2 share an airborne collision
height, and body 3 remains isolated. Cube and all spheres remain outside the
water support and initially unburied.
W-001 consumes the basin metadata without changing any canonical terrain
sample, query, topology, resource, or descriptor.

Focused Debug verification passes 56,792 lake-basin assertions across three
cases, 4,732 scenario assertions across three cases, 23 culling assertions
across two cases, and 367 LOD assertions across two cases. Active T-008
validation passed the Debug build and all `150/150` tests in 195.60 seconds;
its hardware, WARP, and WARP+GBV presentation gates took 8.61, 83.60, and
66.85 seconds. The Release build and all `150/150` tests passed in 157.45
seconds, with the same gates taking 2.06, 78.66, and 60.14 seconds. Both
configurations retained exact smoke accounting.

The separate final Debug RTX 4070 smoke submitted 1,000 frames with
86,375 visible and 138,625 culled chunks, 125/86,250 LOD0/coarse draws, and
192,000/74,520,000 LOD0/coarse indices. It logged the active maximum coarse
error as 0.603516 meter, GPU frame average/maximum 0.125/0.681 ms, and Terrain
average/maximum 0.105/0.285 ms. Direct3D validation reported zero corruption,
zero errors, zero live child objects, and only the two expected
`ReportLiveDeviceObjects` warnings.

Historical T-007 deterministic-shape snapshot: seed `0x4FFB0830` and five fixed-point
value-noise bands produce the row-major height checksum
`0xC0FB1097EBCB8B7B`. All 115,200 LOD0 triangles are at or below 12 degrees;
the maximum is `11.251308698`, with `6.2933`/`8.7923`-degree p90/p99 slopes.
Offsets span `-12.30078125..13.5234375` meters for `25.82421875` meters of
relief, and maximum adjacent X/Z steps are `0.79296875`/`0.6875` meters.
Recorded construction times are `8098.750` ms on Debug hardware, `87.203` ms on
Release hardware, and `7699.463` ms on Debug WARP. The final Debug and Release
`SharkTests` runs each passed 135 cases and 303,048 assertions, including direct
relief, near-camera clearance, and then-active smoke-pose checks. Debug
`ctest -L unit` also passed `135/135`. Shader, platform, hardware-device, and
WARP-device probes passed earlier in the same increment.

The historical T-007 smoke finishes with a smoke-only near-camera pose that selects
one LOD0 and 60 coarse chunks, keeping both D3D12 surface-index ranges live.
Debug and Release hardware completed 1,000 frames, normal WARP completed 600,
and focused WARP+GBV completed 120 with the then-active exact accounting, zero
corruption/errors, and zero live child objects. Presentation shutdown retained
only the two expected device-level RLDO warnings. The focused path alone uses
`640x360 -> 480x300`, preserving the normal `16:9 -> 1.6` aspect sequence;
hardware and normal WARP retain `1280x720 -> 960x600`.

Historical T-006 verification snapshot: Debug and Release builds completed, and all
`135/135` unit CTest entries passed in both configurations. The latest focused
capacity contract completed in `5.98` seconds in Debug and `0.09` seconds in
Release; the large-scene LOD smoke completed in `5.91` seconds in Debug and
`0.09` seconds in Release. The latest measured terrain-construction boundary,
including the LOD/query proof, was `6049.240` milliseconds in Debug and
`82.738` milliseconds in Release. These are startup observations, not
retained-CPU-memory or temporary-upload measurements.

The full Debug `SharkTests` run passed 302,707 assertions across those 135 test
cases. The build/shader labels passed `3/3`, and the platform/device integration
subset passed `3/3`, in each configuration.

The exact presentation gates also passed cleanly in Debug, and the 1,000-frame
hardware gate passed with the same exact accounting in Release. Hardware
presents 1,000 frames. Packaged WARP Debug uses a measured 600-frame gate for
this larger scene, while packaged WARP with GPU-based validation retains its
focused 120-frame gate. Every recorded path reported zero D3D12 errors and
zero live child objects; their terrain accounting is recorded below.

T-005 verification snapshot: Debug and Release each passed all 141 discovered
CTest entries in bounded batches, including 132 `SharkTests` unit cases with
301,254 assertions, the shader build probes, the platform lifecycle check, the
hardware and packaged-WARP 1,000-frame presentation paths, and the focused
120-frame packaged-WARP GPU-validation path. The presentation gates reached
the exact `8/8 -> 3/2` LOD split and reported zero D3D12 errors and zero live
child objects.

T-004 verification snapshot: Debug and Release each passed the full `133/133`
CTest matrix, including all 124 `SharkTests` unit cases, the hardware and
packaged-WARP 1,000-frame presentation paths, and the focused 120-frame
packaged-WARP GPU-validation path with exact `16 -> 5` visibility.

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
lighting for the terrain and one material-sphere proof. T-004 derives a
full-resolution `4x4` render-chunk layout from the same canonical tile,
conservatively culls exact chunk AABBs against the current camera frustum, and
introduced magenta diagnostic bounds for the visible chunks. T-005 adds one
boundary-preserving coarse visual surface, measures its exact
continuous vertical deviation from LOD0, and selects between the two ranges
from camera distance without changing canonical queries. T-006 retains that
small fixture as the analytic regression oracle and moves the sandbox to a
separate bounded resident-capacity fixture: a `241x241` canonical surface,
`225` chunks, and both visual ranges packed under the existing global
`R16_UINT` index contract. T-007 replaces only that capacity surface's
alternating diagnostic heights with deterministic multi-scale rolling terrain;
topology, queries, resource budgets, and the compact analytic oracle remain
unchanged. T-008 then applies one scenario-owned Q8 basin post-process whose
support is bounded away from the tile edge and whose samples outside that
support remain byte-identical to the T-007 base.

## Canonical height fixtures

`shark::terrain::HeightTile` stores row-major height offsets in `+Z`, then
`+X`. `HeightTileSurface` takes ownership of one validated tile and caches its
world-space AABB.

### Active W-001 Environment Lab scenario

`world::make_environment_lab_scenario()` is the composition root for the active
sandbox terrain. It creates the unchanged T-007 base, applies
`terrain::shape_closed_lake_basin`, validates core and spawn queries, and
publishes the authoritative metadata consumed by W-001:

```text
composite height checksum      0x4890DE3E1AA063A9
footprint center               (-128, -128) m
nominal semi-axes              56 / 48 m
X warp                         dx + (dz^2 - 1,152) / 512
Z warp                         dz + (dx^2 - 1,568) / 1,024
approx. continuous X extrema   -181.753764 / -69.755343 m
approx. continuous Z extrema   -174.473083 / -78.474411 m
approx. continuous X/Z spans   111.998421 / 95.998672 m
waterline                      -4.0 m
profile core depth             6.5 m
rise / rim-end / blend radii   1.15 / 1.35 / 2.0
sampled core                   (-124, -10.47265625, -128) m
sampled core clearance         6.47265625 m below waterline
fixed rim height               -2.5 m
fixed rim clearance            1.5 m above waterline
footprint / rim samples        530 / 260
spawn ground                   (-128, 1.34375, -20) m
spawn camera eye               (-128, 3.34375, -20) m
spawn yaw / pitch / far plane  0 / -0.1 rad / 1,500 m
sampled support-boundary distance approximately 58.496138 m
maximum adjacent X/Z step      1.16015625 / 1.33203125 m
maximum LOD0 slope             18.681598 degrees
maximum coarse deviation       0.603515625 m
```

The warp defines normalized squared radius `rho`, but the global
`rho <= 1` inequality is not treated as one bounded lobe. W-001's authoritative
water support is its intersection with the local quad domain centered at
`(-128,-128)` with X/Z half-extents `64/56`; this selects the intended
spawn-side component. That support is not itself the final visible shoreline.
The shoreline is the depth-tested intersection of the `-4`-meter water plane
with canonical terrain, so canonical bank triangles can occlude a narrow part
near its edge. Terrain rises from the shaping boundary to a fixed rim, holds the
rim, then smoothly returns to the T-007 base; samples beyond normalized radius
`2.0` remain byte-identical.

The shaper rejects any input tile containing a non-Q8 height sample, plus
malformed/nonfinite, non-Q8-representable, unsafe-extreme, or unordered shape
and grid inputs and any support that reaches the terrain boundary. Success is
returned only after proving one connected lattice footprint inside a closed
canonical-triangle rim. The post-process changes height values only: sample
order, origin, spacing, topology, extrema, AABB, chunk layout, index domains,
and resource sizes remain unchanged.

### Historical T-007 natural rolling base oracle

`make_large_capacity_height_tile()` remains the project-owned,
fixed-seed rolling landscape over T-006's unchanged resident topology. Five
smooth value-noise bands use integer hash/lattice arithmetic, Q23 lattice
values, a Q30 quintic fade, and one symmetric final conversion to Q8 meters.
Every output is therefore an exact multiple of `1/256` meter and consumes no
runtime random state, transcendental function, external content, or periodic
texture.

```text
base seed                    0x4FFB0830
per-band seed step           0x9E3779B9
band 0                       axis, period 128, amplitude 10 m, phase (37, 71)
band 1                       diagonal, period 91, amplitude 6 m, phase (19, 53)
band 2                       axis, period 32, amplitude 3 m, phase (11, 23)
band 3                       diagonal, period 23, amplitude 2 m, phase (7, 13)
band 4                       axis, period 8, amplitude 1 m, phase (3, 5)
lattice / fade / output      Q23 / Q30 / Q8 (1/256 m)
checksum                     0xC0FB1097EBCB8B7B
checksum encoding            64-bit FNV-1a over row-major IEEE-754 float bytes,
                             least-significant byte first
```

```text
sample columns / rows       241 / 241
sample spacing              4.0 m
origin                      (-480.0, -2.0, -480.0) m
sample order                row-major in +Z, then +X
canonical height payload    232,324 bytes
vertices                    58,081
cells                       240 x 240
LOD0 triangles / indices    115,200 / 345,600
coarse triangles / indices   64,800 / 194,400
combined surface indices    540,000
render chunk grid           15 x 15
cells per render chunk      16 x 16
LOD0 triangles / indices    512 / 1,536 per chunk
coarse triangles / indices  288 /   864 per chunk
render chunks               225
minimum / maximum offset    -12.30078125 / 13.5234375 m
total relief                25.82421875 m
minimum offset coordinate   (0, 91)
maximum offset coordinates  (126, 228), (126, 229)
world AABB minimum          (-480.0, -14.30078125, -480.0) m
world AABB maximum          ( 480.0,  11.5234375,  480.0) m
maximum adjacent X/Z step   0.79296875 / 0.6875 m
triangles <= 12 degrees     115,200 / 115,200
triangles <= 30 degrees     115,200 / 115,200
maximum slope               11.251308698 degrees
slope p90 / p99             6.2933 / 8.7923 degrees
maximum coarse deviation    0.1171875 m
```

The greatest referenced vertex is `58,080`, leaving `7,455` additional
addressable indices below the maximum `uint16` value. T-008 retains T-007's
index format and renderer path.

The canonical-height figure above is only the `58,081 * sizeof(float)` stored
height payload. It must not be interpreted as peak or retained total CPU
memory: the owning tile, query surface, derived layouts, vectors, allocator
overhead, and temporary startup data were not measured as one total.

Selected height-offset anchors are part of the deterministic contract:

| Sample `(X,Z)` | Offset (m) |
|---|---:|
| `(0,0)` | `-4.1875` |
| `(240,0)` | `-5.40625` |
| `(0,240)` | `-1.1796875` |
| `(240,240)` | `-1.01171875` |
| `(120,120)` | `-1.92578125` |
| `(120,148)` | `0.6953125` |
| `(30,197)` | `-2.1328125` |
| `(87,42)` | `-2.9609375` |
| `(176,203)` | `-2.22265625` |

### Historical T-006 capacity height profile

T-006 used the same dimensions, topology, index ranges, and resource budgets
with a deliberately shallow checker alternating `+0.25/-0.25` meter. Its world
Y bounds were `-2.25..-1.75` meters, every chunk's coarse deviation was
`0.5` meter, and its active smoke splits were `3/90 -> 4/67`. Those values
remain historical capacity evidence and are not the active T-008 scenario.

### Compact T-005 analytic regression oracle

`make_deterministic_height_tile()` remains unchanged and continues to drive
the detailed query, triangulation, chunk-boundary, continuous-error, and
mixed-LOD regression coverage. It is no longer the active sandbox terrain.
Its exact layout is:

```text
sample columns / rows       33 / 33
sample spacing              0.5 m
origin                      (-8.0, -2.25, -12.0) m
sample order                row-major in +Z, then +X
vertices                    1,089
cells                       32 x 32
triangles                   2,048
LOD0 uint16 indices         6,144
coarse uint16 indices       3,840
combined surface indices    9,984
render chunk grid           4 x 4
cells per render chunk      8 x 8
LOD0 triangles/indices      128 / 384 per chunk
coarse triangles/indices     80 / 240 per chunk
render chunks               16
maximum coarse deviation    0.140625 m
world AABB minimum          (-8.0, -3.171875, -12.0) m
world AABB maximum          ( 8.0, -0.09375,    4.0) m
```

Each stored value is an offset, so a sample's world `Y` is
`origin.y + height_offset`. World `X/Z` sample coordinates use the same
single-precision `origin + float(index) * spacing` calculation for canonical
queries and LOD0 vertex emission. Those float-rounded coordinates, rather than
an idealized higher-precision affine grid, define the surface. Both
project-owned fixtures use exact deterministic arithmetic before conversion to
`float`; rebuilding either consumes no random state, transcendental function,
external content, or platform API.

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
Canonical ray queries still check the LOD0 triangles directly. T-004 render
chunks are presentation data and are not a query or physics acceleration
structure. T-005's coarse ranges likewise remain derived presentation data;
such acceleration requires a separate future increment.

## Bounded segment closest-feature query

The PHY-005 terrain boundary adds the CPU-only `Segment3` input and
`closest_lod0_point_to_segment(segment, maximum_distance)` query. Both segment
endpoints are world-space positions. The query finds the closest witness pair
between that finite segment and the exact canonical LOD0 triangles, provided
their Euclidean separation is within the inclusive maximum distance. Its
candidate search is bounded to the segment's expanded world-space bounds and
the corresponding conservative X/Z cell range; a valid query with no candidate
inside the limit succeeds with an empty optional.

Both endpoints and the maximum distance must be finite, and the maximum
distance must be positive. Invalid input returns `invalid_argument` without
mutating the owned tile or any caller state. Coincident endpoints are valid: a
zero-length segment deterministically reduces to a point-versus-triangle
closest-feature query rather than being rejected as a directionless ray.

Shared edges, vertices, coplanar features, and other equal-distance candidates
retain the first candidate in this stable ownership order:

1. cells in row-major order, with `+Z` as the outer traversal and `+X` as the
   inner traversal;
2. `v00_v01_v11` before `v00_v11_v10` within each cell; and
3. within one triangle, an intersection before the first endpoint, second
   endpoint, and directed edges `AB`, `BC`, then `CA`.

`HeightTileSegmentClosestPoint` returns both witnesses. `segment_position` is
the point on the finite segment and `segment_parameter` is its clamped
parameter in `[0, 1]`, where zero names the first endpoint and one names the
second. `surface.position` is the canonical terrain witness;
`surface.normal`, cell coordinates, fixed triangle, and barycentrics retain
that triangle's exact ownership. The barycentric components use the named
triangle-vertex order, matching direct surface samples and ray hits. `distance`
is the metric separation in meters.

This query reads `HeightTileSurface`'s canonical samples directly. It does not
read smooth render normals, chunk bounds, culling state, coarse ranges, a render
mesh, GPU buffers, or camera-selected LOD, and it adds no renderer or D3D12
state. Its focused Debug and Release suite passes `442` assertions across seven
cases; at PHY-005 completion both complete unit presets passed `202/202`.

## Bounded canonical-triangle candidates

PHY-006 adds
`lod0_triangles_overlapping_bounds(const Bounds3&)` as the narrow terrain
candidate boundary needed by oriented boxes. Its finite ordered world AABB is
inclusive on all three axes. A disjoint valid box succeeds with an empty
vector; malformed, reversed, or nonfinite bounds return `invalid_argument`.

The query conservatively maps the clipped X/Z range to nearby cells, then
filters each exact triangle by its own 3D AABB. Results retain the canonical
order: cells are row-major in `+Z`, then `+X`, with `v00_v01_v11` before
`v00_v11_v10`. Each record contains the three exact world positions, geometric
normal, cell coordinates, and fixed split. Shared edges and the maximum tile
boundary are inclusive, so touching candidates are not lost.

This is deliberately candidate generation, not collision response or a public
spatial acceleration structure. Box contact performs the complete SAT test and
selects a manifold; Terrain does not know about boxes, rigid bodies, impulses,
render chunks, or visual LOD. The focused Debug and Release suite passes `351`
assertions across seven cases, including exact repeated results without tile
mutation.

## Render mesh versus query data

`build_lod0_mesh` preserves every canonical sample as one world-space vertex
and emits the same fixed cell indices. It also accumulates each adjacent
triangle's unnormalized cross product and normalizes the sum to produce one
area-weighted smooth normal per vertex. Those normals make the visible surface
readable under daylight, but they are derived render data only.

`HeightTileSurface` never reads the mesh, its smooth normals, GPU buffers, or a
D3D12 resource. Rendering receives derived vertices plus one query-derived
diagnostic. Future physics and fluid systems must query the canonical surface,
not recover data from either visual LOD or a render mesh.

## Full-resolution chunks and frustum culling

`build_lod0_chunk_layout` partitions the canonical cell grid without creating
a second surface. It validates the source tile and `uint16` vertex capacity,
requires positive chunk dimensions, supports partial chunks at the maximum
`+X/+Z` edges, and emits chunks row-major in `+Z`, then `+X`. Every source cell
appears exactly once. Its two triangles retain the fixed LOD0 winding and
diagonal.

The active W-001 scenario retains T-008's `16x16` cells per chunk. Its
`240x240` cells divide exactly into 225 complete chunks in a row-major
`15x15` grid. Each chunk owns:

- its first cell and cell extent;
- one contiguous 1,536-index range in the chunk-major 345,600-index LOD0
  stream;
- an exact AABB enclosing all `17x17` canonical samples used by its cells; and
- eight diagnostic bounds vertices with 24 line-list indices.

All active chunk indices reference the one unchanged 58,081-vertex LOD0
stream. Shared edge samples are not duplicated. The sandbox passes only draw
ranges, full-resolution bounds, and measured error through the renderer upload
boundary; no D3D12 object enters the terrain module. The compact regression
oracle continues to prove the generic `8x8`-cell, `4x4` layout and partial-edge
behavior.

Renderer startup accepts one through 4,096 chunk records. It requires each
LOD0 and coarse range to be nonempty and triangle-aligned, all LOD0 ranges to
form one contiguous prefix, all coarse ranges to form the contiguous suffix,
and their union to cover the complete surface index stream exactly once. A
coarse range may not exceed its chunk's LOD0 count. Each record also requires
finite ordered bounds enclosing every referenced LOD0 and coarse vertex within
`0.00001` meters, a finite nonnegative geometric error, and exactly eight
bounds vertices plus 24 local line indices per chunk. Invalid tables fail
before resource creation.

Before recording `Terrain`, the renderer extracts six normalized,
inward-facing planes from the current row-vector `view_projection` and the
Direct3D clip volume:

```text
-w <= x <= w
-w <= y <= w
 0 <= z <= w
```

The `z` half-spaces therefore honor Shark's finite reversed-Z projection
without special-case near/far assumptions. Scale-first double-precision
normalization makes plane extraction invariant under positive homogeneous
matrix scaling, including very small or large finite scales. A support-point
AABB test rejects a chunk only when it is wholly outside a plane by more than
the fixed `0.0001`-meter tolerance. Tangent and intersecting boxes remain
visible. The test is conservative: it may retain a box at a frustum corner,
but it cannot discard one intersecting the frustum, which is the intersection
of all six clip half-spaces. Nonfinite or rank-deficient view-projection
matrices fail the frame before submission.

The interactive camera starts at the scenario-owned eye
`(-128,3.34375,-20)` meters with pitch `-0.1` radians and a 1,500-meter far
plane, overlooking the dry basin. Normal movement is 32 meters/second and
sprint is four times that speed. Presentation smoke deliberately retains its
separate deterministic `(0,28,112)` pose with pitch `-0.25`. At both its
initial `16:9` aspect and the scripted
`960x600` resize, the camera sees `93 / 225` chunks. Turning to yaw `1.25`
radians at the resized aspect leaves `72 / 225` visible. The renderer logs
`Terrain chunks: <visible> / <total> visible (LOD0=<fine>, coarse=<coarse>)`
under `renderer.terrain` only when visibility or the LOD split changes. Culling
uses the ordinary camera matrix, never the translation-free sky matrix, and
never changes canonical height, normal, bounds, or ray answers.

## Boundary-preserving coarse LOD

`build_boundary_preserving_coarse_chunk_layout` derives exactly one additional
visual range for every complete, even-sized render chunk. The active
`16x16`-cell chunk is divided into 64 `2x2`-cell patches. Each patch emits a
fan around its canonical center sample. A patch touching the chunk boundary
splits its outer macro edge at the intervening canonical midpoint, so every
active chunk edge retains all 17 samples and all 16 LOD0 boundary segments.

Fine/fine, coarse/coarse, and fine/coarse neighbors therefore submit identical
edge segments and positions. Mixed LODs are crack-free without skirts,
morphing, duplicated vertices, stitch masks, or adjacency-dependent index
variants. Both active surfaces reference the unchanged 58,081-vertex stream.
An active chunk contains 288 coarse triangles and 864 indices. The compact
oracle still locks 80 triangles and 240 indices for an `8x8` chunk. Generic
odd-sized or partial chunks conservatively copy their complete LOD0 range
instead of claiming a coarser approximation.

Each coarse chunk records the exact maximum continuous vertical separation from
the canonical LOD0 surface over its closed `X/Z` domain. Measurement overlays
every coarse triangle with every overlapping fine triangle and evaluates all
vertices of their clipped intersection polygon; a linear height difference
reaches its extrema at those vertices. The active shaped landscape's global
maximum is `0.603515625` meter. The historical T-007 base measured
`0.1171875` meter; the compact analytic oracle retains its exact
`0.140625`-meter maximum, while T-006's alternating capacity profile measured
`0.5` meter.

For each visible chunk, the renderer computes `D`, the shortest 3D Euclidean
distance from the finite camera position to the chunk's closed AABB. It selects
the coarse range when:

```text
maximum_geometric_error <= 0.008 * D
```

Equality selects coarse. A camera inside the AABB has `D=0`, so only an
exact-zero-error coarse range can be selected there. Selection is stateless and
has no hysteresis or simulation effect. Nonfinite camera/bounds/error input,
negative error, or unordered bounds fails the frame before submission.

The unchanged smoke start and smoke phases A/B have zero visible LOD0
chunks and 93 visible coarse chunks. Phase C applies the scripted yaw and keeps
all 72 overview chunks coarse from three quarters through seven eighths of the
run. Smoke-only phase D then moves the camera to `(16, -1, 0)` with the same
yaw and pitch for the final eighth; it sees 61 chunks and selects `1/60`
LOD0/coarse. The production smoke therefore keeps both D3D12 surface-index
ranges live while compact and focused selector tests retain broader LOD
coverage. Frustum visibility remains independent of LOD choice, and the same
exact full-resolution AABB is tested for both ranges.

## Sphere-support cyan normal preview

At startup the sandbox samples the proof sphere's fixed X/Z contact site
`(-128,-44)` directly, then casts a downward ray from 50 meters above its body
spawn with a 100-meter limit. Startup fails unless the
sample and ray agree within `0.00001` on position, exact geometric normal,
cell, triangle, barycentrics, and metric distance.

The resulting static marker contains six vertices and six `uint16` indices:

- one 2-meter line from the exact surface hit along its geometric normal; and
- two 0.4-meter crossed lines centered at the normal tip.

Its first endpoint is the exact query position, so the pin rests on the visible
LOD0 triangle. The marker is cyan; the AABB remains magenta. Both use the
terrain line PSO, `LINELIST`, reversed-Z `GREATER_EQUAL`, depth testing, and no
depth writes. Their diagnostic color is encoded through the negative-`Y`
normal sentinel and is independent of daylight. Bounds and the marker are
disabled in ordinary interactive frames; `F4` toggles both together. Smoke
tests deliberately enable them for their first 30 submitted frames and then
disable them. The marker previews the proof sphere's fixed support sample and
normal even while the sphere is airborne; it is not an active-contact
indicator or the slope-shifted physical tangent point.

The active immutable geometry resources pack these exact logical ranges:

```text
vertex range       base vertex   count       end
surface                      0   58,081    58,081
chunk bounds            58,081    1,800    59,881
query marker            59,881        6    59,887
material sphere         59,887      266    60,153

index range        first index   count       end
LOD0 surfaces                0  345,600   345,600
coarse surfaces        345,600  194,400   540,000
chunk bounds           540,000    5,400   545,400
query marker           545,400        6   545,406
material sphere        545,406    1,584   546,990
```

Each visible bounds draw selects one chunk's eight-vertex/24-index range; the
sphere remains packed after the marker. LOD selection changes only which
surface range is submitted. The 58,081 interleaved surface vertices occupy
1,393,944 bytes, and the 540,000 surface indices occupy 1,080,000 bytes. Their
2,473,944-byte total is 2.359 MiB. Chunk bounds plus the query marker add
54,156 bytes of logical diagnostic payload.

After the material sphere is included, the packed terrain vertex resource is
1,443,672 bytes and its index resource is 1,093,980 bytes, for 2,537,652
logical bytes. D3D12 committed allocation rounds those resources to 2,621,440
bytes. These are resource widths and committed allocation, not total startup
upload memory or total process memory.

Keeping every chunk range in those existing resources preserves four total
geometry buffers across the cube and terrain. T-003's three material arrays
and S-003's four HDR environment textures share the one-time static upload,
whose 13 initialization transitions remain unchanged.

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

The renderer-owned production declaration now contains five passes:

1. `Terrain` clears color/depth, draws each visible chunk's selected LOD0 or
   coarse range in the selected solid or wireframe mode, and draws the material
   sphere. When terrain diagnostics are enabled, it also draws each visible
   chunk's magenta AABB and the cyan query marker.
2. `TexturedCube` preserves the attachments and draws the checker cube.
3. `Skybox` reads depth and fills only the far background with procedural
   daylight or HDR radiance.
4. `Water` preserves HDR color, reads canonical terrain depth, and composites
   one procedural six-vertex quad over the local-domain/`rho <= 1` intersection.
5. `ToneMap` reads the HDR scene color and writes the swap-chain back buffer.

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
passes                  5
dependencies            5
emitted transitions     6
elided transitions      34
```

Chunk culling and LOD selection are CPU work before graph execution. All
surface ranges and bounds share the existing two terrain-buffer imports. W-001
adds one pass, two dependencies, three elisions, a PIX scope, and one timestamp
pair without adding an import, emitted transition, descriptor, or water GPU
resource. Let `V0` and `Vc` be the visible LOD0 and coarse chunks, `V = V0 + Vc`,
and `D` be one when diagnostics are enabled and zero otherwise. The `Terrain`
timing interval contains all of these commands:

```text
Terrain indexed draws       V + 4 + D * (V + 1)
full-frame indexed draws     V + 6 + D * (V + 1)
LOD0 terrain indices         1,536 * V0
coarse terrain indices         864 * Vc
material sphere indices      6,336
diagnostic AABB indices      D * 24 * V
diagnostic marker indices    D * 6
textured-cube indices        36
skybox indices               36
```

`Water` issues one non-indexed six-vertex draw generated from `SV_VertexID`, and
`ToneMap` issues one non-indexed fullscreen-triangle draw. With diagnostics off,
the initial/resized `V0=0`, `Vc=93` poses submit 80,352 terrain-surface indices;
the turned `V0=0`, `Vc=72` overview submits 62,208; and the final smoke-only
`V0=1`, `Vc=60` near pose submits 53,376, exercising both packed surface ranges.

The stable PIX hierarchy and timestamp allocation remain:

```text
Frame
  Terrain
  TexturedCube
  Skybox
  Water
  ToneMap

12 timestamps per frame-context slice
36 timestamps across 3 frame contexts
```

Hardware presentation requires 1,000 successful presents. Normal
packaged-WARP Debug retains the 600-frame gate and focused packaged WARP with
GPU-based validation retains 120 frames. Every path exercises both fill modes,
all three material views, both environment modes, the initial and resized
93-chunk views, the turned 72-chunk overview, and the final smoke-only
61-chunk near pose. Terrain diagnostics are enabled only for the first 30
submitted frames. T-007 Debug/Release hardware, normal WARP, and focused GBV
passed the four-phase contract exactly. T-008 passed the same accounting in the
active Debug and Release test runs.

For `F` submitted frames and `V`, `V0`, and `Vc` cumulative visible, LOD0, and
coarse chunks, smoke accounting requires:

```text
terrain_chunk_count == 225
terrain_chunks_tested == 225 * F
terrain_chunks_visible + terrain_chunks_culled
    == terrain_chunks_tested
terrain_chunks_visible == terrain_draw_calls == V
terrain_lod0_draw_calls == V0
terrain_coarse_draw_calls == Vc
terrain_lod0_draw_calls + terrain_coarse_draw_calls
    == terrain_draw_calls
terrain_bounds_draw_calls == 93 * 30
terrain_query_marker_draw_calls == 30
terrain_lod0_indices == 1,536 * V0
terrain_coarse_indices == 864 * Vc
terrain_indices == terrain_lod0_indices + terrain_coarse_indices
terrain_bounds_indices == 24 * terrain_bounds_draw_calls
terrain_query_marker_indices == 6 * terrain_query_marker_draw_calls
terrain_solid_draw_calls + terrain_wireframe_draw_calls == V
terrain_shaded_draw_calls + terrain_material_weight_draw_calls
    + terrain_shading_normal_draw_calls == V

terrain_visible_chunk_min == 61
terrain_visible_chunk_max == 93
terrain_visible_chunk_last == 61
terrain_lod0_chunks_last == 1
terrain_coarse_chunks_last == 60

scene_matrix_changes == 4
sky_matrix_changes == 3

material_sphere_draw_calls == 4 * F
cube_draw_calls == skybox_draw_calls == water_draw_calls
    == tone_map_draw_calls == F
water_vertices == 6 * F
```

The one-per-pass identity uses `pix_terrain_events`, not variable surface draw
count:

```text
pix_terrain_events + cube_draw_calls + skybox_draw_calls + water_draw_calls
    + tone_map_draw_calls == render_graph_pass_executions
```

The T-008 deterministic expected totals remain:

| Gate | Frames | Tested / visible / culled chunks | LOD0 / coarse draws | LOD0 / coarse / total indices |
|---|---:|---:|---:|---:|
| Hardware Debug and Release | 1,000 | 225,000 / 86,375 / 138,625 | 125 / 86,250 | 192,000 / 74,520,000 / 74,712,000 |
| Packaged WARP Debug | 600 | 135,000 / 51,825 / 83,175 | 75 / 51,750 | 115,200 / 44,712,000 / 44,827,200 |
| Packaged WARP Debug + GBV | 120 | 27,000 / 10,365 / 16,635 | 15 / 10,350 | 23,040 / 8,942,400 / 8,965,440 |

| Gate | Solid / wireframe draws | Shaded / weights / normals draws |
|---|---:|---:|
| Hardware Debug and Release | 46,500 / 39,875 | 30,969 / 30,969 / 24,437 |
| Packaged WARP Debug | 27,900 / 23,925 | 18,600 / 18,600 / 14,625 |
| Packaged WARP Debug + GBV | 5,580 / 4,785 | 3,720 / 3,720 / 2,925 |

Every gate also requires 2,790 bounds draws/66,960 bounds indices and 30 marker
draws/180 marker indices. These totals follow from `Q * (2*A + 4*B + C + D)`,
where `Q = F/8` and `A/B/C/D = 93/93/72/61`; only phase D contributes `Q`
LOD0 draws. T-007 verified every row on Debug/Release hardware, normal WARP,
and focused GBV. T-008 verified these locked expectations in both active
configurations.

Static accounting requires 58,081 shared surface vertices, 345,600 LOD0
indices, 194,400 coarse indices, 1,800 chunk-bounds vertices, 5,400
chunk-bounds indices, and six marker vertices/indices. `terrain_index_count`
is 540,000 and `terrain_maximum_geometric_error` is `0.603515625`. W-001 retains
one static upload producing four geometry buffers, three material arrays, and
four HDR environment textures. Its procedural water draw adds no buffer,
texture, resource, descriptor, or upload; active accounting is
`15/5/5/6/34`, 12 timestamps, and five texture-table binds per frame.

Startup still proves two layers, six mips, 36 material subresources, 32,760
meaningful material bytes, 79 environment subresources, 284,608 meaningful
environment bytes, and ten persistent texture descriptors including the
resize-owned HDR scene SRV. The minimize interval in the normal hardware and
WARP gates proves all chunk, draw, and frame-owned counters remain unchanged
while no frame is submitted.

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
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][triangle-bounds]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][height-tile][capacity]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][height-tile][chunks]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][height-tile][lod]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[terrain][lake-basin]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[world][scenario][environment-lab]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[renderer][terrain][culling]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[renderer][terrain][lod]"
& .\out\build\windows-vs2026\bin\Debug\SharkTests.exe "[renderer][terrain][smoke]"
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
small valid cells. The triangle-bounds suite additionally locks exact positions
and normals, row-major/fixed-split order, inclusive interior/shared/maximum
edges, multi-cell filtering, horizontal and vertical misses, invalid input,
and exact repeatability without mutation.

Chunk-layout coverage retains the compact oracle's exact `4x4` partition,
row-major ordering, contiguous ranges, unchanged global vertex references,
once-only cell coverage, fixed triangle order, sample-derived bounds and line
geometry, partial edge chunks, one-chunk fallback, invalid dimensions,
malformed/nonfinite tiles, and the `uint16` vertex ceiling. The capacity
base contract additionally locks the five fixed-point bands, seed/seed step,
nine sample anchors, row-major T-007 FNV-1a checksum, Q8 quantization, exact
extrema and bounds, 25.82421875-meter relief, 115,200 base-triangle slopes below
12 degrees, 11.251308698-degree base maximum slope, nonmatching opposite edges,
and 0.79296875/0.6875-meter base maximum adjacent X/Z steps. The T-008 scenario
coverage separately locks composite checksum `0x4890DE3E1AA063A9`, the
validated spawn-side support component, rejection of non-Q8 tile height samples
or shape/grid inputs, unsafe-extreme, malformed, and open-rim inputs, 530
footprint and 260 rim samples, and byte identity outside the bounded shaping
domain. A successful shape proves one connected lattice footprint inside a
closed canonical-triangle rim.
The same coverage locks the dry
spawn/core/prop gates, 18.681598-degree composite maximum slope,
1.16015625/1.33203125-meter adjacent steps, and 0.603515625-meter measured
coarse error. Both retain 225 complete row-major `16x16` chunks, 1,536/864
per-chunk indices, maximum referenced index 58,080, and the 2.5-MiB
surface-payload budget.

Renderer coverage locks all six Direct3D clip half-spaces, normalized planes,
inside, intersecting, tangent, and outside AABBs, rejection of
degenerate/nonfinite matrices, positive-scale-invariant extraction, ordinary
and extreme-far reversed-Z ranges, and exact active smoke poses:
`93 (0/93)` before and after resize, `72 (0/72)` after the yaw, and the
smoke-only `(16, -1, 0)` near pose at `61 (1/60)` with the same yaw/pitch.

Coarse-layout coverage retains the compact oracle's sixteen center-fan patches
per complete `8x8` chunk, 240 indices, nine boundary samples/eight edge
segments, exact `0.140625`-meter continuous bound, and conservative LOD0
copies for odd/partial chunks. The capacity proof exercises 64 patches and 864
indices in every complete `16x16` chunk, with the active 0.603515625-meter
bound.
LOD-selection
coverage locks shortest 3D camera-to-closed-AABB distance, the inclusive
`error <= 0.008 * distance` threshold, inside/zero-error behavior,
finite-input rejection, and all three active smoke splits.

For manual acceptance, run `SharkSandbox` without arguments. The active
960-meter landscape must read as broad, mostly flat rolling terrain with a
smoothly irregular lake near the scenario spawn. The basin must not resemble a
perfect circle, sharp crater, repeated procedural stamp, or stepped terrace.
Its depth-tested shoreline must follow the canonical terrain, while Fresnel,
depth tint and absorption, bounded environment reflection/refraction, animated
normal-only waves, and sun glint make the surface readable without implying a
fluid simulation. The numeric tests, not visual estimation, own the exact
footprint, depth, rim, and spawn-distance gates. Capture the initial interactive
view plus the resized and turned deterministic smoke views for comparison.
Interactive terrain diagnostics start off; press `F4` and confirm the cyan pin
begins on the terrain and follows its exact triangle normal while the visible
magenta AABBs match the currently logged visible count, partition, and enclose
their chunks.
Rotate away and confirm surface pieces and their matching bounds
disappear together without popping an intersecting chunk. Press `F4` again and
confirm both diagnostic classes disappear without changing surfaces or LOD.
The separate deterministic smoke must begin at
`93 / 225 visible (LOD0=0, coarse=93)`, retain 93 visible after its resize, reach
`72 / 225 visible (LOD0=0, coarse=72)` after its yaw, then finish at the
smoke-only `(16, -1, 0)` pose with
`61 / 225 visible (LOD0=1, coarse=60)`. That final phase does not change the
ordinary interactive start.

In `F1` wireframe, inspect the interactive equal-coarse neighbors and confirm every
shared boundary remains connected without a skirt, T-junction crack, or
missing corner; the final smoke phase also keeps both production index ranges
live, while compact/focused tests retain broader mixed-LOD coverage. Use `F2` to
confirm the shaded ground/rock blend,
complementary material weights, and world-normal visualization. Use `F3` to
compare HDR IBL with procedural daylight and confirm terrain and material
sphere switch coherently. Verify the 32-meter/second camera, four-times sprint,
and 1,500-meter far plane make the full footprint navigable. Move, rotate,
resize, minimize, restore, and close; world-space material tiling, checker
cube, sky, tone mapping, diagnostics, and Direct3D validation must remain
clean. The validation cube and all four spheres must remain dry and unburied
while paused. Once PHY-004 motion runs, bodies 1 and 2 must collide airborne;
primary body 0 must fall and remain supported by the displayed canonical
terrain, and isolated body 3's local material cap must rotate under constant
torque. The cyan preview marks only body 0's fixed supporting sample and exact
triangle normal.
Fly around the complete basin rim and confirm the broad natural terrain resumes
smoothly outside the bounded shaping support. The spawn must remain on dry
ground overlooking the basin.

## Explicit non-goals and continuation

T-008 itself adds no terrain collision response, rigid bodies, additional
visual LOD levels, hysteresis, skirts, morphing, spatial acceleration for
canonical queries or physics, streaming, occlusion culling, generalized world
partition, authored/file-backed material assets, stored mesh UVs/tangents,
arbitrary layer counts, painting, virtual texturing, mesh shaders, shadows,
weather interaction, erosion, water rendering or simulation, editor, dynamic
mouse picking, general debug-draw service, or general scene/mesh/material
resource system. The fixed ray proof, static pin, and chunk bounds are
diagnostics, not gameplay or editor selection. The material sphere is PHY-001's
simulation presentation proxy and lighting proof, not a general entity or mesh
system. The bounded natural fixture is not a general procedural-world system
or a promise that one resident tile is the eventual world-size strategy.

`HeightTileSurface` remains platform-independent and authoritative; material
weights, sampled normals, and shading never feed its height, ray, or collision
answers. T-007 was completed on July 19, 2026 with deterministic Q8 rolling
heights, locked anchors/checksum, 25.82421875-meter relief, all 115,200
triangles below 12 degrees, a 0.1171875-meter coarse-error bound, and dry,
unburied validation props. It retains T-006's 241x241 capacity topology,
225 bounded chunks, 540,000 two-level surface indices, scalable navigation,
and opt-in diagnostics, plus the compact T-005 analytic
oracle, global `R16_UINT` indexing, `15/4/3/6/31` graph contract, four geometry
buffers, three material arrays, four HDR environment textures, ten persistent
texture descriptors, four per-frame texture-table binds, and the existing
PIX/timestamp hierarchy. The then-active four-phase T-007 smoke passed on
Debug/Release hardware, normal WARP, and focused 120-frame GBV with exact
accounting, zero corruption/errors, and zero live child objects.

T-008 composes that historical oracle with scenario-owned lake metadata and a
bounded dry indentation. Its focused lake, scenario, culling, and LOD tests pass
with the counts recorded above, and both complete active test runs pass
`150/150`. It changes no San Andreas-class feature ceiling, and the visual-rain
track remains deferred.

W-001 consumes that immutable metadata with one procedural six-vertex flat quad
centered at `(-128,-4,-128)` and conservative `64/56`-meter half extents. The
shader intersects that local domain with exact `rho <= 1` using T-008's
`56/48`-meter nominal semi-axes and the X/Z warp offsets and divisors
`1152/512` and `1568/1024`, selecting the intended spawn-side component rather
than treating the inequality as globally bounded. Reversed-Z terrain depth
produces the visible shoreline. After sky rendering, premultiplied HDR
transmission/tint, analytic depth-proxy absorption, Fresnel, bounded environment
reflection/refraction, animated normal-only waves, and sun glint provide a
visual surface without canonical terrain mutation or simulated fluid state.

PHY-002 consumes `HeightTileSurface` directly after each ballistic prediction.
It uses the exact LOD0 point and geometric triangle normal, applies a vertical
one-sample face-plane correction without mutating terrain, and never reads the
coarse visual LOD or a D3D12 resource. A center on the maximum tile edge is
supported; a center immediately outside has no contact even when its radius
overlaps the edge. Closest-feature edge collision, continuous collision, and
two-sided terrain collision remain outside this proof. PHY-007 now submits the
post-projection sphere witness and exact face normal to the shared contact
solver, so material friction can exchange linear and angular velocity while
the zero-penetration constraint preserves the same X/Z query ownership. The
active queue remains centralized in [ENGINE_PLAN.md](ENGINE_PLAN.md).

PHY-003 repeats that terrain step for four fixed-index bodies before testing
the six possible sphere pairs in lexicographic order. Pair response remains
CPU-only and does not read or mutate terrain, its visual LODs, or GPU
resources. PHY-004 supplies orientation and angular velocity; PHY-007 uses both
for contact-point velocity and world inverse inertia before the existing
external-torque update.

PHY-005 adds the bounded finite-segment closest-feature query for pure capsule
contacts. PHY-006 adds only the bounded exact-triangle candidate query used by
pure oriented-box SAT; it does not add a physics acceleration structure or
mutate canonical terrain. Those capsule and box queries remain response-free
until a runtime body adapter needs them. The active queue is `PHY-008` manifold
persistence and warm starting and remains centralized in
[ENGINE_PLAN.md](ENGINE_PLAN.md).
