#pragma once

#include <shark/physics/ballistic_body.hpp>
#include <shark/physics/sphere_collider.hpp>
#include <shark/terrain/height_tile.hpp>

#include <optional>

namespace shark::physics {

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
// the exact canonical LOD0 face beneath its predicted center. The correction
// is vertical so X/Z query ownership cannot change during resolution. PHY-002
// uses a temporary infinite-friction endpoint projection for its one resting
// proof: every velocity component is erased on contact. This is a one-sample
// heightfield approximation, not closest-feature sphere/triangle collision.
// Generalized friction, restitution, and contact constraints remain deferred.
[[nodiscard]] core::Result<SphereTerrainStep>
advance_sphere_against_terrain(
    BallisticBodyState& state,
    const SphereCollider& collider,
    const terrain::HeightTileSurface& terrain_surface,
    math::Float3 acceleration,
    float fixed_delta_seconds);

} // namespace shark::physics
