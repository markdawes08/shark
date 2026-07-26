#include <shark/terrain/island.hpp>

#include <shark/core/error.hpp>
#include <shark/core/math.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace shark::terrain {
namespace {

inline constexpr double q8_scale = 256.0;
inline constexpr double maximum_world_magnitude = 4'096.0;
inline constexpr double maximum_profile_height = 256.0;

[[nodiscard]] core::Error island_error(std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        core::ErrorCode::invalid_argument,
        std::move(message),
    };
}

[[nodiscard]] bool valid_tile(const HeightTile& tile) noexcept
{
    if (tile.sample_columns < 2U ||
        tile.sample_rows < 2U ||
        !std::isfinite(tile.sample_spacing) ||
        tile.sample_spacing <= 0.0F ||
        !math::is_finite(tile.origin)) {
        return false;
    }
    const auto expected_size =
        static_cast<std::size_t>(tile.sample_columns) *
        static_cast<std::size_t>(tile.sample_rows);
    return tile.height_offsets.size() == expected_size &&
        std::all_of(
            tile.height_offsets.begin(),
            tile.height_offsets.end(),
            [](const float height) {
                return std::isfinite(height);
            });
}

[[nodiscard]] bool exactly_representable_q8(
    const double value) noexcept
{
    const auto scaled = value * q8_scale;
    return std::isfinite(scaled) &&
        scaled == std::trunc(scaled);
}

[[nodiscard]] bool valid_shape(const IslandShape& shape) noexcept
{
    const auto& footprint = shape.footprint;
    const auto finite =
        std::isfinite(footprint.center_x) &&
        std::isfinite(footprint.center_z) &&
        std::isfinite(footprint.semi_axis_x) &&
        std::isfinite(footprint.semi_axis_z) &&
        std::isfinite(footprint.x_warp_square_offset) &&
        std::isfinite(footprint.x_warp_divisor) &&
        std::isfinite(footprint.z_warp_square_offset) &&
        std::isfinite(footprint.z_warp_divisor) &&
        std::isfinite(shape.waterline_y) &&
        std::isfinite(shape.shoreline_land_clearance) &&
        std::isfinite(shape.interior_land_height) &&
        std::isfinite(shape.natural_relief_scale) &&
        std::isfinite(shape.shoreline_water_depth) &&
        std::isfinite(shape.deep_water_depth) &&
        std::isfinite(shape.deep_water_end_radius_squared);
    if (!finite ||
        footprint.semi_axis_x <= 0.0F ||
        footprint.semi_axis_z <= 0.0F ||
        footprint.x_warp_divisor <= 0.0F ||
        footprint.z_warp_divisor <= 0.0F ||
        shape.shoreline_land_clearance <= 0.0F ||
        shape.interior_land_height <=
            shape.shoreline_land_clearance ||
        shape.natural_relief_scale < 0.0F ||
        shape.natural_relief_scale > 1.0F ||
        shape.shoreline_water_depth <= 0.0F ||
        shape.deep_water_depth <=
            shape.shoreline_water_depth ||
        shape.deep_water_end_radius_squared <= 1.0) {
        return false;
    }

    const auto bounded =
        std::abs(static_cast<double>(footprint.center_x)) <=
            maximum_world_magnitude &&
        std::abs(static_cast<double>(footprint.center_z)) <=
            maximum_world_magnitude &&
        footprint.semi_axis_x <= maximum_world_magnitude &&
        footprint.semi_axis_z <= maximum_world_magnitude &&
        shape.interior_land_height <= maximum_profile_height &&
        shape.deep_water_depth <= maximum_profile_height;
    return bounded &&
        exactly_representable_q8(footprint.center_x) &&
        exactly_representable_q8(footprint.center_z) &&
        exactly_representable_q8(footprint.semi_axis_x) &&
        exactly_representable_q8(footprint.semi_axis_z) &&
        exactly_representable_q8(
            footprint.x_warp_square_offset) &&
        exactly_representable_q8(footprint.x_warp_divisor) &&
        exactly_representable_q8(
            footprint.z_warp_square_offset) &&
        exactly_representable_q8(footprint.z_warp_divisor) &&
        exactly_representable_q8(shape.waterline_y) &&
        exactly_representable_q8(
            shape.shoreline_land_clearance) &&
        exactly_representable_q8(shape.interior_land_height) &&
        exactly_representable_q8(shape.natural_relief_scale) &&
        exactly_representable_q8(
            shape.shoreline_water_depth) &&
        exactly_representable_q8(shape.deep_water_depth);
}

[[nodiscard]] double smooth_unit(const double value) noexcept
{
    const auto bounded = std::clamp(value, 0.0, 1.0);
    return bounded * bounded * (3.0 - 2.0 * bounded);
}

[[nodiscard]] core::Result<float> quantized_height_offset(
    const double world_height,
    const float origin_y)
{
    const auto scaled = world_height * q8_scale;
    if (!std::isfinite(scaled) ||
        scaled < static_cast<double>(
                     std::numeric_limits<std::int64_t>::min()) ||
        scaled > static_cast<double>(
                     std::numeric_limits<std::int64_t>::max())) {
        return core::Result<float>::failure(
            island_error(
                "Island profile produced an unrepresentable height"));
    }
    const auto quantized_world_height =
        static_cast<double>(std::llround(scaled)) / q8_scale;
    const auto offset =
        quantized_world_height - static_cast<double>(origin_y);
    if (!std::isfinite(offset) ||
        offset < -static_cast<double>(
                     std::numeric_limits<float>::max()) ||
        offset > static_cast<double>(
                     std::numeric_limits<float>::max())) {
        return core::Result<float>::failure(
            island_error(
                "Island profile produced a non-float height offset"));
    }
    return core::Result<float>::success(
        static_cast<float>(offset));
}

[[nodiscard]] std::size_t sample_index(
    const std::uint32_t x,
    const std::uint32_t z,
    const std::uint32_t columns) noexcept
{
    return static_cast<std::size_t>(z) * columns + x;
}

struct TopologySummary final {
    std::size_t dry_components{};
    std::size_t dry_samples{};
    std::size_t shallow_samples{};
    std::size_t deep_samples{};
    bool dry_perimeter{};
};

[[nodiscard]] TopologySummary summarize_topology(
    const HeightTile& tile,
    const IslandShape& shape)
{
    const auto count = tile.height_offsets.size();
    std::vector<std::uint8_t> dry(count, 0U);
    TopologySummary summary;
    const auto deep_threshold =
        static_cast<double>(shape.deep_water_depth) * 0.75;

    for (std::uint32_t z = 0; z < tile.sample_rows; ++z) {
        for (std::uint32_t x = 0; x < tile.sample_columns; ++x) {
            const auto index = sample_index(
                x,
                z,
                tile.sample_columns);
            const auto world_height =
                static_cast<double>(tile.origin.y) +
                static_cast<double>(tile.height_offsets[index]);
            if (world_height >
                static_cast<double>(shape.waterline_y)) {
                dry[index] = 1U;
                ++summary.dry_samples;
                if (x == 0U ||
                    z == 0U ||
                    x + 1U == tile.sample_columns ||
                    z + 1U == tile.sample_rows) {
                    summary.dry_perimeter = true;
                }
            }
            else {
                const auto depth =
                    static_cast<double>(shape.waterline_y) -
                    world_height;
                if (depth <= 2.0) {
                    ++summary.shallow_samples;
                }
                if (depth >= deep_threshold) {
                    ++summary.deep_samples;
                }
            }
        }
    }

    std::vector<std::uint8_t> visited(count, 0U);
    constexpr std::array<std::array<int, 2>, 4> neighbors{{
        {-1, 0},
        {1, 0},
        {0, -1},
        {0, 1},
    }};
    for (std::uint32_t start_z = 0;
         start_z < tile.sample_rows;
         ++start_z) {
        for (std::uint32_t start_x = 0;
             start_x < tile.sample_columns;
             ++start_x) {
            const auto start_index = sample_index(
                start_x,
                start_z,
                tile.sample_columns);
            if (dry[start_index] == 0U ||
                visited[start_index] != 0U) {
                continue;
            }

            ++summary.dry_components;
            std::deque<std::array<std::uint32_t, 2>> pending;
            pending.push_back({start_x, start_z});
            visited[start_index] = 1U;
            while (!pending.empty()) {
                const auto point = pending.front();
                pending.pop_front();
                for (const auto direction : neighbors) {
                    const auto next_x =
                        static_cast<std::int64_t>(point[0]) +
                        direction[0];
                    const auto next_z =
                        static_cast<std::int64_t>(point[1]) +
                        direction[1];
                    if (next_x < 0 ||
                        next_z < 0 ||
                        next_x >= tile.sample_columns ||
                        next_z >= tile.sample_rows) {
                        continue;
                    }
                    const auto x =
                        static_cast<std::uint32_t>(next_x);
                    const auto z =
                        static_cast<std::uint32_t>(next_z);
                    const auto index = sample_index(
                        x,
                        z,
                        tile.sample_columns);
                    if (dry[index] == 0U ||
                        visited[index] != 0U) {
                        continue;
                    }
                    visited[index] = 1U;
                    pending.push_back({x, z});
                }
            }
        }
    }
    return summary;
}

} // namespace

double island_normalized_radius_squared(
    const IslandFootprint& footprint,
    const float x,
    const float z) noexcept
{
    if (!std::isfinite(x) ||
        !std::isfinite(z) ||
        !std::isfinite(footprint.center_x) ||
        !std::isfinite(footprint.center_z) ||
        !std::isfinite(footprint.semi_axis_x) ||
        !std::isfinite(footprint.semi_axis_z) ||
        !std::isfinite(footprint.x_warp_square_offset) ||
        !std::isfinite(footprint.x_warp_divisor) ||
        !std::isfinite(footprint.z_warp_square_offset) ||
        !std::isfinite(footprint.z_warp_divisor) ||
        footprint.semi_axis_x <= 0.0F ||
        footprint.semi_axis_z <= 0.0F ||
        footprint.x_warp_divisor <= 0.0F ||
        footprint.z_warp_divisor <= 0.0F) {
        return std::numeric_limits<double>::infinity();
    }

    const auto offset_x =
        static_cast<double>(x) -
        static_cast<double>(footprint.center_x);
    const auto offset_z =
        static_cast<double>(z) -
        static_cast<double>(footprint.center_z);
    const auto warped_x =
        offset_x +
        (offset_z * offset_z -
         static_cast<double>(
             footprint.x_warp_square_offset)) /
            static_cast<double>(footprint.x_warp_divisor);
    const auto warped_z =
        offset_z +
        (offset_x * offset_x -
         static_cast<double>(
             footprint.z_warp_square_offset)) /
            static_cast<double>(footprint.z_warp_divisor);
    const auto normalized_x =
        warped_x /
        static_cast<double>(footprint.semi_axis_x);
    const auto normalized_z =
        warped_z /
        static_cast<double>(footprint.semi_axis_z);
    return normalized_x * normalized_x +
        normalized_z * normalized_z;
}

core::Result<HeightTile> shape_playable_island(
    HeightTile base,
    const IslandShape& shape)
{
    if (!valid_tile(base)) {
        return core::Result<HeightTile>::failure(
            island_error(
                "Island shaping requires a finite rectangular height tile"));
    }
    if (!valid_shape(shape)) {
        return core::Result<HeightTile>::failure(
            island_error(
                "Island shape fields are invalid or not Q8-compatible"));
    }

    for (std::uint32_t z = 0; z < base.sample_rows; ++z) {
        const auto world_z =
            static_cast<double>(base.origin.z) +
            static_cast<double>(z) *
                static_cast<double>(base.sample_spacing);
        for (std::uint32_t x = 0; x < base.sample_columns; ++x) {
            const auto index = sample_index(
                x,
                z,
                base.sample_columns);
            const auto world_x =
                static_cast<double>(base.origin.x) +
                static_cast<double>(x) *
                    static_cast<double>(base.sample_spacing);
            const auto radius_squared =
                island_normalized_radius_squared(
                    shape.footprint,
                    static_cast<float>(world_x),
                    static_cast<float>(world_z));
            if (!std::isfinite(radius_squared)) {
                return core::Result<HeightTile>::failure(
                    island_error(
                        "Island footprint produced a nonfinite radius"));
            }

            const auto base_world_height =
                static_cast<double>(base.origin.y) +
                static_cast<double>(base.height_offsets[index]);
            double world_height{};
            if (radius_squared < 1.0) {
                const auto inland_weight =
                    smooth_unit(1.0 - radius_squared);
                const auto natural_relief = std::clamp(
                    base_world_height *
                        static_cast<double>(
                            shape.natural_relief_scale),
                    -static_cast<double>(
                        shape.interior_land_height) * 0.35,
                    static_cast<double>(
                        shape.interior_land_height) * 0.35);
                const auto interior_height = std::max(
                    static_cast<double>(
                        shape.shoreline_land_clearance),
                    static_cast<double>(
                        shape.interior_land_height) +
                        natural_relief);
                const auto height_above_water =
                    static_cast<double>(
                        shape.shoreline_land_clearance) +
                    inland_weight *
                        (interior_height -
                         static_cast<double>(
                             shape.shoreline_land_clearance));
                world_height =
                    static_cast<double>(shape.waterline_y) +
                    height_above_water;
            }
            else {
                const auto shelf_progress = smooth_unit(
                    (radius_squared - 1.0) /
                    (shape.deep_water_end_radius_squared - 1.0));
                const auto profile_depth =
                    static_cast<double>(
                        shape.shoreline_water_depth) +
                    shelf_progress *
                        (static_cast<double>(
                             shape.deep_water_depth) -
                         static_cast<double>(
                             shape.shoreline_water_depth));
                const auto seabed_relief = std::clamp(
                    base_world_height *
                        static_cast<double>(
                            shape.natural_relief_scale) *
                        0.5,
                    -static_cast<double>(
                        shape.deep_water_depth) * 0.20,
                    static_cast<double>(
                        shape.deep_water_depth) * 0.20);
                world_height =
                    static_cast<double>(shape.waterline_y) -
                    profile_depth +
                    shelf_progress * seabed_relief;
                world_height = std::min(
                    world_height,
                    static_cast<double>(shape.waterline_y) -
                        static_cast<double>(
                            shape.shoreline_water_depth));
            }

            auto offset_result = quantized_height_offset(
                world_height,
                base.origin.y);
            if (!offset_result) {
                return core::Result<HeightTile>::failure(
                    std::move(offset_result).error());
            }
            base.height_offsets[index] =
                offset_result.value();
        }
    }

    const auto topology = summarize_topology(base, shape);
    if (topology.dry_components != 1U ||
        topology.dry_samples == 0U ||
        topology.dry_perimeter ||
        topology.shallow_samples == 0U ||
        topology.deep_samples == 0U) {
        return core::Result<HeightTile>::failure(
            island_error(
                "Island profile did not produce one closed landmass with "
                "both shallow and deep surrounding water"));
    }

    return core::Result<HeightTile>::success(std::move(base));
}

} // namespace shark::terrain
