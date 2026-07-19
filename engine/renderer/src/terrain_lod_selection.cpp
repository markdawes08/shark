#include "terrain_lod_selection.hpp"

#include <cmath>

namespace shark::renderer::detail {
namespace {

[[nodiscard]] constexpr double axis_distance(
    const double coordinate,
    const double minimum,
    const double maximum) noexcept
{
    if (coordinate < minimum) {
        return minimum - coordinate;
    }
    if (coordinate > maximum) {
        return coordinate - maximum;
    }
    return 0.0;
}

} // namespace

std::optional<TerrainLodDecision> select_terrain_lod(
    const math::Float3 camera_position,
    const math::Float3 bounds_minimum,
    const math::Float3 bounds_maximum,
    const double maximum_geometric_error) noexcept
{
    if (!math::is_finite(camera_position) ||
        !math::is_finite(bounds_minimum) ||
        !math::is_finite(bounds_maximum) ||
        bounds_minimum.x > bounds_maximum.x ||
        bounds_minimum.y > bounds_maximum.y ||
        bounds_minimum.z > bounds_maximum.z ||
        !std::isfinite(maximum_geometric_error) ||
        maximum_geometric_error < 0.0) {
        return std::nullopt;
    }

    const auto distance_x = axis_distance(
        static_cast<double>(camera_position.x),
        static_cast<double>(bounds_minimum.x),
        static_cast<double>(bounds_maximum.x));
    const auto distance_y = axis_distance(
        static_cast<double>(camera_position.y),
        static_cast<double>(bounds_minimum.y),
        static_cast<double>(bounds_maximum.y));
    const auto distance_z = axis_distance(
        static_cast<double>(camera_position.z),
        static_cast<double>(bounds_minimum.z),
        static_cast<double>(bounds_maximum.z));
    const auto camera_distance = std::sqrt(
        distance_x * distance_x +
        distance_y * distance_y +
        distance_z * distance_z);
    if (!std::isfinite(camera_distance)) {
        return std::nullopt;
    }

    const auto lod =
        maximum_geometric_error <=
            terrain_coarse_relative_error_limit * camera_distance
        ? TerrainLod::coarse
        : TerrainLod::lod0;
    return TerrainLodDecision{lod, camera_distance};
}

} // namespace shark::renderer::detail
