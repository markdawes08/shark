# Gameplay Water Query Contract

- **Completed through:** `WQ-001`
- **Character integration completed through:** `CHR-006`
- **Last verified:** July 26, 2026
- **CHR-006 verification:** Debug and Release each passed `612,172` assertions
  across `410` cases; focused Debug passed `22,221` assertions across `10`
  surface-swimming cases; RTX 4070/WARP smokes passed `1,000/600` frames

WQ-001 adds a small platform- and renderer-independent `shark::water` boundary
for character-facing calm-water queries. It depends only on Core and Terrain.
It owns no renderer material, Direct3D object, GPU readback, fluid solver state,
or character movement policy.

## Authored body

`CalmWaterBody` records:

- one deterministic `terrain::IslandFootprint`;
- whether water occupies the inside or outside of its warped `rho == 1`
  boundary;
- one equilibrium surface height;
- one positive shoreline depth tolerance; and
- optional horizontal X/Z flow.

The Island Demo authors outside support, surface height `-4` meters, the
binary-exact `1/256`-meter default tolerance, and a present positive-zero flow.
Its presentation-only depth proxy and water-quad extents remain outside this
gameplay body.

The scenario owns the neutral support side. The sandbox composition root maps
it explicitly to `renderer::WaterSurfaceSupport`; Water never includes or
returns a renderer type.

## Query result

`query_gameplay_water` accepts a finite world X/Z point and the validated
canonical `HeightTileSurface`. It returns one explicit disposition:

| Disposition | Meaning | Numeric contract |
|---|---|---|
| `out_of_terrain` | The finite X/Z point has no canonical terrain sample | Authored surface remains available; support is not evaluated and reports false; bed/depth are positive zero; flow is absent |
| `no_water` | Terrain exists, but horizontal support is false or depth is at/below tolerance | Surface and canonical triangle-interpolated bed are reported; depth is positive zero; flow is absent |
| `water` | Horizontal support is true and `surface - bed > tolerance` | Surface, bed, checked positive depth, and the body's optional flow are reported |

Inside support uses `rho <= 1`; outside support uses `rho >= 1`, matching the
visual shader's inclusive clip boundary. The CPU calculation uses the shared
terrain footprint function rather than duplicating its polynomial.

The depth test is deliberately strict. A raw depth exactly equal to the
shoreline tolerance remains `no_water`; the immediately deeper representable
case is `water`. This conservative gameplay strip absorbs small
triangle-interpolation differences. CPU double evaluation and rasterized
shader float evaluation are not claimed to be bit-identical on the analytic
support curve; Character's wading and surface-swimming hysteresis policies
belong to Character, not this stateless query.

Invalid body fields, unknown support values, nonpositive tolerance, nonfinite
flow, or nonfinite query coordinates return a Simulation `invalid_argument`
error. A finite terrain miss is an ordinary result, not an error. Derived
nonfinite or unrepresentable support/depth fails without partial output.

## Character consumption through CHR-006

The sandbox evaluates this CPU query once per emitted fixed tick at the
tick-start authoritative capsule X/Z, after advancing the authoritative orbit
and before advancing Character. Catch-up frames therefore obtain a fresh
result for each simulation tick. Render interpolation, water pixels, and GPU
state never participate.

Character consumes only `GameplayWaterQuery`; it does not receive
`CalmWaterBody` or Water's analytic containment policy. A supported grounded
or landing character enters wading at depth `>= 0.25 m`, remains wading while
depth is greater than `0.125 m`, and exits at depth `<= 0.125 m`. The query's
bed must exactly match Character's canonical LOD0 terrain support at the same
source position. A malformed or inconsistent observation fails the Character
advance transaction.

The DTO does not carry source X/Z. Querying at the authoritative tick-start
position is consequently a composition-root contract; exact bed matching is a
defensive check, not complete provenance for equal-height terrain points. The
sandbox satisfies the contract by querying immediately before each Character
advance.

Supported wading scales movement linearly from multiplier `1.0` at `0.25 m`
to `0.5` at `1.5 m`. At depth `>= 1.50 m`, supported wading enters surface
swimming unless a same-tick wading jump wins first. An existing swimmer remains
one while depth is greater than `1.25 m`. Its center is
`max(surface height - 0.50 m, canonical support)`, and camera-relative
directional motion uses one `3 m/s` speed. Shift and Space are ignored while
swimming. Ground acceleration, braking, facing, and bounded terrain probes are
reused. Character stores the consumed surface height in a surface-swimming
snapshot; dry and wading Character states keep that field at canonical
positive zero.

Descending airborne motion with a sufficiently deep observation can be
captured at the surface-center target; rising motion remains unchanged.
Walkable shallow exit snaps to exact support and publishes wading when the same
source still meets wading hysteresis, otherwise dry. Steep exit publishes dry
steep contact. A swimmer that receives `no_water` snaps grounded/dry only when
already at or above support and within ground-snap distance; otherwise it
falls dry with preserved horizontal momentum. `out_of_terrain` or missing
canonical support uses collapsed spawn recovery.

The optional horizontal flow remains part of the adapter-neutral result but is
ignored. A step that crosses the shoreline or another water boundary is
reclassified on the next emitted fixed tick because the result describes the
tick-start position. Terrain probing retains a safe prefix but performs no
per-probe Water query. No Character operation mutates the body, water volume,
renderer input, fluid state, or GPU state. Reset wins over a wet source
observation and collapses the canonical dry spawn.

## Island Demo proof

The permanent fixture verifies:

- the dry, shallow, transition, and swim transect depths
  `0`, `0.33984375`, `1.359375`, and `5.734375` meters;
- exact inclusive inside/outside footprint boundaries and their adjacent
  representable neighbors;
- unsupported water over a submerged bed;
- terrain above, equal to, and immediately around the depth tolerance;
- inclusive canonical terrain edges and one-float-step exterior misses;
- absent, present positive-zero, and nonzero flow, including suppression
  outside active water;
- malformed bodies and coordinates;
- finite terrain misses and checked support/depth range failures;
- adjacent representable Island Demo shoreline samples; and
- deterministic repeated queries with unchanged terrain.

Historically, at the WQ-001 checkpoint, focused Debug and Release verification
each passed 362 assertions across eight cases, while both complete
configurations passed 492,868 assertions across 335 cases. The current complete
verification record is in [BUILDING.md](BUILDING.md).

## Non-goals

WQ-001 itself still owns no capsule thresholds, movement modes, character
state, buoyancy, water displacement, dynamic waves, GPU readback, fluid
coupling, or renderer change. CHR-006 owns the Character policy described
above; a later simulated-water adapter may provide the same query information
without changing that ownership.

The query still carries no source X/Z or tick provenance. Exact bed agreement
detects many misplaced observations but cannot distinguish stale results over
equal-height terrain. The sandbox's immediate tick-start query remains the
binding contract. Underwater movement/combat, swim jumping, current response,
animation, dynamic-water smoothing, per-probe Water queries, and GPU
synchronization remain deferred. CAM-001 remains presentation only. The active
queue is `AVT-001`, placeholder avatar presentation. The authoritative
increment order remains in [ENGINE_PLAN.md](ENGINE_PLAN.md), and the player
contract is in [CHARACTER.md](CHARACTER.md).

CHR-006 focused Debug surface-swimming verification passed `22,221` assertions
across `10` cases, and the complete Debug and Release suites each passed
`612,172` assertions across `410` cases. The final Debug RTX 4070 and
packaged-WARP presentation smokes passed `1,000/600` frames; each end-of-run
validation reported zero D3D12 corruption, zero errors, zero live D3D12 child
objects, and only the two expected device-level RLDO advisory warnings.
