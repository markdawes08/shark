# Fixed-Step Rigid-Body and Contact Contract

- **Completed through:** `PHY-006`
- **Last verified:** July 22, 2026

PHY-001 established Shark's fixed-clock ballistic path. PHY-002 gives that one
sphere a one-meter collider and deterministic canonical-terrain support.
PHY-003 expands the Environment Lab to a stable fixed array of four equal-unit-
mass spheres and resolves their overlaps with restitution impulses. This is a
bounded discrete collision proof. PHY-004 adds normalized orientation, angular
velocity, explicit solid-sphere inertia, torque integration, and render
interpolation without yet coupling angular motion to contacts. It remains a
focused rigid-body foundation rather than a general solver. PHY-005 adds a
finite capsule shape and pure analytic contacts against canonical terrain,
spheres, and other capsules. It deliberately generates contact data without
yet changing velocities or positions. PHY-006 adds checked oriented boxes and
fixed-capacity contact manifolds against boxes and exact canonical terrain,
also as pure CPU queries without response.

## Ownership and data flow

The boundaries are intentionally narrow:

- `Simulation` owns fixed-step time accounting, pause state, and step requests;
- `Physics` owns the fixed four-body capacity, rigid state, solid-sphere mass
  properties, linear/angular integration, colliders, canonical-terrain
  response, sphere-pair response, capsule closest-feature contacts,
  oriented-box SAT/manifold queries, contact records, and interpolation;
- `World` publishes four deterministic scenario-owned body spawns, initial
  linear/angular states, equal mass, and external torques beside the lake;
- the sandbox composition root sequences input, fixed ticks, immutable
  previous/current snapshots, and render interpolation; and
- `Renderer` receives only an active count and four interpolated sphere
  positions/orientations.

Neither the clock nor rigid body includes Win32 or Direct3D types. The
platform continues to publish raw events, and the renderer cannot advance or
mutate simulation state. Physics depends on the platform-independent
`HeightTileSurface` query contract; Terrain has no dependency on Physics.

## Fixed clock

The production clock runs at exactly 60 ticks per second. It accumulates
elapsed render time in an integer nanosecond phase, emits zero or more fixed
steps, and publishes an interpolation alpha in `[0, 1]`. One render update
accepts at most 250 milliseconds; any excess is reported as discarded time
instead of causing an unbounded catch-up.

The interactive sandbox starts paused. Paused frames discard wall-clock elapsed
time and render the current snapshot with alpha `1`. Changing pause state clears
the partial accumulator, so time spent paused cannot become a later burst of
simulation work. The deterministic presentation-smoke path instead runs from
startup and supplies one fixed-step ceiling duration per frame.

| Input | Simulation action |
|---|---|
| `F5` | Toggle pause/resume on a non-repeat key press |
| `F6` | While paused, request exactly one 60 Hz tick |

A single-step request is consumed once. It does not resume the clock or carry
render-frame elapsed time into the simulation.

## Sphere body snapshots

The Environment Lab publishes an exact active count of four, all with a
one-meter radius. Body 0 remains the original zero-velocity terrain-rest proof.
Bodies 1 and 2 start at the same airborne Y coordinate, 12 meters apart, with
X velocities of `+5` and `-3` meters per second. Body 3 is an isolated
zero-velocity fall driven by a constant world-space torque of
`(0, 0, 0.2)` newton-meters. Every body has one-kilogram mass, one-meter
radius, identity orientation, zero initial angular velocity, and gravity
`(0, -9.81, 0)` meters per second squared.

For fixed delta `dt = 1/60` second, semi-implicit Euler advances:

```text
velocity = velocity + gravity * dt
position = position + velocity * dt
```

Before each tick, the sandbox copies the entire fixed current-state array to
the previous-state array. It advances the active current states in stable body
index order, resolves pairs, advances angular motion, and publishes no partial
state. Rendering interpolates each previous/current pair using the clock alpha
and never mutates either authoritative snapshot.

## Angular rigid-body state

`RigidBodyState` contains position, a quaternion orientation `(x,y,z,w)`,
linear velocity, and world-space angular velocity. Identity is `(0,0,0,1)`.
Accepted simulation states must be finite and unit-oriented within the bounded
normalization tolerance. A solid sphere publishes explicit mass, inverse mass,
radius, moment of inertia, and inverse moment:

```text
I = 2/5 * mass * radius^2
```

The Environment Lab's one-kilogram, one-meter spheres therefore use
`I = 0.4 kg*m^2`. Construction rejects nonpositive/nonfinite inputs and
nonrepresentable derived values. With torque `T`, fixed delta `dt`, and the
isotropic sphere inertia, one angular tick is:

```text
angularAcceleration = T / I
angularVelocity     = angularVelocity + angularAcceleration * dt
deltaOrientation    = axisAngle(normalize(angularVelocity),
                                length(angularVelocity) * dt)
orientation         = normalize(deltaOrientation * orientation)
```

The update is transactional and uses the new angular velocity, matching the
linear semi-implicit convention. The exact axis-angle increment avoids
first-order quaternion drift; deterministic normalization still enforces the
unit contract. World-space angular velocity left-multiplies the increment.
Zero torque preserves angular velocity, and zero torque plus zero angular
velocity preserves an already normalized orientation.

Render interpolation linearly blends position and both velocities while using
normalized shortest-path quaternion interpolation. Quaternion sign equivalence
is handled before interpolation, and rendering cannot write the authoritative
previous/current simulation arrays.

## Canonical terrain contact

After each ballistic prediction, Physics samples
`HeightTileSurface::sample_lod0_surface` exactly once at the predicted center's
X/Z position. The returned position, exact geometric triangle normal, cell,
triangle, and barycentrics are the contact source. Smooth render normals,
coarse visual LOD, render meshes, and GPU resources never participate.

For predicted center `C`, canonical point `P`, unit face normal `N`, and radius
`r`, the signed sphere/plane separation is:

```text
separation = dot(C - P, N) - r
```

A separation above the 0.00001-meter contact tolerance remains ballistic. At
or below the tolerance, the solver changes only center Y:

```text
supportedCenterY = P.y + r / N.y
```

This places the sphere exactly one radius from the selected triangle plane
without changing X/Z and therefore without switching query ownership during
resolution. The corrected state is committed only after every calculation
succeeds. A center outside the tile continues ballistically with no contact;
the maximum X/Z edges retain the terrain query's inclusive ownership.

The terrain endpoint still gives each contacting sphere a temporary
infinite-friction endpoint
projection: every linear-velocity component becomes zero at contact. That
deliberately feature-limited rule makes the sphere settle on the Environment
Lab terrain without bounce, penetration, or drift; its correction/impulse
magnitude is not a general bounded material law. It is not the later
restitution/friction/contact-constraint model. Each fixed tick republishes an
optional contact containing the untouched canonical sample, pre-resolution
separation, and penetration depth.

## Sphere-pair collision

After every active sphere has completed ballistic prediction and terrain
support, Physics tests the active pairs exactly once in lexicographic order:

```text
(0,1), (0,2), (0,3), (1,2), (1,3), (2,3)
```

The fixed capacity is four bodies and six possible pairs. There is no dynamic
allocation and no broad phase. For centers `A` and `B`, radii `rA` and `rB`,
and first-to-second normal `N`:

```text
offset      = B - A
overlap     = rA + rB - length(offset)
N           = normalize(offset)
```

Pairs with negative overlap are separated. Touching and overlapping pairs
publish a contact. Coincident centers use `+X` as a deterministic,
index-ordered fallback normal. Positive overlap is projected equally between
the equal-unit-mass bodies:

```text
A = A - N * overlap / 2
B = B + N * overlap / 2
```

With relative velocity `Vrel = Vb - Va`, an impulse is applied only when
`dot(Vrel, N) < 0`:

```text
impulse = -(1 + restitution) * dot(Vrel, N) / 2
Va      = Va - N * impulse
Vb      = Vb + N * impulse
```

The Environment Lab restitution is `0.75`. Tangential velocity is untouched,
and equal-unit-mass linear momentum is preserved. Each contact records stable
body indices, normal, penetration, incoming relative normal velocity, and
normal-impulse magnitude. Invalid counts, states, radii, restitution, or
non-representable results fail transactionally without changing the input
array.

The first Environment Lab pair collision is deliberately airborne. Therefore
the temporary terrain-sticking response cannot erase its proof impulse during
that tick, while isolated body 0 continues to prove canonical support. Pair
projection is a single discrete pass; it does not iterate a stack to
convergence and it cannot prevent fast bodies from tunneling.

## Capsule closest-feature contacts

`CapsuleCollider` contains a finite positive radius and a finite local
half-segment. Its centerline endpoints are the rigid-body position plus and
minus that vector after unit-quaternion rotation. A zero half-segment is valid
and intentionally reduces the capsule to a sphere. Endpoint construction and
all contact calculations use checked finite intermediates; invalid state,
orientation, radius, or local coordinates return `invalid_argument`, while a
nonrepresentable result returns `unavailable`.

The three PHY-005 entry points are read-only queries:

- capsule/terrain returns the exact canonical terrain sample and the closest
  capsule-axis point;
- capsule/sphere returns the closest capsule-axis point and sphere center; and
- capsule/capsule returns the closest points on both finite axes.

Each successful contact records a unit normal, signed separation, and
penetration depth:

```text
separation       = featureDistance - combinedRadius
penetrationDepth = max(0, -separation)
```

Exact touching and separations no greater than `0.00001` meter produce a
contact; a cleanly separated pair succeeds with an empty optional. Primitive
pair normals point from the first shape toward the second. Coincident
primitive features use a deterministic axis-derived perpendicular, an axis
cross product for crossed capsules, or `+X` only when both axes degenerate.

Capsule/terrain does not approximate the centerline with endpoint height
samples. Terrain owns a bounded exact segment-versus-LOD0-triangle query that
checks intersection, endpoint/face, and segment/edge candidates over the
expanded X/Z cell range. It returns both witnesses, the segment parameter, and
the selected triangle's exact cell, fixed split, normal, and barycentrics.
Equal-distance candidates retain row-major cell order and fixed triangle
order. Terrain contact normals stay in the canonical upward face-normal
hemisphere; exact coincidence uses the selected face normal.

These functions generate no positional correction, impulse, torque, friction,
or manifold. They do not mutate either body or terrain and are not called by
the current four-sphere Environment Lab. That separation allowed PHY-006 to
build manifolds and lets PHY-007 introduce one shared response path instead of
adding a second temporary solver.

## Oriented-box contact manifolds

`BoxCollider` owns finite strictly positive local half-extents. The checked
world-geometry query normalizes the existing rigid orientation in double
precision, derives its three orthogonal axes, emits all eight corners in XYZ
sign-bit order, and computes an inclusive world AABB. Invalid input returns
`invalid_argument`; nonrepresentable axes, vertices, bounds, or extents that
collapse when stored as floats return `unavailable`.

Box/box contact tests the complete 15-axis separating-axis set in fixed order:
the first box's three face axes, the second box's three face axes, then nine
axis-pair cross products. Box/terrain tests each exact candidate triangle's
face normal, the box's three face axes, and nine normalized box-axis/triangle-
edge cross products. Near-parallel axes are skipped with a relative threshold,
and axes tied within `0.00001` meter retain the earlier feature. Clean
separation succeeds with an empty optional; touching within that same tolerance
produces a contact.

A box/box normal points from the first box toward the second. Face winners clip
the incident face against the reference face; edge winners use the closest
finite edge pair. The resulting fixed-capacity manifold keeps at most four
ordered contacts, each with two witnesses, their midpoint, signed separation,
and nonnegative penetration depth. Stable feature and axis indices make the
chosen SAT feature directly testable. Swapping the two boxes reverses normals
and witness ownership for noncoincident pairs while retaining the same
geometric result; ambiguous coincident pairs retain their canonical tied-axis
fallback.

Box/terrain first requests only canonical triangles whose inclusive 3D bounds
overlap the box AABB expanded by contact tolerance. It tests candidates in
Terrain's stable row-major/fixed-split order and selects the deepest manifold;
tied candidates retain the earlier owner. Each terrain witness records the
exact cell, fixed triangle, normal, barycentrics, and point. Normals are kept in
the selected face's upward hemisphere when not tangent. Near-tangent axes are
first projected into the face plane, then use directional interval gaps and a
stable center/canonical tie-break to point from terrain toward the box; their
stored-float face-normal dot may differ from zero only by representation
tolerance. The query remains a one-sided discrete heightfield contact. A box
that has already tunneled completely below the thin surface is a miss;
continuous collision remains deferred.

These queries do not apply impulses or modify state, and no box is added to the
Environment Lab. Their fixed-capacity records of at most four points are the
honest inspection surface for PHY-006; a frozen render fixture would not
visualize real contacts. PHY-007 will consume contact witnesses through one
shared response path.

## Rendering boundary

The existing material-sphere mesh remains packed in the terrain geometry
buffers at its original authored center. Its vertex shader reads seven 32-bit
`b2` constants: one quaternion followed by one world position.
`RenderFrameData` owns fixed four-entry position/orientation arrays and an
active count in `[0, 4]`. The renderer validates active transforms, binds the
material-sphere PSO once, then visits active bodies in index order, rebinds
`b2`, rotates each vertex/normal about the authored center, and issues the
existing indexed draw once per sphere inside `Terrain`. A small local `+X`
material cap makes rotation inspectable without another mesh or texture.

This adds no geometry buffer, descriptor, texture, graph pass, water resource,
or upload-buffer allocation. Four active bodies produce exactly four draws and
`4 * 1,584` indices per submitted frame. Composition-level tests lock the
physics, world, and renderer capacities together and validate the renderer
fixture's one-meter visual radius against the scenario-owned collider radius.

`F4` retains the existing bounded six-vertex/six-index cyan normal pin and
magenta visible-chunk bounds. The pin is now built at the proof sphere's fixed
support sample and extends two meters from the canonical surface along the
exact face normal, so its tip remains visible after the sphere settles. It is
a support-normal preview, not an active-contact indicator: `F4` also shows it
while the sphere is airborne. No per-frame diagnostic upload or additional
draw was added.

## Verification

Permanent CPU coverage checks:

- exact 60 Hz step accounting under differently partitioned render deltas;
- pause, resume, accumulator reset, and one-shot single-step behavior;
- bounded elapsed-time acceptance and invalid-input rejection;
- semi-implicit ballistic motion under standard gravity;
- the same authoritative trajectory across different render rates;
- interpolation endpoints, intermediate values, and finite-state validation;
- identity/unit quaternion and explicit solid-sphere inertia contracts;
- zero-torque preservation, exact constant-angular-velocity increments, and
  analytic semi-implicit torque response;
- deterministic normalization, shortest-path quaternion interpolation, and
  invalid/overflow transactionality;
- bit-identical angular state across 30/60/120/144 Hz render partitions;
- flat-terrain fall, exact long-term support, and zero resting velocity;
- exact sloped-face plane clearance and geometric normal ownership;
- diagonal, maximum-edge, immediately-outside, and separating transitions;
- transactional rejection of invalid radii, state, deltas, and overflow; and
- bit-identical resting state/contact count across 30/60/120/144 Hz render
  partitions;
- fixed four-body/six-pair capacity and lexicographic pair traversal;
- separated no-op, equal overlap projection, touching, and coincident-center
  fallback behavior;
- equal-mass restitution, unequal incoming speeds, tangential preservation,
  momentum conservation, and separating-overlap behavior;
- transactional count, restitution, state, collider, and numeric-overflow
  rejection;
- bit-identical sphere-collision results across 30/60/120/144 Hz render
  partitions; and
- the real Environment Lab pair impulse occurring before either pair body
  touches terrain while body 0 reaches canonical support;
- identity, rotated, sign-equivalent, degenerate, invalid, and overflowing
  capsule endpoint construction;
- separated, tolerance-touching, penetrating, endpoint, parallel, crossed,
  coincident, and degenerate capsule closest-feature cases;
- canonical terrain face, edge, vertex, diagonal, maximum-edge, coplanar, and
  zero-length segment ownership with stable repeated results; and
- identical capsule query results after fixed ticks reached through
  30/60/120/144 Hz render partitions;
- identity, rotated, sign-equivalent, invalid, overflowing, and float-collapsed
  box world geometry;
- separated, tolerance-touching, penetrating, four-point face, edge, corner,
  rotated, cross-axis-only, coincident, near-parallel, tied-axis, and swapped
  box/box cases with exact repeated ordering;
- flat, sloped, tilted, maximum-boundary, outside, vertically separated, and
  invalid box/terrain queries with canonical witnesses; and
- identical box contact results after fixed ticks reached through
  30/60/120/144 Hz render partitions.

After defining `$cmake` and `$ctest` with the discovery block in
[Building Shark](BUILDING.md#fresh-command-line-build), build and run all
discovered unit cases with:

```powershell
& $cmake --build --preset windows-debug --target SharkTests
& $ctest --preset windows-debug -R '^unit\.'
```

For manual acceptance, launch `SharkSandbox`. Four spheres must remain still
at startup. `F6` advances all four by one deterministic tick. After `F5`,
bodies 1 and 2 collide while airborne and separate, while body 0 falls to the
visible terrain and remains at rest without hover or penetration. Pause/resume
must not create a catch-up jump. The local brown `+X` cap on body 3 must rotate
under its constant torque while the sphere remains isolated; its angular motion
continues after terrain support because contact friction is deferred. `F4`
still previews only body 0's fixed
canonical support normal and is intentionally available before and after
contact. Camera motion and presentation-only water remain independent. PHY-005
adds no visible capsule and PHY-006 adds no visible box to this manual scene;
their acceptance result is tested CPU contact capability rather than a
misleading static render proxy.

PHY-004's focused rigid-body suite passes `1,540` assertions across nine cases
in both Debug and Release; both full unit suites pass `182/182`. Presentation
smoke passes 1,000 frames on Debug hardware, 600 on Debug WARP, 1,000 on
Release hardware, and 120 on Debug WARP with GPU-based validation. Every path
observes the airborne `(1,2)` impulse, body 0's exact canonical support, and
finite normalized torque-driven body-3 rotation. They record exactly 4,000,
2,400, 4,000, and 480 sphere draws respectively, with zero D3D12 corruption,
errors, or live child objects.

PHY-005's focused capsule suite passes `3,242` assertions across 11 cases in
both Debug and Release. The Terrain-owned segment query passes `442` assertions
across seven cases in both configurations, including exact repeated ownership.
Both full unit presets pass `202/202`. The unchanged Debug hardware
presentation smoke passes 1,000 frames, records the expected 4,000 existing
sphere draws, and reports zero D3D12 corruption/errors or live child objects.

PHY-006's focused box suite passes `4,282` assertions across 15 cases in both
Debug and Release. Terrain's triangle-bounds candidate suite passes `351`
assertions across seven cases in both configurations. Both complete suites
pass `393,840` assertions across `224/224` cases. The unchanged Debug hardware
presentation smoke passes 1,000 frames, records the expected 4,000 existing
sphere draws, and reports zero D3D12 corruption/errors or live child objects.

## Explicit non-goals

PHY-006 adds no capsule or box simulation entity, capsule/box mass or inertia,
positional or velocity response, unequal per-body mass, angular contact
impulse, rolling or sliding friction, persistent manifold, iterative constraint
solve, continuous collision detection, broad phase, sleeping, arbitrary
closest-feature sphere/triangle collision,
buoyancy, water displacement, reset control, entity system, or general
debug-draw service. Its one-sample face support remains intended for the
current one-meter-radius, four-meter-cell, slope-bounded Environment Lab
heightfield proof. The lake remains W-001 presentation-only water, and `R-001`
through `R-004` remain deferred. The active queue is `PHY-007` contact
constraint solving and is centralized in [ENGINE_PLAN.md](ENGINE_PLAN.md).
