#include <shark/terrain/lake_basin.hpp>

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

inline constexpr std::int64_t q8_scale = std::int64_t{1} << 8U;
inline constexpr std::int64_t q20_scale = std::int64_t{1} << 20U;
inline constexpr double maximum_fixed_world_magnitude = 4'096.0;
inline constexpr double maximum_fixed_height_offset = 8'192.0;
inline constexpr double maximum_fixed_square_parameter =
    maximum_fixed_world_magnitude * maximum_fixed_world_magnitude;
inline constexpr double maximum_fixed_profile_radius = 64.0;
inline constexpr std::int64_t maximum_warped_component_q8 =
    2'048 * q8_scale;

[[nodiscard]] core::Error basin_error(std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        core::ErrorCode::invalid_argument,
        std::move(message),
    };
}

[[nodiscard]] bool finite_footprint(
    const LakeBasinFootprint& footprint) noexcept
{
    return std::isfinite(footprint.center.x) &&
        std::isfinite(footprint.center.z) &&
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

[[nodiscard]] bool valid_shape(
    const LakeBasinShape& shape) noexcept
{
    return finite_footprint(shape.footprint) &&
        std::isfinite(shape.future_waterline_y) &&
        std::isfinite(shape.core_depth) &&
        std::isfinite(shape.rim_height) &&
        std::isfinite(shape.rise_end_radius) &&
        std::isfinite(shape.rim_end_radius) &&
        std::isfinite(shape.blend_end_radius) &&
        shape.core_depth > 0.0F &&
        shape.rim_height > 0.0F &&
        shape.rise_end_radius > 1.0 &&
        shape.rim_end_radius > shape.rise_end_radius &&
        shape.blend_end_radius > shape.rim_end_radius;
}

[[nodiscard]] bool bounded_magnitude(
    const double value,
    const double maximum) noexcept
{
    return std::isfinite(value) && std::abs(value) <= maximum;
}

[[nodiscard]] bool exactly_representable_q8(
    const double value) noexcept
{
    const auto scaled =
        value * static_cast<double>(q8_scale);
    return std::isfinite(scaled) &&
        scaled == std::trunc(scaled);
}

[[nodiscard]] bool positive_q8(
    const double value,
    const double maximum =
        maximum_fixed_world_magnitude) noexcept
{
    return value > 0.0 && value <= maximum &&
        exactly_representable_q8(value);
}

[[nodiscard]] bool fixed_shape_is_representable(
    const LakeBasinShape& shape) noexcept
{
    const auto& footprint = shape.footprint;
    return bounded_magnitude(
               footprint.center.x,
               maximum_fixed_world_magnitude) &&
        bounded_magnitude(
            footprint.center.z,
            maximum_fixed_world_magnitude) &&
        exactly_representable_q8(footprint.center.x) &&
        exactly_representable_q8(footprint.center.z) &&
        positive_q8(footprint.semi_axis_x) &&
        positive_q8(footprint.semi_axis_z) &&
        bounded_magnitude(
            footprint.x_warp_square_offset,
            maximum_fixed_square_parameter) &&
        exactly_representable_q8(
            footprint.x_warp_square_offset) &&
        positive_q8(footprint.x_warp_divisor) &&
        bounded_magnitude(
            footprint.z_warp_square_offset,
            maximum_fixed_square_parameter) &&
        exactly_representable_q8(
            footprint.z_warp_square_offset) &&
        positive_q8(footprint.z_warp_divisor) &&
        bounded_magnitude(
            shape.future_waterline_y,
            maximum_fixed_world_magnitude) &&
        exactly_representable_q8(shape.future_waterline_y) &&
        positive_q8(
            shape.core_depth,
            maximum_fixed_height_offset) &&
        positive_q8(
            shape.rim_height,
            maximum_fixed_height_offset) &&
        bounded_magnitude(
            shape.future_waterline_y - shape.core_depth,
            maximum_fixed_world_magnitude) &&
        bounded_magnitude(
            shape.future_waterline_y + shape.rim_height,
            maximum_fixed_world_magnitude) &&
        shape.blend_end_radius <= maximum_fixed_profile_radius;
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
    const auto maximum_x =
        static_cast<double>(tile.origin.x) +
        static_cast<double>(tile.sample_columns - 1U) *
            static_cast<double>(tile.sample_spacing);
    const auto maximum_z =
        static_cast<double>(tile.origin.z) +
        static_cast<double>(tile.sample_rows - 1U) *
            static_cast<double>(tile.sample_spacing);
    return tile.height_offsets.size() == expected_size &&
        bounded_magnitude(
            tile.origin.x,
            maximum_fixed_world_magnitude) &&
        bounded_magnitude(
            tile.origin.y,
            maximum_fixed_world_magnitude) &&
        bounded_magnitude(
            tile.origin.z,
            maximum_fixed_world_magnitude) &&
        bounded_magnitude(
            maximum_x,
            maximum_fixed_world_magnitude) &&
        bounded_magnitude(
            maximum_z,
            maximum_fixed_world_magnitude) &&
        exactly_representable_q8(tile.origin.x) &&
        exactly_representable_q8(tile.origin.y) &&
        exactly_representable_q8(tile.origin.z) &&
        positive_q8(tile.sample_spacing) &&
        std::all_of(
            tile.height_offsets.begin(),
            tile.height_offsets.end(),
            [&tile](const float height) {
                return bounded_magnitude(
                           height,
                           maximum_fixed_height_offset) &&
                    exactly_representable_q8(height) &&
                    bounded_magnitude(
                        static_cast<double>(tile.origin.y) +
                            static_cast<double>(height),
                        maximum_fixed_world_magnitude);
            });
}

[[nodiscard]] std::int64_t rounded_divide(
    const std::int64_t numerator,
    const std::int64_t denominator) noexcept
{
    const auto magnitude =
        numerator >= 0 ? numerator : -numerator;
    const auto quotient =
        (magnitude + denominator / 2) / denominator;
    return numerator >= 0 ? quotient : -quotient;
}

[[nodiscard]] std::int64_t to_q8(const double value) noexcept
{
    return static_cast<std::int64_t>(
        std::llround(value * static_cast<double>(q8_scale)));
}

struct FixedFootprint final {
    std::int64_t center_x_q8{};
    std::int64_t center_z_q8{};
    std::int64_t semi_axis_x_q8{};
    std::int64_t semi_axis_z_q8{};
    std::int64_t x_warp_square_offset_q16{};
    std::int64_t x_warp_divisor_q8{};
    std::int64_t z_warp_square_offset_q16{};
    std::int64_t z_warp_divisor_q8{};
};

[[nodiscard]] FixedFootprint fixed_footprint(
    const LakeBasinFootprint& footprint) noexcept
{
    return FixedFootprint{
        .center_x_q8 = to_q8(footprint.center.x),
        .center_z_q8 = to_q8(footprint.center.z),
        .semi_axis_x_q8 = to_q8(footprint.semi_axis_x),
        .semi_axis_z_q8 = to_q8(footprint.semi_axis_z),
        .x_warp_square_offset_q16 =
            to_q8(footprint.x_warp_square_offset) * q8_scale,
        .x_warp_divisor_q8 =
            to_q8(footprint.x_warp_divisor),
        .z_warp_square_offset_q16 =
            to_q8(footprint.z_warp_square_offset) * q8_scale,
        .z_warp_divisor_q8 =
            to_q8(footprint.z_warp_divisor),
    };
}

[[nodiscard]] std::int64_t normalized_radius_squared_q20(
    const FixedFootprint& footprint,
    const std::int64_t world_x_q8,
    const std::int64_t world_z_q8) noexcept
{
    const auto dx_q8 = world_x_q8 - footprint.center_x_q8;
    const auto dz_q8 = world_z_q8 - footprint.center_z_q8;
    const auto warped_x_q8 =
        dx_q8 +
        rounded_divide(
            dz_q8 * dz_q8 -
                footprint.x_warp_square_offset_q16,
            footprint.x_warp_divisor_q8);
    const auto warped_z_q8 =
        dz_q8 +
        rounded_divide(
            dx_q8 * dx_q8 -
                footprint.z_warp_square_offset_q16,
            footprint.z_warp_divisor_q8);
    const auto axis_x_squared =
        footprint.semi_axis_x_q8 *
        footprint.semi_axis_x_q8;
    const auto axis_z_squared =
        footprint.semi_axis_z_q8 *
        footprint.semi_axis_z_q8;
    return warped_x_q8 * warped_x_q8 * q20_scale /
            axis_x_squared +
        warped_z_q8 * warped_z_q8 * q20_scale /
            axis_z_squared;
}

[[nodiscard]] std::int64_t multiply_q20(
    const std::int64_t first,
    const std::int64_t second) noexcept
{
    return (first * second + q20_scale / 2) / q20_scale;
}

[[nodiscard]] std::int64_t fade_q20(
    const std::int64_t value) noexcept
{
    const auto clamped =
        std::clamp(value, std::int64_t{0}, q20_scale);
    const auto squared = multiply_q20(clamped, clamped);
    const auto cubed = multiply_q20(squared, clamped);
    const auto fourth = multiply_q20(cubed, clamped);
    const auto fifth = multiply_q20(fourth, clamped);
    return std::clamp(
        6 * fifth - 15 * fourth + 10 * cubed,
        std::int64_t{0},
        q20_scale);
}

[[nodiscard]] std::int64_t interval_amount_q20(
    const std::int64_t value,
    const std::int64_t minimum,
    const std::int64_t maximum) noexcept
{
    return std::clamp(
        (value - minimum) * q20_scale /
            (maximum - minimum),
        std::int64_t{0},
        q20_scale);
}

[[nodiscard]] std::int64_t interpolate_q8(
    const std::int64_t first,
    const std::int64_t second,
    const std::int64_t amount_q20) noexcept
{
    return first + rounded_divide(
        (second - first) * amount_q20,
        q20_scale);
}

[[nodiscard]] std::int64_t radius_threshold_q20(
    const double radius) noexcept
{
    return static_cast<std::int64_t>(
        radius * radius *
        static_cast<double>(q20_scale));
}

[[nodiscard]] bool fixed_warp_is_safe(
    const HeightTile& tile,
    const FixedFootprint& footprint) noexcept
{
    const auto minimum_x_q8 = to_q8(tile.origin.x);
    const auto minimum_z_q8 = to_q8(tile.origin.z);
    const auto spacing_q8 = to_q8(tile.sample_spacing);
    const auto maximum_x_q8 =
        minimum_x_q8 +
        static_cast<std::int64_t>(tile.sample_columns - 1U) *
            spacing_q8;
    const auto maximum_z_q8 =
        minimum_z_q8 +
        static_cast<std::int64_t>(tile.sample_rows - 1U) *
            spacing_q8;
    const auto maximum_dx_q8 = std::max(
        std::abs(minimum_x_q8 - footprint.center_x_q8),
        std::abs(maximum_x_q8 - footprint.center_x_q8));
    const auto maximum_dz_q8 = std::max(
        std::abs(minimum_z_q8 - footprint.center_z_q8),
        std::abs(maximum_z_q8 - footprint.center_z_q8));
    const auto maximum_warped_x =
        static_cast<long double>(maximum_dx_q8) +
        (static_cast<long double>(maximum_dz_q8) *
             static_cast<long double>(maximum_dz_q8) +
         std::abs(static_cast<long double>(
             footprint.x_warp_square_offset_q16))) /
            static_cast<long double>(
                footprint.x_warp_divisor_q8) +
        1.0L;
    const auto maximum_warped_z =
        static_cast<long double>(maximum_dz_q8) +
        (static_cast<long double>(maximum_dx_q8) *
             static_cast<long double>(maximum_dx_q8) +
         std::abs(static_cast<long double>(
             footprint.z_warp_square_offset_q16))) /
            static_cast<long double>(
                footprint.z_warp_divisor_q8) +
        1.0L;
    return maximum_warped_x <= maximum_warped_component_q8 &&
        maximum_warped_z <= maximum_warped_component_q8;
}

[[nodiscard]] bool has_closed_canonical_topology(
    const HeightTile& tile,
    const std::vector<bool>& footprint_mask,
    const std::size_t footprint_samples,
    const float rim_world_height)
{
    const auto first_inside = std::find(
        footprint_mask.begin(),
        footprint_mask.end(),
        true);
    if (first_inside == footprint_mask.end()) {
        return false;
    }
    const auto first_index = static_cast<std::size_t>(
        std::distance(footprint_mask.begin(), first_inside));
    const auto first_x = static_cast<std::uint32_t>(
        first_index % tile.sample_columns);
    const auto first_z = static_cast<std::uint32_t>(
        first_index / tile.sample_columns);
    constexpr std::array<std::array<int, 2>, 6> neighbors{{
        {{-1, 0}},
        {{1, 0}},
        {{0, -1}},
        {{0, 1}},
        {{-1, -1}},
        {{1, 1}},
    }};
    const auto index_of = [&tile](
                              const std::uint32_t x,
                              const std::uint32_t z) {
        return static_cast<std::size_t>(z) *
                tile.sample_columns +
            x;
    };
    const auto visit = [&](const auto& can_enter,
                           const bool reject_boundary) {
        std::vector<bool> visited(
            footprint_mask.size(),
            false);
        std::deque<std::array<std::uint32_t, 2>> pending;
        pending.push_back({first_x, first_z});
        visited[first_index] = true;
        std::size_t visited_footprint_samples = 0;
        while (!pending.empty()) {
            const auto current = pending.front();
            pending.pop_front();
            const auto current_index =
                index_of(current[0], current[1]);
            visited_footprint_samples +=
                static_cast<std::size_t>(
                    footprint_mask[current_index]);
            if (reject_boundary &&
                (current[0] == 0U ||
                 current[1] == 0U ||
                 current[0] + 1U == tile.sample_columns ||
                 current[1] + 1U == tile.sample_rows)) {
                return false;
            }
            for (const auto offset : neighbors) {
                const auto next_x =
                    static_cast<std::int64_t>(current[0]) +
                    offset[0];
                const auto next_z =
                    static_cast<std::int64_t>(current[1]) +
                    offset[1];
                if (next_x < 0 ||
                    next_z < 0 ||
                    next_x >= static_cast<std::int64_t>(
                        tile.sample_columns) ||
                    next_z >= static_cast<std::int64_t>(
                        tile.sample_rows)) {
                    continue;
                }
                const auto unsigned_x =
                    static_cast<std::uint32_t>(next_x);
                const auto unsigned_z =
                    static_cast<std::uint32_t>(next_z);
                const auto next_index =
                    index_of(unsigned_x, unsigned_z);
                if (visited[next_index] ||
                    !can_enter(next_index)) {
                    continue;
                }
                visited[next_index] = true;
                pending.push_back({unsigned_x, unsigned_z});
            }
        }
        return visited_footprint_samples == footprint_samples;
    };
    if (!visit(
            [&footprint_mask](const std::size_t index) {
                return footprint_mask[index];
            },
            false)) {
        return false;
    }
    return visit(
        [&tile, rim_world_height](const std::size_t index) {
            return tile.origin.y +
                    tile.height_offsets[index] <
                rim_world_height;
        },
        true);
}

} // namespace

double lake_basin_normalized_radius_squared(
    const LakeBasinFootprint& footprint,
    const HorizontalPoint point) noexcept
{
    if (!finite_footprint(footprint) ||
        !std::isfinite(point.x) ||
        !std::isfinite(point.z)) {
        return std::numeric_limits<double>::infinity();
    }
    const auto dx =
        static_cast<double>(point.x) -
        static_cast<double>(footprint.center.x);
    const auto dz =
        static_cast<double>(point.z) -
        static_cast<double>(footprint.center.z);
    const auto warped_x =
        dx +
        (dz * dz -
         static_cast<double>(
             footprint.x_warp_square_offset)) /
            static_cast<double>(footprint.x_warp_divisor);
    const auto warped_z =
        dz +
        (dx * dx -
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

core::Result<HeightTile> shape_closed_lake_basin(
    HeightTile base,
    const LakeBasinShape& shape)
{
    if (!valid_tile(base)) {
        return core::Result<HeightTile>::failure(
            basin_error(
                "Lake-basin shaping requires a valid finite height tile"));
    }
    if (!valid_shape(shape)) {
        return core::Result<HeightTile>::failure(
            basin_error(
                "Lake-basin footprint and profile parameters are invalid"));
    }
    if (!fixed_shape_is_representable(shape)) {
        return core::Result<HeightTile>::failure(
            basin_error(
                "Lake-basin parameters exceed the safe Q8/Q20 "
                "representation"));
    }

    const auto footprint = fixed_footprint(shape.footprint);
    const auto origin_x_q8 = to_q8(base.origin.x);
    const auto origin_y_q8 = to_q8(base.origin.y);
    const auto origin_z_q8 = to_q8(base.origin.z);
    const auto spacing_q8 = to_q8(base.sample_spacing);
    const auto waterline_q8 = to_q8(shape.future_waterline_y);
    const auto core_depth_q8 = to_q8(shape.core_depth);
    const auto rim_world_q8 =
        waterline_q8 + to_q8(shape.rim_height);
    const auto shoreline_q20 = q20_scale;
    const auto rise_end_q20 =
        radius_threshold_q20(shape.rise_end_radius);
    const auto rim_end_q20 =
        radius_threshold_q20(shape.rim_end_radius);
    const auto blend_end_q20 =
        radius_threshold_q20(shape.blend_end_radius);
    if (spacing_q8 <= 0 ||
        !fixed_warp_is_safe(base, footprint) ||
        rise_end_q20 <= shoreline_q20 ||
        rim_end_q20 <= rise_end_q20 ||
        blend_end_q20 <= rim_end_q20) {
        return core::Result<HeightTile>::failure(
            basin_error(
                "Lake-basin fixed-point profile or warp is not "
                "representable"));
    }

    const auto boundary_inside_support =
        [&](const std::uint32_t x, const std::uint32_t z) {
            const auto world_x_q8 =
                origin_x_q8 +
                static_cast<std::int64_t>(x) * spacing_q8;
            const auto world_z_q8 =
                origin_z_q8 +
                static_cast<std::int64_t>(z) * spacing_q8;
            return normalized_radius_squared_q20(
                       footprint,
                       world_x_q8,
                       world_z_q8) <=
                blend_end_q20;
        };
    for (std::uint32_t x = 0; x < base.sample_columns; ++x) {
        if (boundary_inside_support(x, 0U) ||
            boundary_inside_support(x, base.sample_rows - 1U)) {
            return core::Result<HeightTile>::failure(
                basin_error(
                    "Lake-basin shaping support reaches a terrain edge"));
        }
    }
    for (std::uint32_t z = 0; z < base.sample_rows; ++z) {
        if (boundary_inside_support(0U, z) ||
            boundary_inside_support(base.sample_columns - 1U, z)) {
            return core::Result<HeightTile>::failure(
                basin_error(
                    "Lake-basin shaping support reaches a terrain edge"));
        }
    }

    std::size_t footprint_samples = 0;
    std::size_t rim_samples = 0;
    std::vector<bool> footprint_mask(
        base.height_offsets.size(),
        false);
    for (std::uint32_t z = 0; z < base.sample_rows; ++z) {
        const auto world_z_q8 =
            origin_z_q8 +
            static_cast<std::int64_t>(z) * spacing_q8;
        for (std::uint32_t x = 0; x < base.sample_columns; ++x) {
            const auto world_x_q8 =
                origin_x_q8 +
                static_cast<std::int64_t>(x) * spacing_q8;
            const auto rho_q20 =
                normalized_radius_squared_q20(
                    footprint,
                    world_x_q8,
                    world_z_q8);
            if (rho_q20 > blend_end_q20) {
                continue;
            }

            const auto index =
                static_cast<std::size_t>(z) *
                    base.sample_columns +
                x;
            const auto base_offset_q8 =
                to_q8(base.height_offsets[index]);
            const auto base_world_q8 =
                origin_y_q8 + base_offset_q8;
            std::int64_t world_height_q8 = base_world_q8;
            if (rho_q20 <= shoreline_q20) {
                const auto remaining =
                    shoreline_q20 - rho_q20;
                const auto squared_remaining =
                    multiply_q20(remaining, remaining);
                const auto depth_q8 =
                    (core_depth_q8 * squared_remaining +
                     q20_scale / 2) /
                    q20_scale;
                world_height_q8 =
                    waterline_q8 - depth_q8;
                ++footprint_samples;
                footprint_mask[index] = true;
            }
            else if (rho_q20 <= rise_end_q20) {
                const auto amount = fade_q20(
                    interval_amount_q20(
                        rho_q20,
                        shoreline_q20,
                        rise_end_q20));
                world_height_q8 = interpolate_q8(
                    waterline_q8,
                    rim_world_q8,
                    amount);
            }
            else if (rho_q20 <= rim_end_q20) {
                world_height_q8 = rim_world_q8;
                ++rim_samples;
            }
            else {
                const auto amount = fade_q20(
                    interval_amount_q20(
                        rho_q20,
                        rim_end_q20,
                        blend_end_q20));
                world_height_q8 = interpolate_q8(
                    rim_world_q8,
                    base_world_q8,
                    amount);
            }
            base.height_offsets[index] =
                static_cast<float>(
                    world_height_q8 - origin_y_q8) /
                static_cast<float>(q8_scale);
        }
    }
    if (footprint_samples == 0U || rim_samples == 0U) {
        return core::Result<HeightTile>::failure(
            basin_error(
                "Lake-basin footprint or closed rim contains no samples"));
    }
    if (!has_closed_canonical_topology(
            base,
            footprint_mask,
            footprint_samples,
            static_cast<float>(rim_world_q8) /
                static_cast<float>(q8_scale))) {
        return core::Result<HeightTile>::failure(
            basin_error(
                "Lake-basin footprint is disconnected or its canonical "
                "rim is open"));
    }
    return core::Result<HeightTile>::success(std::move(base));
}

} // namespace shark::terrain
