#pragma once

#include <shark/physics/ballistic_body.hpp>
#include <shark/physics/contact_constraint.hpp>
#include <shark/physics/sphere_collider.hpp>

#include <array>
#include <cstddef>

namespace shark::physics {

inline constexpr std::size_t sphere_body_capacity = 4;
inline constexpr std::size_t sphere_pair_capacity =
    sphere_body_capacity * (sphere_body_capacity - 1U) / 2U;

using SphereBodyStates =
    std::array<BallisticBodyState, sphere_body_capacity>;
using SphereColliders =
    std::array<SphereCollider, sphere_body_capacity>;
using SphereBodyMassProperties =
    std::array<SolidSphereMassProperties, sphere_body_capacity>;

struct SphereBodyCollisionSettings final {
    float restitution{0.75F};
    float static_friction{};
    float dynamic_friction{};
    ContactSolverSettings solver{};

    [[nodiscard]] friend bool operator==(
        const SphereBodyCollisionSettings&,
        const SphereBodyCollisionSettings&) noexcept = default;
};

struct SphereBodyPairContact final {
    std::size_t first_body_index{};
    std::size_t second_body_index{};
    math::Float3 normal{};
    float penetration_depth{};
    float relative_normal_velocity_before_resolution{};
    float normal_impulse_magnitude{};

    [[nodiscard]] friend bool operator==(
        const SphereBodyPairContact&,
        const SphereBodyPairContact&) noexcept = default;
};

struct SphereBodyCollisionStep final {
    std::array<SphereBodyPairContact, sphere_pair_capacity> contacts{};
    std::size_t contact_count{};
    std::size_t broad_phase_proxy_count{};
    std::size_t possible_pair_count{};
    std::size_t x_overlap_pair_count{};
    std::size_t candidate_pair_count{};
    std::size_t narrow_phase_tested_pair_count{};

    [[nodiscard]] friend bool operator==(
        const SphereBodyCollisionStep&,
        const SphereBodyCollisionStep&) noexcept = default;
};

// Resolves the active prefix through conservative sphere AABBs and a bounded
// deterministic broad phase. Narrow-phase candidates, contacts, and solver
// constraints retain stable lexicographic body-index order. Coincident centers
// use +X as a deterministic normal. Every overlap is gathered before one shared
// iterative constraint solve.
//
// This remains discrete collision detection without continuous collision
// detection. Validation and numerical failures leave the input states
// unchanged.
[[nodiscard]] core::Result<SphereBodyCollisionStep>
resolve_sphere_body_collisions(
    SphereBodyStates& states,
    const SphereColliders& colliders,
    const SphereBodyMassProperties& mass_properties,
    std::size_t active_body_count,
    SphereBodyCollisionSettings settings = {});

} // namespace shark::physics
