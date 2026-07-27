# Gameplay Water Query Contract

- **Completed through:** `WQ-001`
- **Character integration observed through:** `CHR-005`
- **Last verified:** July 26, 2026

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
support curve; CHR-005's character-state hysteresis belongs to Character, not
this stateless query.

Invalid body fields, unknown support values, nonpositive tolerance, nonfinite
flow, or nonfinite query coordinates return a Simulation `invalid_argument`
error. A finite terrain miss is an ordinary result, not an error. Derived
nonfinite or unrepresentable support/depth fails without partial output.

## CHR-005 consumption

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
to `0.5` at `1.5 m`; deeper observations remain clamped at `0.5`. The optional
horizontal flow remains part of the adapter-neutral result but CHR-005 ignores
it. A step that crosses the shoreline is reclassified on the next emitted
fixed tick because the result describes the tick-start position.

Jumping, airborne motion, steep contact, reset, and recovery publish dry
Character water state. Reset wins over a wet source observation. No Character
operation mutates this body, water volume, renderer input, or fluid state.
CHR-005 performs no per-probe water query and creates no deep-water barrier;
deep-bed walking temporarily remains possible at the `0.5` multiplier until
CHR-006 adds surface swimming.

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

At the WQ-001 checkpoint, focused Debug and Release verification each passed
362 assertions across eight cases, while both complete configurations passed
492,868 assertions across 335 cases. The current complete verification record
is in [BUILDING.md](BUILDING.md).

## Non-goals

WQ-001 itself still owns no capsule thresholds, movement modes, character
state, buoyancy, water displacement, dynamic waves, GPU readback, fluid
coupling, or renderer change. CHR-005 owns the wading policy described above;
a later simulated-water adapter may provide the same query information without
changing Character policy.

Surface-relative buoyancy and surface-swim entry/exit remain outside CHR-005.
No Character action mutates water volume, and CAM-001 remains presentation
only. The active queue is `CHR-006`, surface swimming. The authoritative
increment order remains in [ENGINE_PLAN.md](ENGINE_PLAN.md), and the player
contract is in [CHARACTER.md](CHARACTER.md).
