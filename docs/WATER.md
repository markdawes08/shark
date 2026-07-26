# Gameplay Water Query Contract

- **Completed through:** `WQ-001`
- **Character integration observed through:** `CHR-004`
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
support curve; later character-state hysteresis belongs to `CHR-005`, not this
stateless query.

Invalid body fields, unknown support values, nonpositive tolerance, nonfinite
flow, or nonfinite query coordinates return a Simulation `invalid_argument`
error. A finite terrain miss is an ordinary result, not an error. Derived
nonfinite or unrepresentable support/depth fails without partial output.

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

Focused Debug and Release verification each pass 362 assertions across eight
cases. Both complete configurations pass 492,868 assertions across 335 cases;
the complete verification record is in [BUILDING.md](BUILDING.md).

## Non-goals

WQ-001 adds no capsule immersion thresholds, wading/swimming modes, character
state, buoyancy, water displacement, dynamic waves, GPU readback, fluid
coupling, or renderer change. A later simulated-water adapter may provide the
same query information without changing character policy.

CHR-004 lets one bounded capsule walk, jump, steer in air, land, and recover
against canonical terrain but still does not query water during fixed-tick
advancement or own immersion policy. It can therefore continue along, jump
over, or land on submerged terrain as a temporary limitation. No Character
action mutates water volume. CAM-001 changes only its presentation camera.
The active queue is `CHR-005`, where WQ-001 first enters Character policy for
shallow-water wading. The authoritative increment order remains in
[ENGINE_PLAN.md](ENGINE_PLAN.md), and the player contract is in
[CHARACTER.md](CHARACTER.md).
