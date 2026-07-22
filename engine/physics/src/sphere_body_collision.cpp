#include <shark/physics/sphere_body_collision.hpp>

#include <shark/core/error.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::physics {
namespace {

[[nodiscard]] core::Error collision_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] bool representable_float(
    const double value) noexcept
{
    return std::isfinite(value) &&
        std::abs(value) <=
            static_cast<double>(
                std::numeric_limits<float>::max());
}

[[nodiscard]] bool valid_settings(
    const SphereBodyCollisionSettings& settings) noexcept
{
    return std::isfinite(settings.restitution) &&
        settings.restitution >= 0.0F &&
        settings.restitution <= 1.0F &&
        std::isfinite(settings.static_friction) &&
        settings.static_friction >= 0.0F &&
        std::isfinite(settings.dynamic_friction) &&
        settings.dynamic_friction >= 0.0F &&
        settings.dynamic_friction <= settings.static_friction &&
        settings.solver.velocity_iterations > 0U &&
        settings.solver.velocity_iterations <=
            max_contact_solver_velocity_iterations &&
        std::isfinite(
            settings.solver.restitution_velocity_threshold) &&
        settings.solver.restitution_velocity_threshold >= 0.0F &&
        std::isfinite(settings.solver.penetration_slop) &&
        settings.solver.penetration_slop >= 0.0F &&
        std::isfinite(
            settings.solver.penetration_correction_fraction) &&
        settings.solver.penetration_correction_fraction >= 0.0F &&
        settings.solver.penetration_correction_fraction <= 1.0F &&
        std::isfinite(
            settings.solver.maximum_position_correction) &&
        settings.solver.maximum_position_correction >= 0.0F;
}

[[nodiscard]] bool representable_components(
    const std::array<double, 3>& components) noexcept
{
    for (const auto component : components) {
        if (!representable_float(component)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] math::Float3 to_float3(
    const std::array<double, 3>& components) noexcept
{
    return {
        static_cast<float>(components[0]),
        static_cast<float>(components[1]),
        static_cast<float>(components[2]),
    };
}

[[nodiscard]] ContactBodyMassProperties
contact_mass_properties(
    const SolidSphereMassProperties& properties) noexcept
{
    return {
        .inverse_mass = properties.inverse_mass,
        .local_inverse_inertia = {
            properties.inverse_moment_of_inertia,
            properties.inverse_moment_of_inertia,
            properties.inverse_moment_of_inertia,
        },
    };
}

} // namespace

core::Result<SphereBodyCollisionStep>
resolve_sphere_body_collisions(
    SphereBodyStates& states,
    const SphereColliders& colliders,
    const SphereBodyMassProperties& mass_properties,
    const std::size_t active_body_count,
    const SphereBodyCollisionSettings settings)
{
    if (active_body_count > sphere_body_capacity ||
        !valid_settings(settings)) {
        return core::Result<SphereBodyCollisionStep>::failure(
            collision_error(
                core::ErrorCode::invalid_argument,
                "Sphere body collision requires at most four bodies "
                "and valid material/solver settings"));
    }
    for (std::size_t index = 0;
         index < active_body_count;
         ++index) {
        if (!is_valid(states[index]) ||
            !is_valid(colliders[index]) ||
            !is_valid(mass_properties[index]) ||
            mass_properties[index].radius !=
                colliders[index].radius) {
            return core::Result<SphereBodyCollisionStep>::failure(
                collision_error(
                    core::ErrorCode::invalid_argument,
                    "Sphere body collision requires finite active "
                    "states and matching finite collider/mass pairs"));
        }
    }

    auto candidate = states;
    std::array<ContactBodyMassProperties, sphere_body_capacity>
        solver_masses{};
    for (std::size_t index = 0;
         index < active_body_count;
         ++index) {
        solver_masses[index] =
            contact_mass_properties(mass_properties[index]);
    }
    std::array<ContactConstraint, sphere_pair_capacity> constraints{};
    SphereBodyCollisionStep step;
    for (std::size_t first_index = 0;
         first_index < active_body_count;
         ++first_index) {
        for (std::size_t second_index = first_index + 1U;
             second_index < active_body_count;
             ++second_index) {
            ++step.tested_pair_count;
            const auto& first = candidate[first_index];
            const auto& second = candidate[second_index];

            const std::array<double, 3> offset{
                static_cast<double>(second.position.x) -
                    static_cast<double>(first.position.x),
                static_cast<double>(second.position.y) -
                    static_cast<double>(first.position.y),
                static_cast<double>(second.position.z) -
                    static_cast<double>(first.position.z),
            };
            const auto distance_squared =
                offset[0] * offset[0] +
                offset[1] * offset[1] +
                offset[2] * offset[2];
            const auto radius_sum =
                static_cast<double>(colliders[first_index].radius) +
                static_cast<double>(colliders[second_index].radius);
            const auto radius_sum_squared = radius_sum * radius_sum;
            if (!std::isfinite(distance_squared) ||
                !std::isfinite(radius_sum_squared)) {
                return core::Result<
                    SphereBodyCollisionStep>::failure(
                        collision_error(
                            core::ErrorCode::unavailable,
                            "Sphere pair separation exceeded finite "
                            "range"));
            }
            if (distance_squared > radius_sum_squared) {
                continue;
            }

            const auto distance = std::sqrt(distance_squared);
            const std::array<double, 3> normal =
                distance > 0.0
                ? std::array<double, 3>{
                    offset[0] / distance,
                    offset[1] / distance,
                    offset[2] / distance,
                }
                : std::array<double, 3>{1.0, 0.0, 0.0};
            const auto penetration_depth = radius_sum - distance;
            const std::array<double, 3> first_surface_point{
                static_cast<double>(first.position.x) +
                    normal[0] *
                        static_cast<double>(
                            colliders[first_index].radius),
                static_cast<double>(first.position.y) +
                    normal[1] *
                        static_cast<double>(
                            colliders[first_index].radius),
                static_cast<double>(first.position.z) +
                    normal[2] *
                        static_cast<double>(
                            colliders[first_index].radius),
            };
            const std::array<double, 3> second_surface_point{
                static_cast<double>(second.position.x) -
                    normal[0] *
                        static_cast<double>(
                            colliders[second_index].radius),
                static_cast<double>(second.position.y) -
                    normal[1] *
                        static_cast<double>(
                            colliders[second_index].radius),
                static_cast<double>(second.position.z) -
                    normal[2] *
                        static_cast<double>(
                            colliders[second_index].radius),
            };
            const std::array<double, 3> contact_position{
                (first_surface_point[0] + second_surface_point[0]) *
                    0.5,
                (first_surface_point[1] + second_surface_point[1]) *
                    0.5,
                (first_surface_point[2] + second_surface_point[2]) *
                    0.5,
            };

            if (!representable_components(normal) ||
                !representable_float(penetration_depth) ||
                !representable_components(contact_position) ||
                step.contact_count >= sphere_pair_capacity) {
                return core::Result<
                    SphereBodyCollisionStep>::failure(
                        collision_error(
                            core::ErrorCode::unavailable,
                            "Sphere pair resolution exceeded finite "
                            "float or contact capacity"));
            }

            step.contacts[step.contact_count] =
                SphereBodyPairContact{
                    .first_body_index = first_index,
                    .second_body_index = second_index,
                    .normal = to_float3(normal),
                    .penetration_depth =
                        static_cast<float>(penetration_depth),
                };
            constraints[step.contact_count] = ContactConstraint{
                .first_body_index = first_index,
                .second_body_index = second_index,
                .normal = to_float3(normal),
                .material = {
                    .restitution = settings.restitution,
                    .static_friction = settings.static_friction,
                    .dynamic_friction = settings.dynamic_friction,
                },
                .points = {{
                    ContactConstraintPoint{
                        .position = to_float3(contact_position),
                        .penetration_depth =
                            static_cast<float>(penetration_depth),
                    },
                }},
                .point_count = 1,
            };
            ++step.contact_count;
        }
    }

    if (step.contact_count != 0U) {
        auto solver_result = solve_contact_constraints(
            std::span<RigidBodyState>{
                candidate.data(),
                active_body_count,
            },
            std::span<const ContactBodyMassProperties>{
                solver_masses.data(),
                active_body_count,
            },
            std::span<const ContactConstraint>{
                constraints.data(),
                step.contact_count,
            },
            settings.solver);
        if (!solver_result) {
            return core::Result<
                SphereBodyCollisionStep>::failure(
                    std::move(solver_result).error());
        }
        const auto& solver_step = solver_result.value();
        if (solver_step.constraint_count != step.contact_count) {
            return core::Result<
                SphereBodyCollisionStep>::failure(
                    collision_error(
                        core::ErrorCode::invalid_state,
                        "Contact solver returned an inconsistent sphere "
                        "pair result count"));
        }
        for (std::size_t contact_index = 0;
             contact_index < step.contact_count;
             ++contact_index) {
            const auto& constraint_result =
                solver_step.constraints[contact_index];
            if (constraint_result.point_count != 1U) {
                return core::Result<
                    SphereBodyCollisionStep>::failure(
                        collision_error(
                            core::ErrorCode::invalid_state,
                            "Contact solver returned an inconsistent "
                            "sphere pair point count"));
            }
            step.contacts[contact_index]
                .relative_normal_velocity_before_resolution =
                    constraint_result.points[0]
                        .relative_normal_velocity_before_resolution;
            step.contacts[contact_index]
                .normal_impulse_magnitude =
                    constraint_result.points[0]
                        .normal_impulse_magnitude;
        }
    }

    states = candidate;
    return core::Result<SphereBodyCollisionStep>::success(step);
}

} // namespace shark::physics
