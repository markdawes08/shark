# Fixed-Step Rigid-Body and Contact Contract

- **Completed through:** `PHY-007`
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
also as pure CPU queries without response. PHY-007 adds one bounded,
deterministic sequential-impulse solver with normal response, restitution,
Coulomb friction, angular response, and bounded positional correction. The
existing sphere/terrain and sphere/sphere adapters now use that shared path;
capsules and boxes remain query-only until a real runtime body needs them.

## Ownership and data flow

The boundaries are intentionally narrow:

- `Simulation` owns fixed-step time accounting, pause state, and step requests;
- `Physics` owns the fixed four-body capacity, rigid state, solid-sphere mass
  properties, linear/angular integration, colliders, canonical-terrain
  response, sphere-pair response, capsule closest-feature contacts,
  oriented-box SAT/manifold queries, bounded contact constraints and response,
  contact records, and interpolation;
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
one-meter radius. Body 0 remains the original zero-initial-velocity canonical
terrain proof, but it may roll after contact now that friction acts at an
offset witness.
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
index order through terrain constraints, resolves all current sphere pairs in
one stable constraint batch, advances external-torque angular motion, and
publishes no partial state. Rendering interpolates each previous/current pair
using the clock alpha and never mutates either authoritative snapshot.

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

After that ownership-preserving projection, the contact witness is the sphere
surface point `C - N * r`. Physics submits one constraint with canonical
terrain as the static first endpoint and the sphere as the dynamic second
endpoint. The Environment Lab terrain material uses restitution `0`, static
friction `1.0`, and dynamic friction `0.8`. The shared solver cancels inward
normal motion, applies Coulomb friction at the offset witness, and therefore
can exchange linear and angular velocity instead of erasing the entire
velocity vector. A sphere can roll or slide on a slope; no rolling resistance
is claimed.

The constraint carries zero penetration after the exact vertical projection,
so generic positional correction cannot move X/Z or change triangle ownership.
Each fixed tick still republishes an optional contact containing the untouched
canonical sample, pre-resolution separation, and penetration depth. State is
committed only after ballistic integration, terrain projection, constraint
solving, and finite-float validation all succeed.

## Sphere-pair collision

After every active sphere has completed ballistic prediction and terrain
support, Physics tests the active pairs exactly once in lexicographic order:

```text
(0,1), (0,2), (0,3), (1,2), (1,3), (2,3)
```

The fixed capacity is four bodies and six possible pairs. There is no dynamic
allocation and no broad phase. Each active body supplies checked solid-sphere
mass and inertia rather than relying on an implicit unit-mass rule. For centers
`A` and `B`, radii `rA` and `rB`, and first-to-second normal `N`:

```text
offset      = B - A
overlap     = rA + rB - length(offset)
N           = normalize(offset)
```

Pairs with negative overlap are separated. Touching and overlapping pairs
publish a contact. Coincident centers use `+X` as a deterministic,
index-ordered fallback normal. The common contact point is the midpoint of the
two surface witnesses. Every overlap is gathered before one shared solve, so
constraint order remains the published lexicographic order rather than being
changed by an earlier pair's position correction.

The Environment Lab pair material uses restitution `0.75` and zero friction.
The scenario still happens to use equal one-kilogram spheres, but the adapter
passes each body's explicit inverse mass and isotropic inverse inertia. The
normal impulse therefore preserves linear momentum and the solver can handle
unequal mass without a separate response path. Default positional correction
removes `80%` of penetration beyond a `0.001`-meter slop, is capped at `0.2`
meter per constraint call, and is divided by inverse mass. It does not perform
the old unbounded full-overlap split.

Each contact records stable body indices, normal, original penetration,
incoming relative normal velocity at its witness, and accumulated normal
impulse. Invalid counts, states, collider/mass mismatches, materials, settings,
or non-representable results fail transactionally without changing the input
array.

The first Environment Lab pair collision is deliberately airborne, while body
0 independently proves canonical terrain support. Pair detection is still one
discrete brute-force pass and cannot prevent fast bodies from tunneling.
Velocity constraints iterate, but no impulse survives to the next fixed tick
yet.

## Contact constraint solver

The platform-independent solver accepts at most four dynamic bodies, ten
ordered constraints, and four points per constraint. Ten covers the current
four terrain contacts plus all six sphere pairs without allocation. A special
sentinel represents a static endpoint on either side; two static endpoints and
self-constraints are rejected. Dynamic bodies supply positive inverse mass and
nonnegative local diagonal inverse inertia. Constraint normals are unit length
and point from the first endpoint toward the second.

For a world contact point `P`, body center `C`, linear velocity `v`, angular
velocity `w`, and lever arm `r = P - C`, contact velocity is:

```text
vContact = v + cross(w, r)
vRelative = vSecondContact - vFirstContact
```

For an impulse direction `d`, each dynamic endpoint contributes:

```text
Kendpoint(d) = inverseMass +
    dot(cross(r, d), worldInverseInertia * cross(r, d))
```

The world inverse inertia is the local diagonal tensor rotated by the body's
unit orientation. Static endpoints contribute zero. Normal impulses accumulate
and clamp to nonnegative values. Restitution uses the untouched incoming normal
velocity captured before iteration zero; impacts slower than the default
one-meter-per-second threshold target zero separating speed. Capturing that
target once prevents restitution from being reapplied on every iteration.

After each normal solve, the solver removes the current normal component from
relative velocity, solves along the remaining tangent direction, and
accumulates one world-space tangent impulse vector. A candidate inside
`staticFriction * normalImpulse` sticks. A candidate outside that cone is
clamped to `dynamicFriction * normalImpulse`. The vector is reconsidered every
iteration, including when the normal impulse shrinks, so it cannot remain
outside the current Coulomb cone.

After the exact configured velocity-iteration count, each manifold uses only
its deepest point for positional stabilization:

```text
correction = min(
    max(deepestPenetration - slop, 0) * correctionFraction,
    maximumCorrection)
```

Translation follows the constraint normal and is divided by endpoint inverse
mass. It changes no orientation and injects no velocity. This deliberately
bounded, timestep-independent correction keeps penetration cleanup separate
from restitution energy.

All inputs are validated before solving. Velocities, positions, impulses, and
rotated-inertia calculations run in double-precision scratch storage. The
solver commits the body span only after every result and body component is a
finite representable float, so even a failure discovered in the last body or
report rolls back the entire batch. Constraints and points always retain caller
order, velocity iterations are fixed in `[1, 32]`, and there is no early-out.
Accumulated impulses live for one call only; persistent manifold identity,
cached impulses, and warm starting belong to PHY-008.

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
the current four-sphere Environment Lab. Their witnesses can be adapted into
the PHY-007 solver when a real runtime capsule is introduced; the query layer
itself remains pure and does not own response state.

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
visualize real contacts. A later runtime-box adapter can submit those witnesses
to the shared solver without moving SAT or manifold generation into response.

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
- exact sloped-face plane clearance, geometric normal ownership, non-inward
  response, and friction-driven linear/angular exchange;
- diagonal, maximum-edge, immediately-outside, and separating transitions;
- transactional rejection of invalid radii, state, deltas, and overflow; and
- bit-identical resting state/contact count across 30/60/120/144 Hz render
  partitions;
- fixed four-body/six-pair capacity and lexicographic pair traversal;
- separated no-op, bounded slop-aware overlap correction, touching, and
  coincident-center fallback behavior;
- explicit sphere mass/inertia, equal-mass restitution, unequal incoming
  speeds, zero-friction tangent preservation, momentum conservation, and
  separating-overlap behavior;
- transactional count, material, solver-setting, state, collider/mass, and
  numeric-overflow rejection;
- bit-identical sphere-collision results across 30/60/120/144 Hz render
  partitions; and
- the real Environment Lab pair impulse occurring before either pair body
  touches terrain while body 0 reaches current canonical support;
- analytic unequal-mass restitution and momentum, separating no-pull behavior,
  and identical one/eight-iteration restitution results;
- off-center angular effective mass and symmetric two-point manifold response;
- static sticking, dynamically clamped sliding, high/low-friction slope
  behavior, and static endpoint use on either side;
- deepest-point, slop-aware, capped, inverse-mass-weighted positional
  correction;
- exact constraint/point/iteration order, empty batches, complete invalid-input
  rejection, and rollback after late float overflow;
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
visible terrain and remains on the exact canonical face without inward normal
motion. Spheres may now roll or slide because terrain friction acts at the real
offset witness. Pause/resume must not create a catch-up jump. The local brown
`+X` cap on body 3 must rotate under its constant torque and any later contact
response while its state remains finite and unit-oriented. `F4` still previews
only body 0's original fixed support normal and is intentionally available
before and after contact; it is not a live rolling-contact marker. Camera motion
and presentation-only water remain independent. PHY-005 adds no visible capsule,
PHY-006 adds no visible box, and PHY-007 adds no new entity or render work to
this manual scene; their additional acceptance evidence is CPU behavior.

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

PHY-007's focused contact-constraint suite passes `389` assertions across 12
cases, the migrated sphere/terrain suite passes `7,423` across nine cases, and
the migrated sphere-pair suite passes `3,696` across 12 cases in both Debug and
Release. The complete Physics label passes `21,215` assertions across 71 cases;
both complete unit configurations pass `394,277` assertions across `236/236`
cases. Strict Debug and Release sandbox builds pass. The Debug hardware smoke
passes 1,000 frames with the unchanged four-sphere scene and 4,000 existing
sphere draws, unchanged GPU accounting, zero D3D12 corruption/errors, and zero
live child objects.

## Explicit non-goals

PHY-007 adds no persistent manifold, cross-tick impulse cache, warm starting,
runtime capsule or box entity, capsule/box mass adapter, continuous collision,
broad phase, islands, sleeping, arbitrary convex collision, rolling resistance,
or general debug-draw service. It does not couple contact response to buoyancy,
water displacement, an entity system, or a reset control. Capsule and box
generation remain pure queries even though their witnesses can feed the shared
solver later.

The terrain adapter remains an intentional one-sample face response for the
current one-meter-radius, four-meter-cell, slope-bounded Environment Lab
heightfield. It is not closest-feature sphere/triangle collision and can still
tunnel under sufficiently large discrete motion. The lake remains W-001
presentation-only water, and `R-001` through `R-004` remain deferred. The active
queue is `PHY-008` manifold persistence and warm starting and is centralized in
[ENGINE_PLAN.md](ENGINE_PLAN.md).
