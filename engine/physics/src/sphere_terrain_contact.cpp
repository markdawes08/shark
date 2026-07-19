#include <shark/physics/sphere_terrain_contact.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
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

} // namespace

core::Result<SphereTerrainStep>
advance_sphere_against_terrain(
    BallisticBodyState& state,
    const SphereCollider& collider,
    const terrain::HeightTileSurface& terrain_surface,
    const math::Float3 acceleration,
    const float fixed_delta_seconds)
{
    if (!is_valid(collider)) {
        return core::Result<SphereTerrainStep>::failure(
            contact_error(
                core::ErrorCode::invalid_argument,
                "Sphere terrain contact requires a finite positive "
                "radius"));
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
    candidate.linear_velocity = {};
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
