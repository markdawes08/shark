# CPU Shallow-Water Reference

W-002 establishes Shark's first simulated-fluid state. It is a small,
platform-independent numerical oracle; it does not replace W-001's visual lake
or submit work to Direct3D 12.

## State contract

`shark::fluids::ShallowWaterReferenceGrid` owns at most `8 x 8` square cells.
The fixed 64-cell storage is allocation-free and has one row-major active
prefix (`index = z * columns + x`) followed by an exact positive-zero tail.
The lower-left domain corner and cell spacing are measured in meters.

Each active cell separates:

- bed elevation `b`, in world-space meters;
- water depth `h`, in meters;
- X momentum `hu`, in square meters per second; and
- Z momentum `hv`, in square meters per second.

The momenta are depth-integrated conserved variables, not kilogram momentum.
All CPU reference values are `double`. A later float GPU solver must match this
oracle within documented tolerances.

Construction rejects malformed dimensions, spans, origins, spacing, extents,
bed values, depth, momentum, free-surface values, velocity, per-cell volume,
pressure scale, integrated momentum, or advective flux scale. Depth cannot be
negative. Exact dry state is representable only as `h = hu = hv = +0`; W-002
does not activate or deactivate dry cells. Factory operations canonicalize
signed zero and never mutate their input spans.

## Uneven terrain and lake at rest

The permanent W-002 fixture derives each fluid bed value from the exact
area-average of one canonical LOD0 terrain cell. Shark's two equal-area
triangles use heights `(b00,b01,b11)` and `(b00,b11,b10)`, so:

```text
b = (2*b00 + b01 + b10 + 2*b11) / 6
```

This is derived simulation data from canonical `TerrainData`; it never reads a
render mesh or coarse visual LOD and never mutates terrain.

The lake-at-rest factory requires one finite waterline strictly above every bed
cell. It creates a fully wet state with `h = waterline - b` and exact-zero
momenta. Diagnostics then prove constant `b + h`, zero per-cell momentum, and
repeatable volume over an uneven canonical terrain fixture.

This establishes the hydrostatic state that a well-balanced solver must
preserve. W-002 has no time update, numerical flux, or bed-slope source term, so
it does not falsely claim that a scheme has advanced the fixture. That proof is
the first acceptance gate in W-003.

## Solid boundaries

An interface query accepts one active cell and one cardinal face. An interior
face returns the adjacent cell unchanged. At the rectangular domain edge it
synthesizes one reflective ghost:

- bed and depth are copied;
- tangent momentum is copied;
- normal momentum is negated; and
- exact zero remains positive zero.

There is no stored ghost ring, diagonal corner query, open boundary, inflow,
outflow, periodic boundary, or internal obstacle mask in W-002.

## Diagnostics

Inspection revalidates the public record before accumulating deterministic
row-major baselines:

- active, wet, and dry cell counts;
- minimum and maximum depth;
- wet-cell free-surface extrema;
- water volume `sum(h * dx^2)`, in cubic meters;
- integrated X/Z momenta `sum(hu * dx^2)` and `sum(hv * dx^2)`, in
  `m^4/s`; and
- maximum absolute per-cell X/Z momentum.

The volume is a conservation baseline, not yet a time ledger. W-003 will compare
pre-step and post-step values after adding an actual conservative update.

## Verification

The permanent `[fluids][shallow-water]` suite covers:

- `1 x 1` and exact `8 x 8` capacity;
- row-major storage, representable cell centers, canonical signed-zero/tail
  state, and repeat construction;
- configuration, span, finite-state, overflow, dry-state, and corruption
  rejection;
- the fully wet lake-at-rest fixture over uneven canonical terrain;
- every perimeter face plus nonzero normal/tangent wall momentum;
- one-cell-domain reflective behavior without negative zero; and
- exact analytic volume and integrated-momentum baselines.

Debug and Release focused runs each pass 498 assertions across 12 cases. Both
complete CPU test configurations pass 480,234 assertions across 292 cases.
No GPU smoke is required because W-002 changes no sandbox, renderer, shader,
resource, descriptor, draw, or D3D12 path.

## Next boundary

W-003 may add only conservative wet-cell fluxes, well-balanced bed-source
handling, CFL-limited substeps, positivity checks, a sealed dam-break fixture,
and volume accounting across real updates. Wet/dry-front activation remains
W-004; rain coupling, GPU compute, and simulated-water rendering remain later
increments.
