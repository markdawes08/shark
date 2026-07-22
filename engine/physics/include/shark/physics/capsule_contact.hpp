#pragma once

#include <shark/physics/capsule_collider.hpp>
#include <shark/physics/sphere_collider.hpp>
#include <shark/terrain/height_tile.hpp>

#include <optional>

namespace shark::physics {

struct CapsuleTerrainContact final {
    terrain::HeightTileSurfaceSample surface;
    math::Float3 capsule_axis_point{};

    // One-sided response normal constrained to the selected canonical face's
    // upward hemisphere. A zero-distance feature pair uses that face normal.
    math::Float3 normal{};
    float separation{};
    float penetration_depth{};

    [[nodiscard]] friend bool operator==(
        const CapsuleTerrainContact&,
        const CapsuleTerrainContact&) noexcept = default;
};

struct CapsuleSphereContact final {
    math::Float3 capsule_axis_point{};
    math::Float3 sphere_center{};

    // Points from the capsule toward the sphere.
    math::Float3 normal{};
    float separation{};
    float penetration_depth{};

    [[nodiscard]] friend bool operator==(
        const CapsuleSphereContact&,
        const CapsuleSphereContact&) noexcept = default;
};

struct CapsuleCapsuleContact final {
    math::Float3 first_axis_point{};
    math::Float3 second_axis_point{};

    // Points from the first capsule toward the second capsule.
    math::Float3 normal{};
    float separation{};
    float penetration_depth{};

    [[nodiscard]] friend bool operator==(
        const CapsuleCapsuleContact&,
        const CapsuleCapsuleContact&) noexcept = default;
};

// These are pure closest-feature queries. Inputs are never mutated, touching
// pairs produce a contact, and separated pairs succeed with an empty optional.
[[nodiscard]] core::Result<std::optional<CapsuleTerrainContact>>
query_capsule_terrain_contact(
    const RigidBodyState& capsule_state,
    const CapsuleCollider& capsule,
    const terrain::HeightTileSurface& terrain_surface);

[[nodiscard]] core::Result<std::optional<CapsuleSphereContact>>
query_capsule_sphere_contact(
    const RigidBodyState& capsule_state,
    const CapsuleCollider& capsule,
    const RigidBodyState& sphere_state,
    const SphereCollider& sphere);

[[nodiscard]] core::Result<std::optional<CapsuleCapsuleContact>>
query_capsule_capsule_contact(
    const RigidBodyState& first_state,
    const CapsuleCollider& first,
    const RigidBodyState& second_state,
    const CapsuleCollider& second);

} // namespace shark::physics
