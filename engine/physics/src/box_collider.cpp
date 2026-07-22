#include <shark/physics/box_collider.hpp>

#include <shark/core/error.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::physics {
namespace {

[[nodiscard]] core::Error box_mass_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] bool positive_representable_float(
    const double value) noexcept
{
    return std::isfinite(value) &&
        value > 0.0 &&
        value <= static_cast<double>(
            std::numeric_limits<float>::max()) &&
        static_cast<float>(value) > 0.0F;
}

[[nodiscard]] double box_axis_moment(
    const double mass,
    const double first_half_extent,
    const double second_half_extent) noexcept
{
    return mass / 3.0 *
        (first_half_extent * first_half_extent +
         second_half_extent * second_half_extent);
}

[[nodiscard]] math::Float3 expected_box_moment(
    const float mass,
    const BoxCollider& collider) noexcept
{
    const auto precise_mass = static_cast<double>(mass);
    const auto half_x =
        static_cast<double>(collider.local_half_extents.x);
    const auto half_y =
        static_cast<double>(collider.local_half_extents.y);
    const auto half_z =
        static_cast<double>(collider.local_half_extents.z);
    return {
        static_cast<float>(box_axis_moment(
            precise_mass,
            half_y,
            half_z)),
        static_cast<float>(box_axis_moment(
            precise_mass,
            half_x,
            half_z)),
        static_cast<float>(box_axis_moment(
            precise_mass,
            half_x,
            half_y)),
    };
}

} // namespace

bool is_valid(
    const SolidBoxMassProperties& properties) noexcept
{
    if (!is_valid(properties.collider) ||
        !is_valid(properties.body)) {
        return false;
    }
    return properties.body.local_moment_of_inertia ==
        expected_box_moment(
            properties.body.mass,
            properties.collider);
}

core::Result<SolidBoxMassProperties>
make_solid_box_mass_properties(
    const float mass,
    const BoxCollider& collider)
{
    if (!std::isfinite(mass) ||
        mass <= 0.0F ||
        !is_valid(collider)) {
        return core::Result<SolidBoxMassProperties>::failure(
            box_mass_error(
                core::ErrorCode::invalid_argument,
                "Solid box requires finite positive mass and half-extents"));
    }

    const auto precise_mass = static_cast<double>(mass);
    const auto half_x =
        static_cast<double>(collider.local_half_extents.x);
    const auto half_y =
        static_cast<double>(collider.local_half_extents.y);
    const auto half_z =
        static_cast<double>(collider.local_half_extents.z);
    const auto inverse_mass = 1.0 / precise_mass;
    const auto moment_x = box_axis_moment(
        precise_mass,
        half_y,
        half_z);
    const auto moment_y = box_axis_moment(
        precise_mass,
        half_x,
        half_z);
    const auto moment_z = box_axis_moment(
        precise_mass,
        half_x,
        half_y);
    if (!positive_representable_float(inverse_mass) ||
        !positive_representable_float(moment_x) ||
        !positive_representable_float(moment_y) ||
        !positive_representable_float(moment_z)) {
        return core::Result<SolidBoxMassProperties>::failure(
            box_mass_error(
                core::ErrorCode::unavailable,
                "Solid-box mass properties exceeded finite float range"));
    }

    const math::Float3 moment{
        static_cast<float>(moment_x),
        static_cast<float>(moment_y),
        static_cast<float>(moment_z),
    };
    const auto inverse_moment_x = 1.0 / moment_x;
    const auto inverse_moment_y = 1.0 / moment_y;
    const auto inverse_moment_z = 1.0 / moment_z;
    if (!positive_representable_float(inverse_moment_x) ||
        !positive_representable_float(inverse_moment_y) ||
        !positive_representable_float(inverse_moment_z)) {
        return core::Result<SolidBoxMassProperties>::failure(
            box_mass_error(
                core::ErrorCode::unavailable,
                "Solid-box inverse inertia exceeded finite float range"));
    }

    SolidBoxMassProperties properties{
        .collider = collider,
        .body = {
            .mass = mass,
            .inverse_mass = static_cast<float>(inverse_mass),
            .local_moment_of_inertia = moment,
            .local_inverse_moment_of_inertia = {
                static_cast<float>(inverse_moment_x),
                static_cast<float>(inverse_moment_y),
                static_cast<float>(inverse_moment_z),
            },
        },
    };
    if (!is_valid(properties)) {
        return core::Result<SolidBoxMassProperties>::failure(
            box_mass_error(
                core::ErrorCode::unavailable,
                "Solid-box mass construction lost reciprocal precision"));
    }
    return core::Result<SolidBoxMassProperties>::success(properties);
}

} // namespace shark::physics
