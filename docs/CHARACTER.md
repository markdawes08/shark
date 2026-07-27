# Character Contract

- **Completed through:** `AVT-001`
- **Camera integration verified through:** `CAM-001`
- **Last verified:** July 27, 2026
- **AVT-001 verification:** Debug and Release each passed `613,817` assertions
  across `418` cases

AVT-001 replaces the diagnostic capsule with an original, code-native
six-part placeholder and derives bounded idle, walk, run, jump, wade, and
surface-swim presentation from the existing immutable player snapshots.
CHR-006's camera-relative movement, authoritative horizontal/vertical/water
state, terrain traversal, surface capture, and recovery remain unchanged.

Underwater movement, a swim jump, currents, obstacle stepping, arbitrary world
collision, an entity registry, a skeletal/glTF asset pipeline, production
animation, and final avatar art remain outside this increment.

## Ownership and data flow

`engine/character` owns the platform- and renderer-independent controller. It
depends on Core and Terrain for `HeightTileSurface` CPU queries and consumes
only the platform-independent `water::GameplayWaterQuery` result from Water.
It does not enter the dynamic rigid-body arrays or read a render mesh, coarse
visual LOD, D3D12 resource, camera, calm-water authoring body, or GPU fluid
state.

```text
Platform events
  -> sandbox PlayerCommandSource
  -> one PlayerActionCommand per emitted 60 Hz tick
  -> World advances the authoritative orbit and publishes a horizontal basis
  -> sandbox queries WQ-001 at authoritative tick-start player X/Z
  -> Character maps water state, ground/air/swim intent, velocity, facing,
     and terrain probes
  -> Character previous/current snapshots
  -> presentation-only player interpolation
  -> sandbox PlayerAvatarFrame mapper
  -> renderer PlaceholderAvatarProxy
  -> World third-person target (read-only sibling consumer)
```

The sandbox advances the third-person orbit first on each emitted fixed tick,
derives a Character-owned movement frame from that tick's new yaw, queries
WQ-001 at the current authoritative capsule X/Z, advances Character on the
same tick number, and then advances the retained dynamic-sphere simulation.
Catch-up frames repeat that query for every emitted tick; interpolated render
positions never drive it. A simultaneous look-and-move command therefore uses
the new heading without a one-tick lag. Character does not depend on World;
the composition root copies World's basis into `PlayerMovementFrame`.
Free-fly camera mode neutralizes character actions but does not suspend
gravity, airborne momentum, grounding, or water observation.

## Capsule and support model

The capsule is upright along world `+Y`. Its shape is a radius plus the
half-length of its vertical centerline segment. The Island Demo authors both
as `0.5` meter, producing a one-meter-wide, two-meter-tall capsule.

Ground support samples the exact canonical LOD0 face at the capsule center's
X/Z. Given sampled point `p`, exact geometric face normal `n`, capsule radius
`r`, and half-segment `h`, the supported center is:

```text
center_y = p.y + h + r / n.y
```

This places the lower spherical cap one radius from the selected face plane;
using only `p.y + h + r` would penetrate a sloped face. The sample retains
Terrain's fixed triangle split and inclusive maximum-edge ownership. Render
normals and coarse terrain never participate.

This remains deliberately a heightfield controller. CHR-004 samples the full
three-dimensional capsule-center path at bounded intervals, but does not
perform exact continuous collision detection, an exact crossed-triangle
query, or a swept-capsule query against side features, overhangs, props, or
arbitrary obstacles. A sufficiently narrow or corner-grazed non-walkable face
on a different terrain topology can fall between samples; the Island Demo's
authored topology and route are verified separately.

## Grounding policy and states

The scenario publishes a bounded `PlayerGroundingSettings` record:

| Setting | Island Demo value |
|---|---:|
| gravity magnitude | `9.81 m/s²` |
| minimum walkable normal Y | `0.70710678` (inclusive 45-degree limit) |
| ground snap distance | `0.05 m` |

A fixed delta must be finite and in `(0, 0.25]` seconds. The ordinary sandbox
uses the existing fixed 60 Hz clock.

Every authoritative snapshot contains vertical velocity, an exact support
normal when touching terrain, and one explicit phase:

| Phase | Meaning |
|---|---|
| `rising` | A grounded launch or continued ascent has positive post-gravity vertical velocity; there is no active support normal |
| `falling` | Gravity advances finite nonpositive vertical velocity; there is no active support normal |
| `landing` | This descending tick crossed onto walkable support; position is exact and vertical velocity is positive zero |
| `grounded` | Walkable support remains exact and stable; no gravity jitter is accumulated |
| `steep_contact` | Terrain contact is too steep to call walkable; vertical penetration is blocked while slope traversal remains deferred |

Walkable classification is the inclusive comparison
`surface.normal.y >= minimum_walkable_normal_y`. A landing phase lasts exactly
one authoritative tick and becomes grounded on the next supported tick.

Airborne motion uses semi-implicit Euler:

```text
velocity_y -= gravity_magnitude * fixed_delta
center_y   += velocity_y * fixed_delta
```

When a descending center-path probe crosses support plus the snap distance,
Character publishes the exact support height and positive-zero vertical
velocity. The sampled crossing protects the authored heightfield path without
claiming exact continuous collision detection. A non-walkable crossing
publishes `steep_contact`, not `grounded`.

## Ground locomotion

The Island Demo publishes this bounded `PlayerGroundLocomotionSettings`
record:

| Setting | Island Demo value |
|---|---:|
| walk target | `4 m/s` |
| run target | `7 m/s` |
| acceleration | `24 m/s^2` |
| braking deceleration | `32 m/s^2` |
| facing turn speed | `10 rad/s` |
| maximum terrain-probe spacing | `0.25 m` |

Each fixed tick cancels opposing held directions, combines the signed
forward/right axes through the current camera-relative movement frame, and
normalizes a nonzero diagonal. This prevents diagonal input from exceeding
the selected walk or run target.

The authoritative horizontal velocity moves toward the requested vector by a
bounded amount. Acceleration is used while gaining speed; braking is used
with no intent, while reversing, or while dropping toward a lower target.
Braking snaps exactly to positive zero rather than oscillating across rest.
Facing turns toward meaningful input through the shortest wrapped yaw arc,
bounded by the authored turn speed. Facing is retained when input is neutral.

Ordinary ground displacement uses the new velocity semi-implicitly. Character
divides the requested path into deterministic probes no farther apart than
the authored maximum. Every accepted probe must have a walkable canonical
LOD0 support sample and a slope-correct center inside the configured bounds.
A steep, missing, or out-of-bounds probe stops traversal, retains the last
safe probe, and zeros horizontal velocity. This is sampled rejection rather
than a continuous sweep.

## Jumping and airborne control

The Island Demo publishes this bounded `PlayerAirLocomotionSettings` record:

| Setting | Island Demo value |
|---|---:|
| pre-gravity jump launch speed | `6.5 m/s` |
| airborne control acceleration | `12 m/s^2` |

`Space` launches only from `grounded` or the one-tick `landing` phase. Reset
wins if reset and jump arrive together. On a launch tick, Character applies
airborne horizontal control rather than the stronger ground acceleration,
sets pre-gravity vertical velocity to `6.5 m/s`, and then performs the same
semi-implicit gravity step:

```text
velocity_y = 6.5 - 9.81 * fixed_delta
center_y  += velocity_y * fixed_delta
```

Airborne intent uses the same newly advanced, camera-relative movement frame
as grounded movement and the same `4/7 m/s` walk/run targets. Horizontal
velocity moves toward a nonzero target by at most
`12 m/s^2 * fixed_delta`, weaker than the `24 m/s^2` ground acceleration.
With neutral airborne input, horizontal momentum is preserved exactly: there
is no implicit air drag or braking. Meaningful intent continues to turn
facing through the same bounded shortest-arc rule.

Jump pulses received during `rising` or `falling` are consumed as one-tick
commands but ignored by the controller, so there is no double jump. The
post-gravity vertical velocity selects `rising` while positive and `falling`
once nonpositive.

Airborne movement samples the full three-dimensional center path with probes
no farther than `0.25 m` apart. The first descending walkable crossing lands
at exact slope-correct support, retains horizontal velocity, and publishes
exactly one `landing` tick. The next supported tick is `grounded` unless a new
jump launches directly from `landing`.

A rising terrain intrusion or rejected X/Z bound keeps the last safe
horizontal prefix, zeros horizontal velocity, and continues vertical motion.
A steep crossing also zeros horizontal velocity. Missing terrain support is
legal while airborne; if the uncontacted capsule falls below its configured
minimum Y, Character recovers to the canonical dry spawn. These are
deterministic sampled heightfield rules, explicitly not an exact swept capsule
or continuous collision detector.

## Shallow-water wading

`PlayerWadingSettings` belongs to Character policy, not Water. The Island Demo
authors:

| Setting | Island Demo value |
|---|---:|
| enter depth | `0.25 m` |
| exit depth | `0.125 m` |
| depth for minimum speed | `1.5 m` |
| minimum speed multiplier | `0.5` |

Each fixed tick consumes exactly one successful WQ-001 result sampled at the
tick-start authoritative capsule X/Z. A supported `grounded` or `landing`
player enters wading at depth `>= 0.25 m`. Once wading, it remains wading while
depth is greater than `0.125 m` and exits at depth `<= 0.125 m`. This explicit
hysteresis prevents threshold-adjacent dry/wading chatter.

The consumed query must be internally valid and agree exactly with the
canonical LOD0 terrain bed at the same X/Z. An `out_of_terrain` observation is
valid only when Character also finds no terrain there; disagreement about
terrain presence, a mismatched bed, noncanonical inactive fields, or malformed
numeric state rejects the complete advance transaction. Character reads the
query DTO only; it never receives the analytic `CalmWaterBody`, renderer water
support, a D3D12 resource, or a GPU query service.

`GameplayWaterQuery` does not carry its source coordinates. Supplying the
result from the authoritative tick-start X/Z is therefore a composition-root
binding contract, while the exact-bed comparison is a defensive consistency
check. It detects wrong-location observations when their canonical bed differs
but cannot distinguish a stale result from another point with exactly the same
bed height. The sandbox performs the required query immediately before each
Character advance; explicit query provenance can be added if future producers
make accidental stale reuse a practical risk.

Wading scales the walk/run target speed without changing ground acceleration,
braking, facing, terrain probes, or the underlying authored `4/7 m/s` targets.
The multiplier is `1.0` at `0.25 m`, decreases linearly to `0.5` at `1.5 m`,
and remains clamped at `0.5` for every deeper observation. Flow is validated
by Water but remains ignored by Character.

Only supported grounded/landing motion can publish `wading`. Jump launch,
rising, falling, steep contact, missing support, reset, and recovery publish
canonical dry state with positive-zero depth. Air control and neutral airborne
momentum are unchanged. Reset wins over a valid wet tick-start observation and
restores the proven dry spawn.

The water observation describes the source position used for the current
step. If movement crosses the shoreline, the resulting position is queried
and reclassified on the next emitted fixed tick, not the next render frame.
Character performs no per-probe Water query and changes no water volume.

## Surface swimming

`PlayerSurfaceSwimmingSettings` is Character-owned policy. The Island Demo
locks:

| Setting | Island Demo value |
|---|---:|
| enter depth | `1.50 m` |
| exit depth | `1.25 m` |
| center depth below surface | `0.50 m` |
| directional speed | `3.0 m/s` |

A supported wading player enters `surface_swimming` when the tick-start WQ-001
depth is at least `1.50 m`. An existing swimmer remains in that mode while
depth is greater than `1.25 m`; at or below `1.25 m`, it returns to exact
canonical support. Walkable support publishes wading when the source still
meets wading hysteresis, otherwise dry, and preserves horizontal velocity;
steep support publishes dry `steep_contact` and zero horizontal velocity. The
separated `1.50/1.25 m` thresholds prevent transition-band chatter. The Island
transect's `1.359375 m` sample lies inside that hysteresis band and its
`5.734375 m` sample is unambiguously deep enough to enter.

The authoritative swim-center target is:

```text
max(WQ surface height - 0.50 m, canonical terrain support center Y)
```

`PlayerWaterState` retains the consumed surface height only for
`surface_swimming`; dry and wading snapshots keep that field at canonical
positive zero. This lets snapshot validation bound a swimmer against the
source surface without making Water state authoritative inside Character.

The maximum prevents the upright capsule from being placed below the
canonical bed as the shore rises. Surface swimming is water-supported rather
than terrain-grounded: its vertical state carries no terrain support normal,
and ground support remains a collision/recovery reference rather than a
seabed locomotion constraint.

Swim input uses the same current camera-relative horizontal basis and one
`3 m/s` target speed. Shift is deliberately ignored in this mode. Character
reuses the existing ground acceleration, braking, shortest-arc facing, and
bounded `0.25 m` maximum terrain-probe spacing. Walkable terrain may raise the
center through the same maximum used by the baseline. A rejected X/Z bound,
missing candidate support, or steep support protruding above the surface
baseline retains the last safe horizontal prefix and zeros horizontal velocity.
Those are terrain probes only; Character does not perform a Water query for
each probe.

Jump precedence is explicit. A Space pulse from supported wading wins and
launches the existing jump before a possible swim entry. Once the player is
surface-swimming, Space is consumed but ignored; there is no swim jump.
Rising airborne motion remains unchanged and dry. Descending motion with a
sufficiently deep tick-start water observation is captured at the bounded
surface-center target before a submerged terrain landing.

The single WQ-001 result still describes only the authoritative tick-start
X/Z. Movement can therefore cross an entry/exit or containment boundary before
the water phase is reclassified on the next emitted fixed tick. Terrain
safe-prefix rejection protects the capsule during that source-only step. The
query DTO carries no source coordinates: exact canonical-bed matching catches
many wrong-location observations but cannot distinguish a stale query from
another point with the same bed height. That known provenance limitation is
accepted for the current composition-root-owned calm-water producer.

If an existing swimmer receives `no_water`, Character snaps to grounded/dry
only when its current center is at or above canonical support and within the
configured ground-snap distance. Otherwise it publishes dry `falling` with
zero vertical velocity and no support normal while preserving horizontal
momentum. An `out_of_terrain` observation or missing canonical support uses the
existing collapsed canonical-spawn recovery. Reset wins over every water state,
restores dry spawn, increments the reset generation, and collapses
previous/current history.

Flow is validated but ignored. CHR-006 adds no underwater vertical control,
underwater combat, current response, swim jump, dynamic-water smoothing,
per-probe Water query, water mutation, GPU readback, or renderer authority.

## Spawn, reset, and interpolation

Creation validates and canonicalizes shape, bounds, grounding policy, signed
zero, and yaw. It queries the authored spawn against canonical terrain before
publishing tick zero. A spawn within the bounded snap distance is stored at
the exact support height and starts grounded (or in steep contact); a higher
spawn starts falling. A spawn meaningfully below support is rejected.

The Island Demo spawn remains dry at `(0, 0.890625, 112)` on canonical terrain
and returns `no_water` from WQ-001. Its capsule center is derived from the
support formula rather than duplicated scenario arithmetic.

`advance_player_capsule` accepts only the exact next fixed-tick number. It
validates source terrain consistency, command, delta, arithmetic, bounds, and
the complete candidate before committing. A rejected call leaves the whole
simulation unchanged.

`R` re-queries and restores the canonical authored spawn, increments the reset
generation, zeros horizontal and vertical velocity, and collapses
previous/current pose and motion state. Falling below the configured minimum Y
uses the same recovery path and also increments the reset generation. Neither
recovery emits a synthetic landing pulse or interpolates through the
teleport. Both publish dry water state, and reset/recovery cannot be overridden
by the source position's wet observation.

`interpolate_player_capsule` remains presentation-only: position is linear and
yaw follows the shortest wrapped arc. Vertical phase and velocity stay
authoritative snapshot data. F5, minimize/restore, dropped-event recovery, and
other time-baseline discontinuities also collapse the player's interpolation
interval alongside the camera and dynamic spheres, preventing a one-tick
visual snap when simulation resumes.

## Commands and current controls

The sandbox maps:

| Input | Character action |
|---|---|
| `W`, `S`, `A`, `D` | forward, backward, left, right held |
| either `Shift` | run held on supported ground; ignored while swimming |
| `Space` | jump pressed from supported ground/wading; ignored while swimming |
| left mouse press | primary action pressed |
| right-mouse drag | bounded yaw/pitch look deltas |
| `R` | reset pressed |

Held actions repeat on emitted fixed ticks. Jump, primary action, reset, and
accumulated look are one-tick pulses. Zero-tick render frames do not consume
pending input; catch-up frames sample separately for each emitted tick.

CHR-006 consumes horizontal movement, run, jump, reset, and the composition
root's tick-start WQ-001 result while advancing the applicable
ground/air/surface-swim policy. Primary action remains deterministic command
data for a later increment.

## Placeholder avatar presentation

The visible player is now an original project-owned placeholder authored
entirely in Shark code. One logical proxy expands into a torso, head, left and
right arms, and left and right legs. The six low-poly parts reuse the existing
material-sphere/capsule parameterization, vertex/index buffers, root
constants, shader pipeline, and Terrain pass. A submitted avatar therefore
costs six indexed draws and `9,504` indices, but adds no graph pass, GPU
resource, descriptor, allocation, upload, PSO, or timestamp interval.

`player_avatar_frame` is a pure sandbox composition boundary. It reads the
validated previous/current `PlayerCapsuleSnapshot` records, calls the existing
player-root interpolation, and classifies each immutable endpoint in this
order:

1. surface swimming;
2. rising/falling jump;
3. wading; and
4. dry idle/walk/run, with steep contact presented as idle.

Exact stopped horizontal velocity selects idle. The default walk/run
presentation boundary is the midpoint between the authoritative `4` and
`7 m/s` targets (`5.5 m/s`); actual velocity selects the pose rather than the
held Shift command. Fixed-tick modulo cycles drive bounded procedural
arm/leg motion at `1.5 Hz`, with a `3 Hz` run harmonic. Jump uses bounded
vertical velocity, wading uses its own restrained gait, and surface swimming
applies a bounded forward body pitch and presentation-only vertical offset.
Every pose scalar remains inside the renderer's validated inclusive limits.

Previous/current pose scalars blend with the same alpha as the player root.
Alpha zero and one return their exact endpoints. The mapper owns no persistent
animator, reads no wall-clock time, mutates no snapshot, and never feeds a pose
back into Character, Water, Terrain, Physics, or the camera. The composition
root explicitly selects ordered-snapshot or collapsed-to-current presentation.
It selects collapse only after reset, recovery, pause, resize, or another
time-baseline discontinuity has collapsed Character's visible payload; the
mapper rejects an inconsistent collapse request. Normal identical snapshots
remain ordered, so a stationary swim cycle still interpolates smoothly, while
real discontinuities use the current gait sample at both endpoints and cannot
reintroduce a one-tick limb smear.

The default World-owned third-person rig still targets the same interpolated
player center, not an avatar part or visual offset. Terrain obstruction can
shorten only the presentation boom and cannot mutate Character. `F7` retains
the independent free-fly diagnostic camera.

## Verification

Permanent tests cover:

- camera-relative walk/run caps, diagonal normalization, opposing-input
  cancellation, acceleration, braking, exact rest, and reversal;
- bounded shortest-arc facing and same-tick look-plus-move behavior;
- gentle-slope traversal with exact support, safe-prefix rejection at steep
  terrain and center bounds, and the complete eight-point dry Island loop;
- `6.5 m/s` launch integration, rising/apex/falling phase changes, ignored
  airborne jump pulses, and direct relaunch from the one-tick landing phase;
- weaker camera-relative airborne steering, exact neutral horizontal momentum
  preservation, and same-tick look-plus-air-move behavior;
- sampled three-dimensional terrain paths, rising intrusion/bounds rejection,
  descending exact support, and landing-to-ground movement;
- flat support over hundreds of ticks with exact positive-zero velocity;
- analytic semi-implicit falling, exactly one landing tick, and stable
  grounded recovery;
- gentle, threshold-equal, and steep face classification with slope-correct
  plane separation;
- canonical diagonal/edge ownership and explicit outside-terrain misses;
- airborne and supported reset behavior, below-minimum-Y recovery, reset
  generation, and interpolation-history collapse;
- malformed settings, delta, state, tick, overflow, bounds, and source-terrain
  disagreement with transactional rollback;
- exact `0.25/0.125 m` wading hysteresis, threshold-adjacent stability,
  linear/clamped speed scaling, and deep-water minimum speed;
- canonical-bed agreement, malformed water observations, source-position shore
  entry/exit latency, and transactional rollback;
- jump, air, steep-contact, reset, recovery, and interpolation-collapse dry
  water state, including reset winning over a wet source observation;
- exact `1.50/1.25 m` surface-swim hysteresis, bounded
  `surface - 0.50 m` positioning, `3 m/s` camera-relative movement, Shift/Space
  policy, terrain safe-prefix behavior, and walkable/steep shore exits;
- descending deep-water surface capture, unchanged rising motion, no-water
  fall/snap behavior, and missing-support collapsed recovery;
- exact command, camera basis, pose, horizontal/vertical velocity, phase,
  support, and reset transcripts across 30, 60, 120, and 144 Hz render
  partitions;
- moving player/camera presentation composition;
- exact avatar phase precedence, stopped/walk/run boundaries, endpoint
  formulas, scalar bounds, alpha endpoints/midpoints, fixed-tick cycle wrap,
  maximum-tick determinism, invalid-input non-mutation, and collapsed
  reset/lifecycle history;
- exact avatar presentation across 30, 60, 120, and 144 Hz render partitions;
  and
- six-part transform/proxy validation plus unchanged render-graph, resource,
  descriptor, pipeline, and upload accounting.

The complete CHR-005 Debug and Release suites historically passed 589,949
assertions across 400 cases, including exact 30/60/120/144 Hz render-partition
invariance. Its final Debug RTX 4070 and packaged-WARP presentation smokes
passed their existing 1,000/600-frame, grounded-tick, capsule-draw,
5,000/3,000-pass, zero-corruption/error, and zero-live-child-object contracts.
CHR-006 focused Debug surface-swimming verification passed `22,221` assertions
across `10` cases. Its complete Debug and Release suites each passed `612,172`
assertions across `410` cases. The final Debug RTX 4070 presentation smoke
passed `1,000` frames, and packaged WARP passed `600` frames. Each end-of-run
validation reported zero D3D12 corruption, zero errors, zero live D3D12 child
objects, and only the two expected device-level RLDO advisory warnings.
AVT-001's complete Debug and Release suites each passed `613,817` assertions
across `418` cases.

Launch `out\build\windows-vs2026\bin\Debug\SharkSandbox.exe` to inspect the
six-part placeholder. Press `F5` to resume/pause fixed-tick simulation, use
`W`/`A`/`S`/`D` to walk and either `Shift` to run, `F6` to single-step while
paused, `Space` to jump, right-drag/wheel to orbit and zoom, `F7` for free-fly
diagnostics, and `R` for a canonical grounded reset. In sufficiently deep
water, WASD swims at one speed; Shift and Space have no swim effect.

The next increment is `DEMO-001`: complete and verify the playable Island Demo
slice from dry spawn through land traversal, wading, surface swimming, and the
return to shore.
