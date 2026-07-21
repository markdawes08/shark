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
static_assert(std::is_standard_layout_v<SolidSphereMassProperties>);
static_assert(std::is_trivially_copyable_v<SolidSphereMassProperties>);

[[nodiscard]] bool is_valid(
    const RigidBodyState& state) noexcept;

[[nodiscard]] bool is_valid(
    const SolidSphereMassProperties& properties) noexcept;

[[nodiscard]] core::Result<SolidSphereMassProperties>
make_solid_sphere_mass_properties(
    float mass,
    float radius);

// Angular velocity and torque are expressed in world space. The update uses
// semi-implicit Euler and left-multiplies the exact axis-angle increment.
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
