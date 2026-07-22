#pragma once

#include <shark/physics/rigid_body.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace shark::physics {

inline constexpr std::size_t contact_solver_body_capacity = 4;
inline constexpr std::size_t contact_constraint_capacity = 10;
inline constexpr std::size_t contact_points_per_constraint = 4;
inline constexpr std::uint32_t
    max_contact_solver_velocity_iterations = 32;
inline constexpr std::size_t static_contact_body_index =
    std::numeric_limits<std::size_t>::max();

struct ContactBodyMassProperties final {
    float inverse_mass{};
    math::Float3 local_inverse_inertia{};

    [[nodiscard]] friend bool operator==(
        const ContactBodyMassProperties&,
        const ContactBodyMassProperties&) noexcept = default;
};

struct ContactMaterial final {
    float restitution{};
    float static_friction{};
    float dynamic_friction{};

    [[nodiscard]] friend bool operator==(
        const ContactMaterial&,
        const ContactMaterial&) noexcept = default;
};

struct ContactConstraintPoint final {
    // One common world-space witness used for both endpoints.
    math::Float3 position{};
    float penetration_depth{};

    [[nodiscard]] friend bool operator==(
        const ContactConstraintPoint&,
        const ContactConstraintPoint&) noexcept = default;
};

struct ContactConstraint final {
    std::size_t first_body_index{static_contact_body_index};
    std::size_t second_body_index{static_contact_body_index};
    math::Float3 normal{};
    ContactMaterial material{};
    std::array<
        ContactConstraintPoint,
        contact_points_per_constraint>
        points{};
    std::size_t point_count{};

    [[nodiscard]] friend bool operator==(
        const ContactConstraint&,
        const ContactConstraint&) noexcept = default;
};

struct ContactSolverSettings final {
    std::uint32_t velocity_iterations{8};
    float restitution_velocity_threshold{1.0F};
    float penetration_slop{0.001F};
    float penetration_correction_fraction{0.8F};
    float maximum_position_correction{0.2F};

    [[nodiscard]] friend bool operator==(
        const ContactSolverSettings&,
        const ContactSolverSettings&) noexcept = default;
};

struct ContactPointWarmStart final {
    float normal_impulse_magnitude{};
    // World-space tangent impulse applied to the second endpoint.
    math::Float3 tangent_impulse{};

    [[nodiscard]] friend bool operator==(
        const ContactPointWarmStart&,
        const ContactPointWarmStart&) noexcept = default;
};

struct ContactConstraintWarmStart final {
    std::array<
        ContactPointWarmStart,
        contact_points_per_constraint>
        points{};
    std::size_t point_count{};

    [[nodiscard]] friend bool operator==(
        const ContactConstraintWarmStart&,
        const ContactConstraintWarmStart&) noexcept = default;
};

struct ContactPointImpulse final {
    float relative_normal_velocity_before_resolution{};
    float warm_start_normal_impulse_magnitude{};
    math::Float3 warm_start_tangent_impulse{};
    float normal_impulse_magnitude{};
    // Accumulated world-space tangent impulse applied to the second endpoint.
    math::Float3 tangent_impulse{};

    [[nodiscard]] friend bool operator==(
        const ContactPointImpulse&,
        const ContactPointImpulse&) noexcept = default;
};

struct ContactConstraintResult final {
    std::array<
        ContactPointImpulse,
        contact_points_per_constraint>
        points{};
    std::size_t point_count{};

    [[nodiscard]] friend bool operator==(
        const ContactConstraintResult&,
        const ContactConstraintResult&) noexcept = default;
};

struct ContactSolverStep final {
    std::array<
        ContactConstraintResult,
        contact_constraint_capacity>
        constraints{};
    std::size_t constraint_count{};
    std::uint32_t completed_velocity_iterations{};

    [[nodiscard]] friend bool operator==(
        const ContactSolverStep&,
        const ContactSolverStep&) noexcept = default;
};

static_assert(std::is_standard_layout_v<ContactBodyMassProperties>);
static_assert(std::is_trivially_copyable_v<ContactBodyMassProperties>);
static_assert(std::is_standard_layout_v<ContactMaterial>);
static_assert(std::is_trivially_copyable_v<ContactMaterial>);
static_assert(std::is_standard_layout_v<ContactConstraintPoint>);
static_assert(std::is_trivially_copyable_v<ContactConstraintPoint>);
static_assert(std::is_standard_layout_v<ContactConstraint>);
static_assert(std::is_trivially_copyable_v<ContactConstraint>);
static_assert(std::is_standard_layout_v<ContactSolverSettings>);
static_assert(std::is_trivially_copyable_v<ContactSolverSettings>);
static_assert(std::is_standard_layout_v<ContactPointWarmStart>);
static_assert(std::is_trivially_copyable_v<ContactPointWarmStart>);
static_assert(std::is_standard_layout_v<ContactConstraintWarmStart>);
static_assert(std::is_trivially_copyable_v<ContactConstraintWarmStart>);
static_assert(std::is_standard_layout_v<ContactPointImpulse>);
static_assert(std::is_trivially_copyable_v<ContactPointImpulse>);
static_assert(std::is_standard_layout_v<ContactConstraintResult>);
static_assert(std::is_trivially_copyable_v<ContactConstraintResult>);
static_assert(std::is_standard_layout_v<ContactSolverStep>);
static_assert(std::is_trivially_copyable_v<ContactSolverStep>);

// Solves the supplied manifolds in their existing order for an exact fixed
// iteration count. Constraint normals point from the first endpoint toward
// the second endpoint. Either endpoint may be static, but never both.
//
// Normal and tangent impulses accumulate only within this call. A bounded,
// inverse-mass-weighted translation follows the velocity solve using each
// manifold's deepest point. Validation or numerical failure leaves every
// input state unchanged.
[[nodiscard]] core::Result<ContactSolverStep>
solve_contact_constraints(
    std::span<RigidBodyState> states,
    std::span<const ContactBodyMassProperties> mass_properties,
    std::span<const ContactConstraint> constraints,
    ContactSolverSettings settings = {});

// Warm-start values must align one-for-one with constraints and their points.
// The solver captures restitution targets before applying warm impulses,
// reprojects cached world tangents into each current contact plane, clamps
// them to the current Coulomb cone, and then performs the same exact fixed
// iteration count as the cold overload. The supplied warm starts are never
// mutated, and any failure leaves every body state unchanged.
[[nodiscard]] core::Result<ContactSolverStep>
solve_contact_constraints(
    std::span<RigidBodyState> states,
    std::span<const ContactBodyMassProperties> mass_properties,
    std::span<const ContactConstraint> constraints,
    std::span<const ContactConstraintWarmStart> warm_starts,
    ContactSolverSettings settings = {});

} // namespace shark::physics
