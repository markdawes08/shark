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
    const SphereBodyCollisionSettings settings) noexcept
{
    return std::isfinite(settings.restitution) &&
        settings.restitution >= 0.0F &&
        settings.restitution <= 1.0F;
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

} // namespace

core::Result<SphereBodyCollisionStep>
resolve_sphere_body_collisions(
    SphereBodyStates& states,
    const SphereColliders& colliders,
    const std::size_t active_body_count,
    const SphereBodyCollisionSettings settings)
{
    if (active_body_count > sphere_body_capacity ||
        !valid_settings(settings)) {
        return core::Result<SphereBodyCollisionStep>::failure(
            collision_error(
                core::ErrorCode::invalid_argument,
                "Sphere body collision requires at most four bodies "
                "and finite restitution in [0, 1]"));
    }
    for (std::size_t index = 0;
         index < active_body_count;
         ++index) {
        if (!is_valid(states[index]) ||
            !is_valid(colliders[index])) {
            return core::Result<SphereBodyCollisionStep>::failure(
                collision_error(
                    core::ErrorCode::invalid_argument,
                    "Sphere body collision requires finite active "
                    "states and finite positive radii"));
        }
    }

    auto candidate = states;
    SphereBodyCollisionStep step;
    for (std::size_t first_index = 0;
         first_index < active_body_count;
         ++first_index) {
        for (std::size_t second_index = first_index + 1U;
             second_index < active_body_count;
             ++second_index) {
            ++step.tested_pair_count;
            auto& first = candidate[first_index];
            auto& second = candidate[second_index];

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
            const auto half_correction = penetration_depth * 0.5;
            const std::array<double, 3> first_position{
                static_cast<double>(first.position.x) -
                    normal[0] * half_correction,
                static_cast<double>(first.position.y) -
                    normal[1] * half_correction,
                static_cast<double>(first.position.z) -
                    normal[2] * half_correction,
            };
            const std::array<double, 3> second_position{
                static_cast<double>(second.position.x) +
                    normal[0] * half_correction,
                static_cast<double>(second.position.y) +
                    normal[1] * half_correction,
                static_cast<double>(second.position.z) +
                    normal[2] * half_correction,
            };
            const std::array<double, 3> relative_velocity{
                static_cast<double>(second.linear_velocity.x) -
                    static_cast<double>(first.linear_velocity.x),
                static_cast<double>(second.linear_velocity.y) -
                    static_cast<double>(first.linear_velocity.y),
                static_cast<double>(second.linear_velocity.z) -
                    static_cast<double>(first.linear_velocity.z),
            };
            const auto relative_normal_velocity =
                relative_velocity[0] * normal[0] +
                relative_velocity[1] * normal[1] +
                relative_velocity[2] * normal[2];
            const auto normal_impulse =
                relative_normal_velocity < 0.0
                ? -(1.0 +
                    static_cast<double>(settings.restitution)) *
                    relative_normal_velocity * 0.5
                : 0.0;
            const std::array<double, 3> first_velocity{
                static_cast<double>(first.linear_velocity.x) -
                    normal[0] * normal_impulse,
                static_cast<double>(first.linear_velocity.y) -
                    normal[1] * normal_impulse,
                static_cast<double>(first.linear_velocity.z) -
                    normal[2] * normal_impulse,
            };
            const std::array<double, 3> second_velocity{
                static_cast<double>(second.linear_velocity.x) +
                    normal[0] * normal_impulse,
                static_cast<double>(second.linear_velocity.y) +
                    normal[1] * normal_impulse,
                static_cast<double>(second.linear_velocity.z) +
                    normal[2] * normal_impulse,
            };

            if (!representable_components(normal) ||
                !representable_float(penetration_depth) ||
                !representable_float(relative_normal_velocity) ||
                !representable_float(normal_impulse) ||
                !representable_components(first_position) ||
                !representable_components(second_position) ||
                !representable_components(first_velocity) ||
                !representable_components(second_velocity) ||
                step.contact_count >= sphere_pair_capacity) {
                return core::Result<
                    SphereBodyCollisionStep>::failure(
                        collision_error(
                            core::ErrorCode::unavailable,
                            "Sphere pair resolution exceeded finite "
                            "float or contact capacity"));
            }

            first.position = to_float3(first_position);
            second.position = to_float3(second_position);
            first.linear_velocity = to_float3(first_velocity);
            second.linear_velocity = to_float3(second_velocity);
            step.contacts[step.contact_count] =
                SphereBodyPairContact{
                    .first_body_index = first_index,
                    .second_body_index = second_index,
                    .normal = to_float3(normal),
                    .penetration_depth =
                        static_cast<float>(penetration_depth),
                    .relative_normal_velocity_before_resolution =
                        static_cast<float>(
                            relative_normal_velocity),
                    .normal_impulse_magnitude =
                        static_cast<float>(normal_impulse),
                };
            ++step.contact_count;
        }
    }

    states = candidate;
    return core::Result<SphereBodyCollisionStep>::success(step);
}

} // namespace shark::physics
