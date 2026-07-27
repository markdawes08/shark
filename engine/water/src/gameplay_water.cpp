#include <shark/water/gameplay_water.hpp>

#include <shark/core/error.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::water {
namespace {

[[nodiscard]] core::Error water_query_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] bool valid_footprint(
    const terrain::IslandFootprint& footprint) noexcept
{
    return std::isfinite(footprint.center_x) &&
        std::isfinite(footprint.center_z) &&
        std::isfinite(footprint.semi_axis_x) &&
        std::isfinite(footprint.semi_axis_z) &&
        std::isfinite(footprint.x_warp_square_offset) &&
        std::isfinite(footprint.x_warp_divisor) &&
        std::isfinite(footprint.z_warp_square_offset) &&
        std::isfinite(footprint.z_warp_divisor) &&
        footprint.semi_axis_x > 0.0F &&
        footprint.semi_axis_z > 0.0F &&
        footprint.x_warp_divisor > 0.0F &&
        footprint.z_warp_divisor > 0.0F;
}

[[nodiscard]] bool valid_support_side(
    const CalmWaterSupportSide support_side) noexcept
{
    return support_side ==
            CalmWaterSupportSide::inside_warped_footprint ||
        support_side ==
            CalmWaterSupportSide::outside_warped_footprint;
}

[[nodiscard]] bool valid_body(
    const CalmWaterBody& body) noexcept
{
    const auto flow_is_valid =
        !body.flow_velocity.has_value() ||
        (std::isfinite(body.flow_velocity->x) &&
         std::isfinite(body.flow_velocity->z));
    return valid_footprint(body.footprint) &&
        valid_support_side(body.support_side) &&
        std::isfinite(body.surface_height) &&
        std::isfinite(body.shoreline_depth_tolerance) &&
        body.shoreline_depth_tolerance > 0.0F &&
        flow_is_valid;
}

[[nodiscard]] float canonical_zero(
    const float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] bool positive_zero(
    const float value) noexcept
{
    return value == 0.0F && !std::signbit(value);
}

[[nodiscard]] bool canonical_finite(
    const float value) noexcept
{
    return std::isfinite(value) &&
        (value != 0.0F || !std::signbit(value));
}

[[nodiscard]] HorizontalFlow canonical_flow(
    const HorizontalFlow flow) noexcept
{
    return {
        .x = canonical_zero(flow.x),
        .z = canonical_zero(flow.z),
    };
}

[[nodiscard]] bool has_horizontal_support(
    const CalmWaterSupportSide support_side,
    const double normalized_radius_squared) noexcept
{
    switch (support_side) {
    case CalmWaterSupportSide::inside_warped_footprint:
        return normalized_radius_squared <= 1.0;
    case CalmWaterSupportSide::outside_warped_footprint:
        return normalized_radius_squared >= 1.0;
    }
    return false;
}

} // namespace

bool is_valid(const GameplayWaterQuery& query) noexcept
{
    if (!canonical_finite(query.surface_height) ||
        !canonical_finite(query.bed_height) ||
        !canonical_finite(query.depth)) {
        return false;
    }

    const auto flow_is_valid =
        !query.flow_velocity.has_value() ||
        (canonical_finite(query.flow_velocity->x) &&
         canonical_finite(query.flow_velocity->z));
    if (!flow_is_valid) {
        return false;
    }

    switch (query.disposition) {
    case GameplayWaterDisposition::out_of_terrain:
        return !query.horizontal_support &&
            positive_zero(query.bed_height) &&
            positive_zero(query.depth) &&
            !query.flow_velocity.has_value();
    case GameplayWaterDisposition::no_water:
        return positive_zero(query.depth) &&
            !query.flow_velocity.has_value();
    case GameplayWaterDisposition::water: {
        const auto checked_depth =
            static_cast<double>(query.surface_height) -
            static_cast<double>(query.bed_height);
        return query.horizontal_support &&
            query.depth > 0.0F &&
            std::isfinite(checked_depth) &&
            checked_depth > 0.0 &&
            checked_depth <=
                static_cast<double>(
                    std::numeric_limits<float>::max()) &&
            query.depth ==
                static_cast<float>(checked_depth);
    }
    default:
        return false;
    }
}

core::Result<GameplayWaterQuery> query_gameplay_water(
    const CalmWaterBody& body,
    const terrain::HeightTileSurface& terrain_surface,
    const float world_x,
    const float world_z)
{
    if (!valid_body(body)) {
        return core::Result<GameplayWaterQuery>::failure(
            water_query_error(
                core::ErrorCode::invalid_argument,
                "Gameplay water query requires a finite calm-water body "
                "with positive axes, divisors, and shoreline tolerance"));
    }
    if (!std::isfinite(world_x) || !std::isfinite(world_z)) {
        return core::Result<GameplayWaterQuery>::failure(
            water_query_error(
                core::ErrorCode::invalid_argument,
                "Gameplay water query coordinates must be finite"));
    }

    const auto surface_height =
        canonical_zero(body.surface_height);

    const auto terrain_sample =
        terrain_surface.sample_lod0_surface(world_x, world_z);
    if (!terrain_sample.has_value()) {
        return core::Result<GameplayWaterQuery>::success(
            GameplayWaterQuery{
                .disposition =
                    GameplayWaterDisposition::out_of_terrain,
                .horizontal_support = false,
                .surface_height = surface_height,
                .bed_height = 0.0F,
                .depth = 0.0F,
                .flow_velocity = std::nullopt,
            });
    }

    const auto radius_squared =
        terrain::island_normalized_radius_squared(
            body.footprint,
            world_x,
            world_z);
    if (!std::isfinite(radius_squared)) {
        return core::Result<GameplayWaterQuery>::failure(
            water_query_error(
                core::ErrorCode::unavailable,
                "Gameplay water support exceeded finite range"));
    }
    const auto horizontal_support = has_horizontal_support(
        body.support_side,
        radius_squared);
    const auto bed_height =
        canonical_zero(terrain_sample->position.y);
    const auto depth =
        static_cast<double>(body.surface_height) -
        static_cast<double>(bed_height);
    if (!std::isfinite(depth)) {
        return core::Result<GameplayWaterQuery>::failure(
            water_query_error(
                core::ErrorCode::unavailable,
                "Gameplay water depth exceeded finite range"));
    }

    if (!horizontal_support ||
        depth <= static_cast<double>(
                     body.shoreline_depth_tolerance)) {
        return core::Result<GameplayWaterQuery>::success(
            GameplayWaterQuery{
                .disposition =
                    GameplayWaterDisposition::no_water,
                .horizontal_support = horizontal_support,
                .surface_height = surface_height,
                .bed_height = bed_height,
                .depth = 0.0F,
                .flow_velocity = std::nullopt,
            });
    }

    if (depth >
        static_cast<double>(std::numeric_limits<float>::max())) {
        return core::Result<GameplayWaterQuery>::failure(
            water_query_error(
                core::ErrorCode::unavailable,
                "Gameplay water depth cannot be represented as float"));
    }

    std::optional<HorizontalFlow> flow;
    if (body.flow_velocity.has_value()) {
        flow = canonical_flow(*body.flow_velocity);
    }
    return core::Result<GameplayWaterQuery>::success(
        GameplayWaterQuery{
            .disposition = GameplayWaterDisposition::water,
            .horizontal_support = true,
            .surface_height = surface_height,
            .bed_height = bed_height,
            .depth = static_cast<float>(depth),
            .flow_velocity = flow,
        });
}

} // namespace shark::water
