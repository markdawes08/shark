#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>

#include <type_traits>

namespace shark::physics {

struct RigidBodyState final {
    math::Float3 position{};
    math::Quaternion orientation{};
    math::Float3 linear_velocity{};
    math::Float3 angular_velocity{};

    [[nodiscard]] friend bool operator==(
        const RigidBodyState&,
        const RigidBodyState&) noexcept = default;
};

struct RigidBodyMassProperties final {
    // Mass and local moment are authoritative physical values. Their stored
    // reciprocals must be consistent and are published for constraint use.
    float mass{};
    float inverse_mass{};
    math::Float3 local_moment_of_inertia{};
    math::Float3 local_inverse_moment_of_inertia{};

    [[nodiscard]] friend bool operator==(
        const RigidBodyMassProperties&,
        const RigidBodyMassProperties&) noexcept = default;
};

struct SolidSphereMassProperties final {
    float mass{};
    float inverse_mass{};
    float radius{};
    float moment_of_inertia{};
    float inverse_moment_of_inertia{};

    [[nodiscard]] friend bool operator==(
        const SolidSphereMassProperties&,
        const SolidSphereMassProperties&) noexcept = default;
};

static_assert(std::is_standard_layout_v<RigidBodyState>);
static_assert(std::is_trivially_copyable_v<RigidBodyState>);
static_assert(std::is_standard_layout_v<RigidBodyMassProperties>);
static_assert(std::is_trivially_copyable_v<RigidBodyMassProperties>);
static_assert(std::is_standard_layout_v<SolidSphereMassProperties>);
static_assert(std::is_trivially_copyable_v<SolidSphereMassProperties>);

[[nodiscard]] bool is_valid(
    const RigidBodyState& state) noexcept;

[[nodiscard]] bool is_valid(
    const RigidBodyMassProperties& properties) noexcept;

[[nodiscard]] bool is_valid(
    const SolidSphereMassProperties& properties) noexcept;

[[nodiscard]] core::Result<SolidSphereMassProperties>
make_solid_sphere_mass_properties(
    float mass,
    float radius);

// Converts the checked scalar inertia of a solid sphere into the common local
// diagonal-inertia representation used by shape-neutral rigid-body motion.
[[nodiscard]] core::Result<RigidBodyMassProperties>
to_rigid_body_mass_properties(
    const SolidSphereMassProperties& properties);

// Angular velocity and torque are expressed in world space. The update
// advances world angular momentum through the body's local diagonal inertia,
// left-multiplies the exact axis-angle orientation increment, then derives the
// new world angular velocity from the rotated inertia tensor.
[[nodiscard]] core::Result<void> advance_rigid_body_angular_motion(
    RigidBodyState& state,
    const RigidBodyMassProperties& properties,
    math::Float3 torque,
    float fixed_delta_seconds);

// Compatibility overload for existing solid-sphere callers. It converts to
// the common diagonal-inertia representation before advancing motion.
[[nodiscard]] core::Result<void> advance_rigid_body_angular_motion(
    RigidBodyState& state,
    const SolidSphereMassProperties& properties,
    math::Float3 torque,
    float fixed_delta_seconds);

[[nodiscard]] core::Result<RigidBodyState> interpolate_rigid_body(
    const RigidBodyState& previous,
    const RigidBodyState& current,
    float alpha);

} // namespace shark::physics
