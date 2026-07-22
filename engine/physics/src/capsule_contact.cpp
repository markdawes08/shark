#include <shark/physics/capsule_contact.hpp>

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
inline constexpr double coincident_distance_squared = 1.0e-20;
inline constexpr double parallel_relative_tolerance = 1.0e-12;

using PreciseVector = std::array<double, 3>;

struct SegmentPair final {
    PreciseVector first_point;
    PreciseVector second_point;
};

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

[[nodiscard]] constexpr PreciseVector precise(
    const math::Float3 value) noexcept
{
    return {
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z),
    };
}

[[nodiscard]] constexpr PreciseVector subtract(
    const PreciseVector first,
    const PreciseVector second) noexcept
{
    return {
        first[0] - second[0],
        first[1] - second[1],
        first[2] - second[2],
    };
}

[[nodiscard]] constexpr PreciseVector add(
    const PreciseVector first,
    const PreciseVector second) noexcept
{
    return {
        first[0] + second[0],
        first[1] + second[1],
        first[2] + second[2],
    };
}

[[nodiscard]] constexpr PreciseVector scale(
    const PreciseVector value,
    const double scalar) noexcept
{
    return {
        value[0] * scalar,
        value[1] * scalar,
        value[2] * scalar,
    };
}

[[nodiscard]] constexpr double dot(
    const PreciseVector first,
    const PreciseVector second) noexcept
{
    return first[0] * second[0] +
        first[1] * second[1] +
        first[2] * second[2];
}

[[nodiscard]] constexpr PreciseVector cross(
    const PreciseVector first,
    const PreciseVector second) noexcept
{
    return {
        first[1] * second[2] - first[2] * second[1],
        first[2] * second[0] - first[0] * second[2],
        first[0] * second[1] - first[1] * second[0],
    };
}

[[nodiscard]] constexpr double length_squared(
    const PreciseVector value) noexcept
{
    return dot(value, value);
}

[[nodiscard]] bool representable_float(
    const double value) noexcept
{
    return std::isfinite(value) &&
        std::abs(value) <=
            static_cast<double>(
                std::numeric_limits<float>::max());
}

[[nodiscard]] bool representable(
    const PreciseVector value) noexcept
{
    return representable_float(value[0]) &&
        representable_float(value[1]) &&
        representable_float(value[2]);
}

[[nodiscard]] math::Float3 float3(
    const PreciseVector value) noexcept
{
    return {
        static_cast<float>(value[0]),
        static_cast<float>(value[1]),
        static_cast<float>(value[2]),
    };
}

[[nodiscard]] PreciseVector normalize(
    const PreciseVector value) noexcept
{
    return scale(value, 1.0 / std::sqrt(length_squared(value)));
}

[[nodiscard]] PreciseVector deterministic_perpendicular(
    const PreciseVector axis) noexcept
{
    const auto axis_length_squared = length_squared(axis);
    if (axis_length_squared <= coincident_distance_squared) {
        return {1.0, 0.0, 0.0};
    }

    const auto unit_axis = normalize(axis);
    const auto absolute_x = std::abs(unit_axis[0]);
    const auto absolute_y = std::abs(unit_axis[1]);
    const auto absolute_z = std::abs(unit_axis[2]);
    const auto basis =
        absolute_x <= absolute_y && absolute_x <= absolute_z
        ? PreciseVector{1.0, 0.0, 0.0}
        : absolute_y <= absolute_z
            ? PreciseVector{0.0, 1.0, 0.0}
            : PreciseVector{0.0, 0.0, 1.0};
    return normalize(cross(unit_axis, basis));
}

[[nodiscard]] PreciseVector capsule_sphere_fallback_normal(
    const CapsuleWorldSegment& capsule) noexcept
{
    return deterministic_perpendicular(subtract(
        precise(capsule.second_endpoint),
        precise(capsule.first_endpoint)));
}

[[nodiscard]] PreciseVector capsule_capsule_fallback_normal(
    const CapsuleWorldSegment& first,
    const CapsuleWorldSegment& second) noexcept
{
    const auto first_axis = subtract(
        precise(first.second_endpoint),
        precise(first.first_endpoint));
    const auto second_axis = subtract(
        precise(second.second_endpoint),
        precise(second.first_endpoint));
    const auto first_length_squared = length_squared(first_axis);
    const auto second_length_squared = length_squared(second_axis);
    const auto crossed = cross(first_axis, second_axis);
    const auto crossed_length_squared = length_squared(crossed);
    if (first_length_squared > coincident_distance_squared &&
        second_length_squared > coincident_distance_squared &&
        crossed_length_squared >
            parallel_relative_tolerance *
                first_length_squared * second_length_squared) {
        return normalize(crossed);
    }
    if (first_length_squared > coincident_distance_squared) {
        return deterministic_perpendicular(first_axis);
    }
    if (second_length_squared > coincident_distance_squared) {
        return deterministic_perpendicular(second_axis);
    }
    return {1.0, 0.0, 0.0};
}

[[nodiscard]] PreciseVector contact_normal(
    const PreciseVector first_point,
    const PreciseVector second_point,
    const PreciseVector fallback) noexcept
{
    const auto offset = subtract(second_point, first_point);
    if (length_squared(offset) <= coincident_distance_squared) {
        return fallback;
    }
    return normalize(offset);
}

[[nodiscard]] PreciseVector closest_point_on_segment(
    const CapsuleWorldSegment& segment,
    const PreciseVector point) noexcept
{
    const auto first = precise(segment.first_endpoint);
    const auto direction = subtract(
        precise(segment.second_endpoint),
        first);
    const auto denominator = length_squared(direction);
    if (denominator <= coincident_distance_squared) {
        return first;
    }
    const auto parameter = std::clamp(
        dot(subtract(point, first), direction) / denominator,
        0.0,
        1.0);
    return add(first, scale(direction, parameter));
}

[[nodiscard]] SegmentPair closest_segment_points(
    const CapsuleWorldSegment& first_segment,
    const CapsuleWorldSegment& second_segment) noexcept
{
    const auto first_start = precise(first_segment.first_endpoint);
    const auto second_start = precise(second_segment.first_endpoint);
    const auto first_direction = subtract(
        precise(first_segment.second_endpoint),
        first_start);
    const auto second_direction = subtract(
        precise(second_segment.second_endpoint),
        second_start);
    const auto start_offset = subtract(first_start, second_start);
    const auto first_length_squared = length_squared(first_direction);
    const auto second_length_squared = length_squared(second_direction);

    double first_parameter = 0.0;
    double second_parameter = 0.0;
    if (first_length_squared <= coincident_distance_squared &&
        second_length_squared <= coincident_distance_squared) {
        return {first_start, second_start};
    }
    if (first_length_squared <= coincident_distance_squared) {
        second_parameter = std::clamp(
            dot(second_direction, start_offset) /
                second_length_squared,
            0.0,
            1.0);
    }
    else {
        const auto first_offset_projection =
            dot(first_direction, start_offset);
        if (second_length_squared <= coincident_distance_squared) {
            first_parameter = std::clamp(
                -first_offset_projection /
                    first_length_squared,
                0.0,
                1.0);
        }
        else {
            const auto directions_projection =
                dot(first_direction, second_direction);
            const auto second_offset_projection =
                dot(second_direction, start_offset);
            const auto denominator =
                first_length_squared * second_length_squared -
                directions_projection * directions_projection;
            if (denominator >
                parallel_relative_tolerance *
                    first_length_squared * second_length_squared) {
                first_parameter = std::clamp(
                    (directions_projection * second_offset_projection -
                     first_offset_projection * second_length_squared) /
                        denominator,
                    0.0,
                    1.0);
            }
            second_parameter =
                (directions_projection * first_parameter +
                 second_offset_projection) /
                second_length_squared;
            if (second_parameter < 0.0) {
                second_parameter = 0.0;
                first_parameter = std::clamp(
                    -first_offset_projection /
                        first_length_squared,
                    0.0,
                    1.0);
            }
            else if (second_parameter > 1.0) {
                second_parameter = 1.0;
                first_parameter = std::clamp(
                    (directions_projection -
                     first_offset_projection) /
                        first_length_squared,
                    0.0,
                    1.0);
            }
        }
    }

    return {
        add(first_start, scale(first_direction, first_parameter)),
        add(second_start, scale(second_direction, second_parameter)),
    };
}

[[nodiscard]] bool valid_contact_values(
    const PreciseVector first_point,
    const PreciseVector second_point,
    const PreciseVector normal,
    const double separation,
    const double penetration_depth) noexcept
{
    return representable(first_point) &&
        representable(second_point) &&
        representable(normal) &&
        representable_float(separation) &&
        representable_float(penetration_depth);
}

} // namespace

core::Result<CapsuleWorldSegment> make_capsule_world_segment(
    const RigidBodyState& state,
    const CapsuleCollider& collider)
{
    if (!is_valid(state) || !is_valid(collider)) {
        return core::Result<CapsuleWorldSegment>::failure(
            contact_error(
                core::ErrorCode::invalid_argument,
                "Capsule world segments require a valid rigid state "
                "and finite positive-radius collider"));
    }

    const auto rotated_half_segment = math::rotate(
        state.orientation,
        collider.local_half_segment);
    if (!math::is_finite(rotated_half_segment)) {
        return core::Result<CapsuleWorldSegment>::failure(
            contact_error(
                core::ErrorCode::unavailable,
                "Capsule orientation exceeded finite float range"));
    }
    const auto position = precise(state.position);
    const auto half_segment = precise(rotated_half_segment);
    const auto first = subtract(position, half_segment);
    const auto second = add(position, half_segment);
    if (!representable(first) || !representable(second)) {
        return core::Result<CapsuleWorldSegment>::failure(
            contact_error(
                core::ErrorCode::unavailable,
                "Capsule world endpoints exceeded finite float range"));
    }

    return core::Result<CapsuleWorldSegment>::success({
        .first_endpoint = float3(first),
        .second_endpoint = float3(second),
    });
}

core::Result<std::optional<CapsuleTerrainContact>>
query_capsule_terrain_contact(
    const RigidBodyState& capsule_state,
    const CapsuleCollider& capsule,
    const terrain::HeightTileSurface& terrain_surface)
{
    auto segment_result = make_capsule_world_segment(
        capsule_state,
        capsule);
    if (!segment_result) {
        return core::Result<
            std::optional<CapsuleTerrainContact>>::failure(
                std::move(segment_result).error());
    }
    const auto& segment = segment_result.value();
    const auto maximum_search_distance = static_cast<float>(
        std::min(
            static_cast<double>(
                std::numeric_limits<float>::max()),
            static_cast<double>(capsule.radius) +
                contact_tolerance_meters));
    auto closest_result =
        terrain_surface.closest_lod0_point_to_segment(
            terrain::Segment3{
                .first_endpoint = segment.first_endpoint,
                .second_endpoint = segment.second_endpoint,
            },
            maximum_search_distance);
    if (!closest_result) {
        return core::Result<
            std::optional<CapsuleTerrainContact>>::failure(
                std::move(closest_result).error());
    }
    if (!closest_result.value().has_value()) {
        return core::Result<
            std::optional<CapsuleTerrainContact>>::success(
                std::nullopt);
    }

    const auto& closest = *closest_result.value();
    const auto separation =
        static_cast<double>(closest.distance) -
        static_cast<double>(capsule.radius);
    if (separation > contact_tolerance_meters) {
        return core::Result<
            std::optional<CapsuleTerrainContact>>::success(
                std::nullopt);
    }
    const auto penetration_depth = std::max(0.0, -separation);
    const auto terrain_point = precise(closest.surface.position);
    const auto capsule_point = precise(closest.segment_position);
    const auto surface_normal = precise(closest.surface.normal);
    auto normal = contact_normal(
        terrain_point,
        capsule_point,
        surface_normal);
    if (dot(normal, surface_normal) < 0.0) {
        normal = scale(normal, -1.0);
    }
    if (!valid_contact_values(
            terrain_point,
            capsule_point,
            normal,
            separation,
            penetration_depth)) {
        return core::Result<
            std::optional<CapsuleTerrainContact>>::failure(
                contact_error(
                    core::ErrorCode::unavailable,
                    "Capsule terrain contact exceeded finite float range"));
    }

    return core::Result<
        std::optional<CapsuleTerrainContact>>::success(
            CapsuleTerrainContact{
                .surface = closest.surface,
                .capsule_axis_point = float3(capsule_point),
                .normal = float3(normal),
                .separation = static_cast<float>(separation),
                .penetration_depth =
                    static_cast<float>(penetration_depth),
            });
}

core::Result<std::optional<CapsuleSphereContact>>
query_capsule_sphere_contact(
    const RigidBodyState& capsule_state,
    const CapsuleCollider& capsule,
    const RigidBodyState& sphere_state,
    const SphereCollider& sphere)
{
    if (!is_valid(sphere_state) || !is_valid(sphere)) {
        return core::Result<
            std::optional<CapsuleSphereContact>>::failure(
                contact_error(
                    core::ErrorCode::invalid_argument,
                    "Capsule sphere queries require a valid sphere "
                    "state and finite positive radius"));
    }
    auto segment_result = make_capsule_world_segment(
        capsule_state,
        capsule);
    if (!segment_result) {
        return core::Result<
            std::optional<CapsuleSphereContact>>::failure(
                std::move(segment_result).error());
    }
    const auto& segment = segment_result.value();
    const auto sphere_center = precise(sphere_state.position);
    const auto capsule_point = closest_point_on_segment(
        segment,
        sphere_center);
    const auto offset = subtract(sphere_center, capsule_point);
    const auto distance_squared = length_squared(offset);
    const auto radius_sum =
        static_cast<double>(capsule.radius) + sphere.radius;
    if (!std::isfinite(distance_squared) ||
        !std::isfinite(radius_sum)) {
        return core::Result<
            std::optional<CapsuleSphereContact>>::failure(
                contact_error(
                    core::ErrorCode::unavailable,
                    "Capsule sphere closest-feature math exceeded "
                    "finite range"));
    }
    const auto distance = std::sqrt(distance_squared);
    const auto separation = distance - radius_sum;
    if (separation > contact_tolerance_meters) {
        return core::Result<
            std::optional<CapsuleSphereContact>>::success(
                std::nullopt);
    }
    const auto penetration_depth = std::max(0.0, -separation);
    const auto normal = contact_normal(
        capsule_point,
        sphere_center,
        capsule_sphere_fallback_normal(segment));
    if (!valid_contact_values(
            capsule_point,
            sphere_center,
            normal,
            separation,
            penetration_depth)) {
        return core::Result<
            std::optional<CapsuleSphereContact>>::failure(
                contact_error(
                    core::ErrorCode::unavailable,
                    "Capsule sphere contact exceeded finite float range"));
    }

    return core::Result<
        std::optional<CapsuleSphereContact>>::success(
            CapsuleSphereContact{
                .capsule_axis_point = float3(capsule_point),
                .sphere_center = sphere_state.position,
                .normal = float3(normal),
                .separation = static_cast<float>(separation),
                .penetration_depth =
                    static_cast<float>(penetration_depth),
            });
}

core::Result<std::optional<CapsuleCapsuleContact>>
query_capsule_capsule_contact(
    const RigidBodyState& first_state,
    const CapsuleCollider& first,
    const RigidBodyState& second_state,
    const CapsuleCollider& second)
{
    auto first_segment_result = make_capsule_world_segment(
        first_state,
        first);
    if (!first_segment_result) {
        return core::Result<
            std::optional<CapsuleCapsuleContact>>::failure(
                std::move(first_segment_result).error());
    }
    auto second_segment_result = make_capsule_world_segment(
        second_state,
        second);
    if (!second_segment_result) {
        return core::Result<
            std::optional<CapsuleCapsuleContact>>::failure(
                std::move(second_segment_result).error());
    }

    const auto& first_segment = first_segment_result.value();
    const auto& second_segment = second_segment_result.value();
    const auto closest = closest_segment_points(
        first_segment,
        second_segment);
    const auto offset = subtract(
        closest.second_point,
        closest.first_point);
    const auto distance_squared = length_squared(offset);
    const auto radius_sum =
        static_cast<double>(first.radius) + second.radius;
    if (!std::isfinite(distance_squared) ||
        !std::isfinite(radius_sum)) {
        return core::Result<
            std::optional<CapsuleCapsuleContact>>::failure(
                contact_error(
                    core::ErrorCode::unavailable,
                    "Capsule pair closest-feature math exceeded "
                    "finite range"));
    }
    const auto distance = std::sqrt(distance_squared);
    const auto separation = distance - radius_sum;
    if (separation > contact_tolerance_meters) {
        return core::Result<
            std::optional<CapsuleCapsuleContact>>::success(
                std::nullopt);
    }
    const auto penetration_depth = std::max(0.0, -separation);
    const auto normal = contact_normal(
        closest.first_point,
        closest.second_point,
        capsule_capsule_fallback_normal(
            first_segment,
            second_segment));
    if (!valid_contact_values(
            closest.first_point,
            closest.second_point,
            normal,
            separation,
            penetration_depth)) {
        return core::Result<
            std::optional<CapsuleCapsuleContact>>::failure(
                contact_error(
                    core::ErrorCode::unavailable,
                    "Capsule pair contact exceeded finite float range"));
    }

    return core::Result<
        std::optional<CapsuleCapsuleContact>>::success(
            CapsuleCapsuleContact{
                .first_axis_point = float3(closest.first_point),
                .second_axis_point = float3(closest.second_point),
                .normal = float3(normal),
                .separation = static_cast<float>(separation),
                .penetration_depth =
                    static_cast<float>(penetration_depth),
            });
}

} // namespace shark::physics
