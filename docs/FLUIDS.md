# CPU Shallow-Water Reference Solver

W-002 establishes Shark's first simulated-fluid state, W-003 adds its first
real-time advance, and W-004 adds conservative wet/dry-front handling. Together
they form a small, platform-independent numerical oracle; they do not replace
W-001's visual lake or submit work to Direct3D 12.

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
negative. Exact dry state is representable only as `h = hu = hv = +0`.
Factory operations canonicalize signed zero and never mutate their input spans.

W-004 classifies solver state with one positive finite dry-depth threshold
`epsilon`:

```text
exact dry:       h == 0
retained film:   0 < h <= epsilon
active:          h > epsilon
```

The default is the binary-exact `epsilon = 2^-20` meters, approximately
`0.953674` micrometers. Classification never deletes depth. A retained film
keeps its complete water volume and participates in hydrostatic reconstruction
and pressure, while its velocity and `hu/hv` are projected to exact positive
zero. A shared face flux can raise it strictly above `epsilon` and reactivate
it.

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
preserve. W-003 now advances that uneven-bed fixture without spurious flow
within the documented floating-point tolerance.

## Conservative wet/dry advance

`advance_shallow_water_reference_grid` evolves the conserved state
`U = (h, hu, hv)` with an unsplit, first-order finite-volume update. Every
cardinal face is evaluated once, so its mass flux is shared by both adjacent
cells.

The interface scheme uses:

- hydrostatic reconstruction against the higher of the two face beds;
- a local Lax-Friedrichs/Rusanov flux for the homogeneous shallow-water
  equations;
- side-specific hydrostatic pressure corrections for the bed source; and
- reflective solid-wall ghosts at the rectangular boundary.

Those choices preserve a fully wet lake at rest over the canonical uneven
terrain fixture. They are intentionally diffusive and small-scale; higher-order
reconstruction and limiters are not part of this CPU oracle.

Each substep recomputes the maximum X and Z signal rates after dividing by cell
spacing. The sum is limited to `CFL <= 0.5` (`0.45` by default). The final
substep lands on the exact requested interval, while an explicit budget prevents
unbounded catch-up work.

The operation is transactional. It advances a complete scratch grid, validates
every substep, and replaces the caller's grid only after the requested interval
and final diagnostics succeed. Nonfinite arithmetic, negative depth,
conservation failure, or budget exhaustion returns an error without a depth
clamp and without changing the input grid.

Hydrostatically dry reconstructed faces are valid. Reconstruction subtracts the
two bed elevations before adding depth, preserving small depth over a very
large equal bed and treating a negative-infinite bed step (an unrepresentably
large upward barrier) as a blocked zero-depth side. During reconstruction,
division by depth occurs only for active cells. Near-dry momentum is projected
after every accepted substep, and the absolute integrated amount removed on
each axis is reported rather than hidden.

An exactly dry domain has zero projected momentum and zero face flux. It can
therefore complete one no-op substep even when `dt / dx` would overflow or
underflow. A rounded zero CFL rate is not enough to take that path: a permanent
range test proves that a wet face with a nonzero mass flux is still advanced
when `signal / dx` underflows to zero.

## Solid boundaries

An interface query accepts one active cell and one cardinal face. An interior
face returns the adjacent cell unchanged. At the rectangular domain edge it
synthesizes one reflective ghost:

- bed and depth are copied;
- tangent momentum is copied;
- normal momentum is negated; and
- exact zero remains positive zero.

There is no stored ghost ring, diagonal corner query, open boundary, inflow,
outflow, periodic boundary, or internal obstacle mask through W-004.

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

W-003 turns volume into an enforced ledger:

```text
residual = final volume - initial volume + net outward boundary volume
```

The report also records cumulative absolute face transport, substep extrema,
maximum observed CFL, final depth/momentum extrema, and the maximum intermediate
ledger residual. W-004 adds the chosen threshold; initial/final active,
retained-film, and exact-dry counts; cumulative activation/deactivation events;
final retained-film volume; and cumulative absolute integrated momentum
discarded by the near-dry projection. Transition counts are per accepted
substep, not unique-cell counts.

Success requires every volume residual to fit a published, scale- and
operation-aware floating-point tolerance. Reflective fixtures have exactly
zero outward boundary volume. Projected momentum is intentionally not claimed
as conserved; the report measures that numerical policy separately.

## Verification

The permanent `[fluids][shallow-water]` suite covers the W-002 state contract
and the W-003/W-004 advance, including:

- `1 x 1` and exact `8 x 8` capacity;
- row-major storage, representable cell centers, canonical signed-zero/tail
  state, and repeat construction;
- configuration, span, finite-state, overflow, dry-state, and corruption
  rejection;
- the fully wet lake-at-rest fixture over uneven canonical terrain;
- every perimeter face plus nonzero normal/tangent wall momentum;
- one-cell-domain reflective behavior without negative zero; and
- exact analytic volume and integrated-momentum baselines;
- analytic non-unit-spacing Rusanov flux and reflective wall impulses;
- well-balanced uneven-bed lake and sealed dam-break evolution;
- X/Z transpose symmetry, tangential transport, and nonlinear `8 x 8`
  capacity;
- CFL partitioning and two-axis, reciprocal, and flux range edge cases;
- nonnegative depth, deterministic clones, report/diagnostic agreement, and
  enforced volume accounting; and
- exact-dry no-op range extremes and nonzero transport at a rounded-zero CFL
  rate;
- strict threshold classification at adjacent floating-point values;
- initial and substep-generated momentum projection with non-unit cell area;
- exact-dry activation, retained-film retreat, and two-dimensional wet-front
  symmetry;
- a partially wet uneven-bed lake-at-rest shoreline;
- deterministic mixed-front evolution and sealed-domain mass accounting; and
- invalid, overflow, substep-budget, and post-work transactional rollback.

Debug and Release W-004-focused runs each pass 10,118 assertions across 13
cases. The combined fluid filter passes 12,679 assertions across 41 cases, and
both complete CPU configurations pass 492,415 assertions across 321 cases. No
GPU smoke is required because W-004 changes no sandbox, renderer, shader,
resource, descriptor, draw, or D3D12 path.

## Next boundary

WQ-001 completed a separate CPU gameplay-water boundary over authored calm
water and canonical LOD0 terrain. It does not advance or consume this numerical
solver, and the solver owns no character immersion policy. The shared future
adapter shape is surface height, bed height, nonnegative depth, and optional
horizontal flow; see [WATER.md](WATER.md).
`W-005` may later port fixed-step batches to D3D12 ping-pong compute resources
and must match this CPU oracle within documented tolerances. GPU
simulated-water rendering, rain coupling, and hydrology remain approved
deferred specializations. CHR-001 does not consume this solver; the active
queue is `CAM-001`.
