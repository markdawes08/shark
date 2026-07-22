#pragma once

#include <shark/physics/ballistic_body.hpp>
#include <shark/physics/contact_constraint.hpp>
#include <shark/physics/sphere_collider.hpp>
#include <shark/terrain/height_tile.hpp>

#include <optional>

namespace shark::physics {

struct SphereTerrainContactSettings final {
    float restitution{};
    float static_friction{1.0F};
    float dynamic_friction{0.8F};
    ContactSolverSettings solver{};

    [[nodiscard]] friend bool operator==(
        const SphereTerrainContactSettings&,
        const SphereTerrainContactSettings&) noexcept = default;
};

struct SphereTerrainContact final {
    terrain::HeightTileSurfaceSample surface;

    // Signed distance from the predicted sphere surface to the selected
    // triangle plane before correction. Negative values are penetration.
    float plane_separation_before_resolution{};
    float penetration_depth{};

    [[nodiscard]] friend bool operator==(
        const SphereTerrainContact&,
        const SphereTerrainContact&) noexcept = default;
};

struct SphereTerrainStep final {
    std::optional<SphereTerrainContact> contact;

    [[nodiscard]] friend bool operator==(
        const SphereTerrainStep&,
        const SphereTerrainStep&) noexcept = default;
};

// Advances one semi-implicit ballistic tick, then resolves the sphere against
// the exact canonical LOD0 face beneath its predicted center. The positional
// correction remains vertical so X/Z query ownership cannot change during
// resolution. Velocity and angular response use the shared contact-constraint
// solver with canonical terrain as the static first endpoint. This remains a
// one-sample heightfield approximation, not closest-feature sphere/triangle
// collision.
[[nodiscard]] core::Result<SphereTerrainStep>
advance_sphere_against_terrain(
    BallisticBodyState& state,
    const SphereCollider& collider,
    const SolidSphereMassProperties& mass_properties,
    const terrain::HeightTileSurface& terrain_surface,
    math::Float3 acceleration,
    float fixed_delta_seconds,
    SphereTerrainContactSettings settings = {});

} // namespace shark::physics
