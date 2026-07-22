#include <shark/physics/sphere_terrain_contact.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::physics {
namespace {

inline constexpr double contact_tolerance_meters = 0.00001;

[[nodiscard]] core::Error contact_error(
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
    const SphereTerrainContactSettings& settings) noexcept
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

core::Result<SphereTerrainStep>
advance_sphere_against_terrain(
    BallisticBodyState& state,
    const SphereCollider& collider,
    const SolidSphereMassProperties& mass_properties,
    const terrain::HeightTileSurface& terrain_surface,
    const math::Float3 acceleration,
    const float fixed_delta_seconds,
    const SphereTerrainContactSettings settings)
{
    if (!is_valid(collider) ||
        !is_valid(mass_properties) ||
        mass_properties.radius != collider.radius ||
        !valid_settings(settings)) {
        return core::Result<SphereTerrainStep>::failure(
            contact_error(
                core::ErrorCode::invalid_argument,
                "Sphere terrain contact requires a finite positive "
                "matching collider/mass pair and valid material"));
    }

    auto candidate = state;
    auto integration_result = advance_ballistic_body(
        candidate,
        acceleration,
        fixed_delta_seconds);
    if (!integration_result) {
        return core::Result<SphereTerrainStep>::failure(
            std::move(integration_result).error());
    }

    const auto surface = terrain_surface.sample_lod0_surface(
        candidate.position.x,
        candidate.position.z);
    if (!surface.has_value()) {
        state = candidate;
        return core::Result<SphereTerrainStep>::success({});
    }

    const auto& point = surface->position;
    const auto& normal = surface->normal;
    if (!math::is_finite(point) ||
        !math::is_finite(normal) ||
        normal.y <= 0.0F) {
        return core::Result<SphereTerrainStep>::failure(
            contact_error(
                core::ErrorCode::invalid_state,
                "Canonical terrain returned an invalid upward face"));
    }

    const auto offset_x =
        static_cast<double>(candidate.position.x) -
        static_cast<double>(point.x);
    const auto offset_y =
        static_cast<double>(candidate.position.y) -
        static_cast<double>(point.y);
    const auto offset_z =
        static_cast<double>(candidate.position.z) -
        static_cast<double>(point.z);
    const auto center_plane_distance =
        offset_x * static_cast<double>(normal.x) +
        offset_y * static_cast<double>(normal.y) +
        offset_z * static_cast<double>(normal.z);
    const auto plane_separation =
        center_plane_distance -
        static_cast<double>(collider.radius);
    if (!std::isfinite(plane_separation)) {
        return core::Result<SphereTerrainStep>::failure(
            contact_error(
                core::ErrorCode::unavailable,
                "Sphere terrain separation exceeded finite range"));
    }
    if (plane_separation > contact_tolerance_meters) {
        state = candidate;
        return core::Result<SphereTerrainStep>::success({});
    }

    // The sampled point shares the predicted center's X/Z coordinates. Moving
    // only Y places the center exactly one radius from that face plane without
    // changing which canonical triangle owns the query.
    const auto supported_center_y =
        static_cast<double>(point.y) +
        static_cast<double>(collider.radius) /
            static_cast<double>(normal.y);
    const auto penetration =
        std::max(0.0, -plane_separation);
    if (!representable_float(supported_center_y) ||
        !representable_float(plane_separation) ||
        !representable_float(penetration)) {
        return core::Result<SphereTerrainStep>::failure(
            contact_error(
                core::ErrorCode::unavailable,
                "Sphere terrain correction exceeded finite float "
                "range"));
    }

    candidate.position.y =
        static_cast<float>(supported_center_y);

    const std::array<double, 3> contact_position{
        static_cast<double>(candidate.position.x) -
            static_cast<double>(normal.x) *
                static_cast<double>(collider.radius),
        static_cast<double>(candidate.position.y) -
            static_cast<double>(normal.y) *
                static_cast<double>(collider.radius),
        static_cast<double>(candidate.position.z) -
            static_cast<double>(normal.z) *
                static_cast<double>(collider.radius),
    };
    for (const auto component : contact_position) {
        if (!representable_float(component)) {
            return core::Result<SphereTerrainStep>::failure(
                contact_error(
                    core::ErrorCode::unavailable,
                    "Sphere terrain witness exceeded finite float "
                    "range"));
        }
    }

    std::array<RigidBodyState, 1> solver_states{{candidate}};
    const std::array<ContactBodyMassProperties, 1> solver_masses{{
        contact_mass_properties(mass_properties),
    }};
    const std::array<ContactConstraint, 1> constraints{{
        ContactConstraint{
            .first_body_index = static_contact_body_index,
            .second_body_index = 0,
            .normal = normal,
            .material = {
                .restitution = settings.restitution,
                .static_friction = settings.static_friction,
                .dynamic_friction = settings.dynamic_friction,
            },
            .points = {{
                ContactConstraintPoint{
                    .position = {
                        static_cast<float>(contact_position[0]),
                        static_cast<float>(contact_position[1]),
                        static_cast<float>(contact_position[2]),
                    },
                    .penetration_depth = 0.0F,
                },
            }},
            .point_count = 1,
        },
    }};
    auto solver_result = solve_contact_constraints(
        solver_states,
        solver_masses,
        constraints,
        settings.solver);
    if (!solver_result) {
        return core::Result<SphereTerrainStep>::failure(
            std::move(solver_result).error());
    }

    candidate = solver_states[0];
    state = candidate;
    return core::Result<SphereTerrainStep>::success(
        SphereTerrainStep{
            .contact = SphereTerrainContact{
                .surface = *surface,
                .plane_separation_before_resolution =
                    static_cast<float>(plane_separation),
                .penetration_depth =
                    static_cast<float>(penetration),
            },
        });
}

} // namespace shark::physics
