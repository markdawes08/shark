#include <shark/physics/contact_constraint.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::physics {
namespace {

inline constexpr double unit_vector_tolerance = 0.00001;
inline constexpr double tangent_speed_squared_tolerance = 1.0e-20;

struct Double3 final {
    double x{};
    double y{};
    double z{};
};

struct ScratchBody final {
    Double3 position{};
    math::Quaternion orientation{};
    Double3 linear_velocity{};
    Double3 angular_velocity{};
    double inverse_mass{};
    Double3 local_inverse_inertia{};
};

struct WorkingPoint final {
    Double3 position{};
    double relative_normal_velocity_before_resolution{};
    double restitution_target{};
    double warm_start_normal_impulse{};
    Double3 warm_start_tangent_impulse{};
    double accumulated_normal_impulse{};
    Double3 accumulated_tangent_impulse{};
};

struct WorkingConstraint final {
    Double3 normal{};
    std::array<WorkingPoint, contact_points_per_constraint> points{};
    std::size_t point_count{};
};

[[nodiscard]] core::Error contact_solver_error(
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
            static_cast<double>(std::numeric_limits<float>::max());
}

[[nodiscard]] bool is_finite(const Double3 value) noexcept
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] Double3 to_double(
    const math::Float3 value) noexcept
{
    return {
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z),
    };
}

[[nodiscard]] Double3 add(
    const Double3 first,
    const Double3 second) noexcept
{
    return {
        first.x + second.x,
        first.y + second.y,
        first.z + second.z,
    };
}

[[nodiscard]] Double3 subtract(
    const Double3 first,
    const Double3 second) noexcept
{
    return {
        first.x - second.x,
        first.y - second.y,
        first.z - second.z,
    };
}

[[nodiscard]] Double3 negate(const Double3 value) noexcept
{
    return {-value.x, -value.y, -value.z};
}

[[nodiscard]] Double3 scale(
    const Double3 value,
    const double factor) noexcept
{
    return {
        value.x * factor,
        value.y * factor,
        value.z * factor,
    };
}

[[nodiscard]] double dot(
    const Double3 first,
    const Double3 second) noexcept
{
    return first.x * second.x +
        first.y * second.y +
        first.z * second.z;
}

[[nodiscard]] Double3 cross(
    const Double3 first,
    const Double3 second) noexcept
{
    return {
        first.y * second.z - first.z * second.y,
        first.z * second.x - first.x * second.z,
        first.x * second.y - first.y * second.x,
    };
}

[[nodiscard]] double length_squared(
    const Double3 value) noexcept
{
    return dot(value, value);
}

[[nodiscard]] Double3 rotate(
    const math::Quaternion orientation,
    const Double3 value) noexcept
{
    const Double3 quaternion_vector{
        static_cast<double>(orientation.x),
        static_cast<double>(orientation.y),
        static_cast<double>(orientation.z),
    };
    const auto twice_cross = scale(
        cross(quaternion_vector, value),
        2.0);
    return add(
        add(
            value,
            scale(
                twice_cross,
                static_cast<double>(orientation.w))),
        cross(quaternion_vector, twice_cross));
}

[[nodiscard]] Double3 inverse_rotate(
    const math::Quaternion orientation,
    const Double3 value) noexcept
{
    return rotate(
        math::Quaternion{
            -orientation.x,
            -orientation.y,
            -orientation.z,
            orientation.w,
        },
        value);
}

[[nodiscard]] Double3 apply_world_inverse_inertia(
    const ScratchBody& body,
    const Double3 value) noexcept
{
    const auto local = inverse_rotate(body.orientation, value);
    return rotate(
        body.orientation,
        Double3{
            local.x * body.local_inverse_inertia.x,
            local.y * body.local_inverse_inertia.y,
            local.z * body.local_inverse_inertia.z,
        });
}

[[nodiscard]] bool is_static_body(
    const std::size_t body_index) noexcept
{
    return body_index == static_contact_body_index;
}

[[nodiscard]] bool valid_mass_properties(
    const ContactBodyMassProperties& properties) noexcept
{
    return std::isfinite(properties.inverse_mass) &&
        properties.inverse_mass > 0.0F &&
        math::is_finite(properties.local_inverse_inertia) &&
        properties.local_inverse_inertia.x >= 0.0F &&
        properties.local_inverse_inertia.y >= 0.0F &&
        properties.local_inverse_inertia.z >= 0.0F;
}

[[nodiscard]] bool valid_material(
    const ContactMaterial material) noexcept
{
    return std::isfinite(material.restitution) &&
        material.restitution >= 0.0F &&
        material.restitution <= 1.0F &&
        std::isfinite(material.static_friction) &&
        material.static_friction >= 0.0F &&
        std::isfinite(material.dynamic_friction) &&
        material.dynamic_friction >= 0.0F &&
        material.dynamic_friction <= material.static_friction;
}

[[nodiscard]] bool valid_settings(
    const ContactSolverSettings settings) noexcept
{
    return settings.velocity_iterations > 0U &&
        settings.velocity_iterations <=
            max_contact_solver_velocity_iterations &&
        std::isfinite(settings.restitution_velocity_threshold) &&
        settings.restitution_velocity_threshold >= 0.0F &&
        std::isfinite(settings.penetration_slop) &&
        settings.penetration_slop >= 0.0F &&
        std::isfinite(settings.penetration_correction_fraction) &&
        settings.penetration_correction_fraction >= 0.0F &&
        settings.penetration_correction_fraction <= 1.0F &&
        std::isfinite(settings.maximum_position_correction) &&
        settings.maximum_position_correction >= 0.0F;
}

[[nodiscard]] bool valid_unit_normal(
    const math::Float3 normal) noexcept
{
    if (!math::is_finite(normal)) {
        return false;
    }
    const auto normal_length_squared =
        length_squared(to_double(normal));
    if (!std::isfinite(normal_length_squared) ||
        normal_length_squared <= 0.0) {
        return false;
    }
    return std::abs(std::sqrt(normal_length_squared) - 1.0) <=
        unit_vector_tolerance;
}

[[nodiscard]] Double3 point_velocity(
    const std::array<ScratchBody, contact_solver_body_capacity>& bodies,
    const std::size_t body_index,
    const Double3 point) noexcept
{
    if (is_static_body(body_index)) {
        return {};
    }
    const auto& body = bodies[body_index];
    return add(
        body.linear_velocity,
        cross(
            body.angular_velocity,
            subtract(point, body.position)));
}

[[nodiscard]] Double3 relative_velocity(
    const std::array<ScratchBody, contact_solver_body_capacity>& bodies,
    const ContactConstraint& constraint,
    const Double3 point) noexcept
{
    return subtract(
        point_velocity(
            bodies,
            constraint.second_body_index,
            point),
        point_velocity(
            bodies,
            constraint.first_body_index,
            point));
}

[[nodiscard]] double endpoint_effective_inverse_mass(
    const std::array<ScratchBody, contact_solver_body_capacity>& bodies,
    const std::size_t body_index,
    const Double3 point,
    const Double3 direction) noexcept
{
    if (is_static_body(body_index)) {
        return 0.0;
    }
    const auto& body = bodies[body_index];
    const auto lever_arm = subtract(point, body.position);
    const auto angular_jacobian = cross(lever_arm, direction);
    return body.inverse_mass + dot(
        angular_jacobian,
        apply_world_inverse_inertia(body, angular_jacobian));
}

[[nodiscard]] double effective_inverse_mass(
    const std::array<ScratchBody, contact_solver_body_capacity>& bodies,
    const ContactConstraint& constraint,
    const Double3 point,
    const Double3 direction) noexcept
{
    return endpoint_effective_inverse_mass(
               bodies,
               constraint.first_body_index,
               point,
               direction) +
        endpoint_effective_inverse_mass(
               bodies,
               constraint.second_body_index,
               point,
               direction);
}

[[nodiscard]] bool apply_impulse_to_body(
    ScratchBody& body,
    const Double3 point,
    const Double3 impulse) noexcept
{
    body.linear_velocity = add(
        body.linear_velocity,
        scale(impulse, body.inverse_mass));
    const auto lever_arm = subtract(point, body.position);
    body.angular_velocity = add(
        body.angular_velocity,
        apply_world_inverse_inertia(
            body,
            cross(lever_arm, impulse)));
    return is_finite(body.linear_velocity) &&
        is_finite(body.angular_velocity);
}

[[nodiscard]] bool apply_pair_impulse(
    std::array<ScratchBody, contact_solver_body_capacity>& bodies,
    const ContactConstraint& constraint,
    const Double3 point,
    const Double3 impulse_toward_second) noexcept
{
    if (!is_static_body(constraint.first_body_index) &&
        !apply_impulse_to_body(
            bodies[constraint.first_body_index],
            point,
            negate(impulse_toward_second))) {
        return false;
    }
    if (!is_static_body(constraint.second_body_index) &&
        !apply_impulse_to_body(
            bodies[constraint.second_body_index],
            point,
            impulse_toward_second)) {
        return false;
    }
    return true;
}

[[nodiscard]] double endpoint_inverse_mass(
    const std::array<ScratchBody, contact_solver_body_capacity>& bodies,
    const std::size_t body_index) noexcept
{
    return is_static_body(body_index)
        ? 0.0
        : bodies[body_index].inverse_mass;
}

[[nodiscard]] bool translate_body(
    ScratchBody& body,
    const Double3 translation) noexcept
{
    body.position = add(body.position, translation);
    return is_finite(body.position);
}

[[nodiscard]] bool store_float3(
    const Double3 value,
    math::Float3& destination) noexcept
{
    if (!representable_float(value.x) ||
        !representable_float(value.y) ||
        !representable_float(value.z)) {
        return false;
    }
    destination = {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z),
    };
    return true;
}

} // namespace

core::Result<ContactSolverStep> solve_contact_constraints(
    const std::span<RigidBodyState> states,
    const std::span<const ContactBodyMassProperties> mass_properties,
    const std::span<const ContactConstraint> constraints,
    const std::span<const ContactConstraintWarmStart> warm_starts,
    const ContactSolverSettings settings)
{
    if (states.size() > contact_solver_body_capacity ||
        mass_properties.size() != states.size() ||
        constraints.size() > contact_constraint_capacity ||
        warm_starts.size() != constraints.size() ||
        !valid_settings(settings)) {
        return core::Result<ContactSolverStep>::failure(
            contact_solver_error(
                core::ErrorCode::invalid_argument,
                "Contact solving requires matching bounded body data, "
                "aligned warm starts, at most ten constraints, and "
                "valid bounded settings"));
    }

    for (std::size_t body_index = 0;
         body_index < states.size();
         ++body_index) {
        if (!is_valid(states[body_index]) ||
            !valid_mass_properties(mass_properties[body_index])) {
            return core::Result<ContactSolverStep>::failure(
                contact_solver_error(
                    core::ErrorCode::invalid_argument,
                    "Contact solving requires finite valid body state, "
                    "positive inverse mass, and nonnegative local "
                    "inverse inertia"));
        }
    }

    for (std::size_t constraint_index = 0;
         constraint_index < constraints.size();
         ++constraint_index) {
        const auto& constraint = constraints[constraint_index];
        const auto& warm_start = warm_starts[constraint_index];
        const auto first_is_static =
            is_static_body(constraint.first_body_index);
        const auto second_is_static =
            is_static_body(constraint.second_body_index);
        const auto first_is_valid = first_is_static ||
            constraint.first_body_index < states.size();
        const auto second_is_valid = second_is_static ||
            constraint.second_body_index < states.size();
        if (!first_is_valid ||
            !second_is_valid ||
            (first_is_static && second_is_static) ||
            (!first_is_static &&
             !second_is_static &&
             constraint.first_body_index ==
                 constraint.second_body_index) ||
            !valid_unit_normal(constraint.normal) ||
            !valid_material(constraint.material) ||
            constraint.point_count == 0U ||
            constraint.point_count >
                contact_points_per_constraint ||
            warm_start.point_count != constraint.point_count) {
            return core::Result<ContactSolverStep>::failure(
                contact_solver_error(
                    core::ErrorCode::invalid_argument,
                    "Contact constraints require distinct valid "
                    "endpoints, a unit first-to-second normal, valid "
                    "material coefficients, and aligned one-to-four-"
                    "point warm starts"));
        }
        for (std::size_t point_index = 0;
             point_index < constraint.point_count;
             ++point_index) {
            const auto& point = constraint.points[point_index];
            const auto& point_warm_start =
                warm_start.points[point_index];
            if (!math::is_finite(point.position) ||
                !std::isfinite(point.penetration_depth) ||
                point.penetration_depth < 0.0F ||
                !std::isfinite(
                    point_warm_start.normal_impulse_magnitude) ||
                point_warm_start.normal_impulse_magnitude < 0.0F ||
                !math::is_finite(
                    point_warm_start.tangent_impulse)) {
                return core::Result<ContactSolverStep>::failure(
                    contact_solver_error(
                        core::ErrorCode::invalid_argument,
                        "Contact points require finite world positions "
                        "and nonnegative finite penetration/normal "
                        "warm impulses plus finite tangent warm "
                        "impulses"));
            }
        }
    }

    std::array<ScratchBody, contact_solver_body_capacity> bodies{};
    for (std::size_t body_index = 0;
         body_index < states.size();
         ++body_index) {
        const auto& state = states[body_index];
        const auto& properties = mass_properties[body_index];
        bodies[body_index] = ScratchBody{
            .position = to_double(state.position),
            .orientation = state.orientation,
            .linear_velocity = to_double(state.linear_velocity),
            .angular_velocity = to_double(state.angular_velocity),
            .inverse_mass =
                static_cast<double>(properties.inverse_mass),
            .local_inverse_inertia =
                to_double(properties.local_inverse_inertia),
        };
    }

    std::array<
        WorkingConstraint,
        contact_constraint_capacity>
        working_constraints{};
    for (std::size_t constraint_index = 0;
         constraint_index < constraints.size();
         ++constraint_index) {
        const auto& constraint = constraints[constraint_index];
        auto& working = working_constraints[constraint_index];
        working.normal = to_double(constraint.normal);
        working.point_count = constraint.point_count;
        for (std::size_t point_index = 0;
             point_index < constraint.point_count;
             ++point_index) {
            auto& working_point = working.points[point_index];
            working_point.position =
                to_double(constraint.points[point_index].position);
            const auto initial_relative_velocity = relative_velocity(
                bodies,
                constraint,
                working_point.position);
            const auto initial_normal_velocity = dot(
                initial_relative_velocity,
                working.normal);
            if (!is_finite(initial_relative_velocity) ||
                !representable_float(initial_normal_velocity)) {
                return core::Result<ContactSolverStep>::failure(
                    contact_solver_error(
                        core::ErrorCode::unavailable,
                        "Initial contact-point velocity exceeded "
                        "finite float range"));
            }
            working_point
                .relative_normal_velocity_before_resolution =
                initial_normal_velocity;
            working_point.restitution_target =
                initial_normal_velocity <
                    -static_cast<double>(
                        settings.restitution_velocity_threshold)
                ? -static_cast<double>(
                      constraint.material.restitution) *
                    initial_normal_velocity
                : 0.0;
        }
    }

    // Restitution targets above were captured from the cold incoming state.
    // Only now may cached accumulators affect velocities.
    for (std::size_t constraint_index = 0;
         constraint_index < constraints.size();
         ++constraint_index) {
        const auto& constraint = constraints[constraint_index];
        const auto& warm_start = warm_starts[constraint_index];
        auto& working = working_constraints[constraint_index];
        for (std::size_t point_index = 0;
             point_index < constraint.point_count;
             ++point_index) {
            const auto& supplied = warm_start.points[point_index];
            auto& point = working.points[point_index];
            const auto normal_impulse = static_cast<double>(
                supplied.normal_impulse_magnitude);
            auto tangent_impulse = to_double(supplied.tangent_impulse);
            tangent_impulse = subtract(
                tangent_impulse,
                scale(
                    working.normal,
                    dot(tangent_impulse, working.normal)));

            const auto tangent_length_squared =
                length_squared(tangent_impulse);
            const auto static_limit = static_cast<double>(
                constraint.material.static_friction) *
                normal_impulse;
            const auto dynamic_limit = static_cast<double>(
                constraint.material.dynamic_friction) *
                normal_impulse;
            if (!std::isfinite(tangent_length_squared) ||
                tangent_length_squared < 0.0 ||
                !std::isfinite(static_limit) ||
                !std::isfinite(dynamic_limit)) {
                return core::Result<ContactSolverStep>::failure(
                    contact_solver_error(
                        core::ErrorCode::unavailable,
                        "Warm-start contact impulse exceeded finite "
                        "range"));
            }
            const auto tangent_length =
                std::sqrt(tangent_length_squared);
            if (tangent_length > static_limit) {
                tangent_impulse = tangent_length > 0.0 &&
                        dynamic_limit > 0.0
                    ? scale(
                          tangent_impulse,
                          dynamic_limit / tangent_length)
                    : Double3{};
            }

            if (!apply_pair_impulse(
                    bodies,
                    constraint,
                    point.position,
                    scale(working.normal, normal_impulse)) ||
                !apply_pair_impulse(
                    bodies,
                    constraint,
                    point.position,
                    tangent_impulse)) {
                return core::Result<ContactSolverStep>::failure(
                    contact_solver_error(
                        core::ErrorCode::unavailable,
                        "Warm-start contact application exceeded "
                        "finite range"));
            }
            point.warm_start_normal_impulse = normal_impulse;
            point.warm_start_tangent_impulse = tangent_impulse;
            point.accumulated_normal_impulse = normal_impulse;
            point.accumulated_tangent_impulse = tangent_impulse;
        }
    }

    for (std::uint32_t iteration = 0;
         iteration < settings.velocity_iterations;
         ++iteration) {
        for (std::size_t constraint_index = 0;
             constraint_index < constraints.size();
             ++constraint_index) {
            const auto& constraint = constraints[constraint_index];
            auto& working = working_constraints[constraint_index];
            for (std::size_t point_index = 0;
                 point_index < constraint.point_count;
                 ++point_index) {
                auto& point = working.points[point_index];
                auto current_relative_velocity = relative_velocity(
                    bodies,
                    constraint,
                    point.position);
                const auto current_normal_velocity = dot(
                    current_relative_velocity,
                    working.normal);
                const auto normal_effective_inverse_mass =
                    effective_inverse_mass(
                        bodies,
                        constraint,
                        point.position,
                        working.normal);
                if (!is_finite(current_relative_velocity) ||
                    !std::isfinite(current_normal_velocity) ||
                    !std::isfinite(
                        normal_effective_inverse_mass) ||
                    normal_effective_inverse_mass <= 0.0) {
                    return core::Result<ContactSolverStep>::failure(
                        contact_solver_error(
                            core::ErrorCode::unavailable,
                            "Normal contact solve exceeded finite "
                            "range or lost effective mass"));
                }

                const auto normal_impulse_delta =
                    (point.restitution_target -
                     current_normal_velocity) /
                    normal_effective_inverse_mass;
                const auto next_normal_impulse = std::max(
                    0.0,
                    point.accumulated_normal_impulse +
                        normal_impulse_delta);
                const auto applied_normal_impulse =
                    next_normal_impulse -
                    point.accumulated_normal_impulse;
                if (!std::isfinite(next_normal_impulse) ||
                    !apply_pair_impulse(
                        bodies,
                        constraint,
                        point.position,
                        scale(
                            working.normal,
                            applied_normal_impulse))) {
                    return core::Result<ContactSolverStep>::failure(
                        contact_solver_error(
                            core::ErrorCode::unavailable,
                            "Normal contact impulse exceeded finite "
                            "range"));
                }
                point.accumulated_normal_impulse =
                    next_normal_impulse;

                current_relative_velocity = relative_velocity(
                    bodies,
                    constraint,
                    point.position);
                const auto tangent_velocity = subtract(
                    current_relative_velocity,
                    scale(
                        working.normal,
                        dot(
                            current_relative_velocity,
                            working.normal)));
                const auto tangent_speed_squared =
                    length_squared(tangent_velocity);
                if (!is_finite(current_relative_velocity) ||
                    !std::isfinite(tangent_speed_squared) ||
                    tangent_speed_squared < 0.0) {
                    return core::Result<ContactSolverStep>::failure(
                        contact_solver_error(
                            core::ErrorCode::unavailable,
                            "Tangent contact velocity exceeded finite "
                            "range"));
                }

                auto next_tangent_impulse =
                    point.accumulated_tangent_impulse;
                if (tangent_speed_squared >
                    tangent_speed_squared_tolerance) {
                    const auto tangent_speed =
                        std::sqrt(tangent_speed_squared);
                    const auto tangent_direction = scale(
                        tangent_velocity,
                        1.0 / tangent_speed);
                    const auto tangent_effective_inverse_mass =
                        effective_inverse_mass(
                            bodies,
                            constraint,
                            point.position,
                            tangent_direction);
                    if (!std::isfinite(
                            tangent_effective_inverse_mass) ||
                        tangent_effective_inverse_mass <= 0.0) {
                        return core::Result<
                            ContactSolverStep>::failure(
                                contact_solver_error(
                                    core::ErrorCode::unavailable,
                                    "Tangent contact solve lost "
                                    "finite effective mass"));
                    }
                    next_tangent_impulse = add(
                        next_tangent_impulse,
                        scale(
                            tangent_direction,
                            -tangent_speed /
                                tangent_effective_inverse_mass));
                }

                const auto candidate_tangent_length_squared =
                    length_squared(next_tangent_impulse);
                const auto static_limit =
                    static_cast<double>(
                        constraint.material.static_friction) *
                    point.accumulated_normal_impulse;
                const auto dynamic_limit =
                    static_cast<double>(
                        constraint.material.dynamic_friction) *
                    point.accumulated_normal_impulse;
                if (!std::isfinite(
                        candidate_tangent_length_squared) ||
                    candidate_tangent_length_squared < 0.0 ||
                    !std::isfinite(static_limit) ||
                    !std::isfinite(dynamic_limit)) {
                    return core::Result<ContactSolverStep>::failure(
                        contact_solver_error(
                            core::ErrorCode::unavailable,
                            "Coulomb friction limits exceeded finite "
                            "range"));
                }
                const auto candidate_tangent_length =
                    std::sqrt(candidate_tangent_length_squared);
                if (candidate_tangent_length > static_limit) {
                    next_tangent_impulse =
                        candidate_tangent_length > 0.0 &&
                            dynamic_limit > 0.0
                        ? scale(
                              next_tangent_impulse,
                              dynamic_limit /
                                  candidate_tangent_length)
                        : Double3{};
                }

                const auto applied_tangent_impulse = subtract(
                    next_tangent_impulse,
                    point.accumulated_tangent_impulse);
                if (!is_finite(next_tangent_impulse) ||
                    !apply_pair_impulse(
                        bodies,
                        constraint,
                        point.position,
                        applied_tangent_impulse)) {
                    return core::Result<ContactSolverStep>::failure(
                        contact_solver_error(
                            core::ErrorCode::unavailable,
                            "Tangent contact impulse exceeded finite "
                            "range"));
                }
                point.accumulated_tangent_impulse =
                    next_tangent_impulse;
            }
        }
    }

    for (std::size_t constraint_index = 0;
         constraint_index < constraints.size();
         ++constraint_index) {
        const auto& constraint = constraints[constraint_index];
        double deepest_penetration = 0.0;
        for (std::size_t point_index = 0;
             point_index < constraint.point_count;
             ++point_index) {
            deepest_penetration = std::max(
                deepest_penetration,
                static_cast<double>(
                    constraint.points[point_index]
                        .penetration_depth));
        }
        const auto excess_penetration = std::max(
            0.0,
            deepest_penetration -
                static_cast<double>(settings.penetration_slop));
        const auto correction_magnitude = std::min(
            excess_penetration *
                static_cast<double>(
                    settings.penetration_correction_fraction),
            static_cast<double>(
                settings.maximum_position_correction));
        const auto first_inverse_mass = endpoint_inverse_mass(
            bodies,
            constraint.first_body_index);
        const auto second_inverse_mass = endpoint_inverse_mass(
            bodies,
            constraint.second_body_index);
        const auto inverse_mass_sum =
            first_inverse_mass + second_inverse_mass;
        if (!std::isfinite(correction_magnitude) ||
            !std::isfinite(inverse_mass_sum) ||
            inverse_mass_sum <= 0.0) {
            return core::Result<ContactSolverStep>::failure(
                contact_solver_error(
                    core::ErrorCode::unavailable,
                    "Contact position correction exceeded finite "
                    "range or lost inverse mass"));
        }
        if (correction_magnitude <= 0.0) {
            continue;
        }

        const auto normal =
            working_constraints[constraint_index].normal;
        if (!is_static_body(constraint.first_body_index) &&
            !translate_body(
                bodies[constraint.first_body_index],
                scale(
                    normal,
                    -correction_magnitude * first_inverse_mass /
                        inverse_mass_sum))) {
            return core::Result<ContactSolverStep>::failure(
                contact_solver_error(
                    core::ErrorCode::unavailable,
                    "First-body position correction exceeded finite "
                    "range"));
        }
        if (!is_static_body(constraint.second_body_index) &&
            !translate_body(
                bodies[constraint.second_body_index],
                scale(
                    normal,
                    correction_magnitude * second_inverse_mass /
                        inverse_mass_sum))) {
            return core::Result<ContactSolverStep>::failure(
                contact_solver_error(
                    core::ErrorCode::unavailable,
                    "Second-body position correction exceeded finite "
                    "range"));
        }
    }

    ContactSolverStep step{
        .constraint_count = constraints.size(),
        .completed_velocity_iterations =
            settings.velocity_iterations,
    };
    for (std::size_t constraint_index = 0;
         constraint_index < constraints.size();
         ++constraint_index) {
        const auto& working =
            working_constraints[constraint_index];
        auto& result = step.constraints[constraint_index];
        result.point_count = working.point_count;
        for (std::size_t point_index = 0;
             point_index < working.point_count;
             ++point_index) {
            const auto& point = working.points[point_index];
            auto& impulse = result.points[point_index];
            if (!representable_float(
                    point
                        .relative_normal_velocity_before_resolution) ||
                !representable_float(
                    point.warm_start_normal_impulse) ||
                !store_float3(
                    point.warm_start_tangent_impulse,
                    impulse.warm_start_tangent_impulse) ||
                !representable_float(
                    point.accumulated_normal_impulse) ||
                !store_float3(
                    point.accumulated_tangent_impulse,
                    impulse.tangent_impulse)) {
                return core::Result<ContactSolverStep>::failure(
                    contact_solver_error(
                        core::ErrorCode::unavailable,
                        "Contact impulse report exceeded finite float "
                        "range"));
            }
            impulse.relative_normal_velocity_before_resolution =
                static_cast<float>(
                    point
                        .relative_normal_velocity_before_resolution);
            impulse.warm_start_normal_impulse_magnitude =
                static_cast<float>(
                    point.warm_start_normal_impulse);
            impulse.normal_impulse_magnitude =
                static_cast<float>(
                    point.accumulated_normal_impulse);
        }
    }

    std::array<RigidBodyState, contact_solver_body_capacity>
        committed_states{};
    for (std::size_t body_index = 0;
         body_index < states.size();
         ++body_index) {
        committed_states[body_index] = states[body_index];
        auto& committed = committed_states[body_index];
        if (!store_float3(
                bodies[body_index].position,
                committed.position) ||
            !store_float3(
                bodies[body_index].linear_velocity,
                committed.linear_velocity) ||
            !store_float3(
                bodies[body_index].angular_velocity,
                committed.angular_velocity) ||
            !is_valid(committed)) {
            return core::Result<ContactSolverStep>::failure(
                contact_solver_error(
                    core::ErrorCode::unavailable,
                    "Solved body state exceeded finite float range"));
        }
    }

    for (std::size_t body_index = 0;
         body_index < states.size();
         ++body_index) {
        states[body_index] = committed_states[body_index];
    }
    return core::Result<ContactSolverStep>::success(step);
}

core::Result<ContactSolverStep> solve_contact_constraints(
    const std::span<RigidBodyState> states,
    const std::span<const ContactBodyMassProperties> mass_properties,
    const std::span<const ContactConstraint> constraints,
    const ContactSolverSettings settings)
{
    if (constraints.size() > contact_constraint_capacity) {
        return solve_contact_constraints(
            states,
            mass_properties,
            constraints,
            std::span<const ContactConstraintWarmStart>{},
            settings);
    }

    std::array<
        ContactConstraintWarmStart,
        contact_constraint_capacity>
        warm_starts{};
    for (std::size_t constraint_index = 0;
         constraint_index < constraints.size();
         ++constraint_index) {
        warm_starts[constraint_index].point_count =
            constraints[constraint_index].point_count;
    }
    return solve_contact_constraints(
        states,
        mass_properties,
        constraints,
        std::span<const ContactConstraintWarmStart>{
            warm_starts.data(),
            constraints.size(),
        },
        settings);
}

} // namespace shark::physics
