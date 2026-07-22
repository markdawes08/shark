#include <shark/terrain/height_tile.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace shark::terrain {
namespace {

[[nodiscard]] core::Error terrain_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] constexpr std::size_t sample_index(
    const std::uint32_t x,
    const std::uint32_t z,
    const std::uint32_t columns) noexcept
{
    return static_cast<std::size_t>(z) *
        static_cast<std::size_t>(columns) +
        static_cast<std::size_t>(x);
}

struct FixedTriangleIndices final {
    std::array<std::size_t, 3> vertices;
};

enum class HorizontalAxis : std::uint8_t {
    x = 1,
    z,
};

[[nodiscard]] constexpr float horizontal_coordinate(
    const HeightTile& tile,
    const std::uint32_t index,
    const HorizontalAxis axis) noexcept
{
    const auto origin = axis == HorizontalAxis::x
        ? tile.origin.x
        : tile.origin.z;
    return origin +
        static_cast<float>(index) * tile.sample_spacing;
}

[[nodiscard]] constexpr FixedTriangleIndices fixed_triangle_indices(
    const std::uint32_t cell_x,
    const std::uint32_t cell_z,
    const std::uint32_t columns,
    const HeightTileTriangle triangle) noexcept
{
    const auto v00 = sample_index(cell_x, cell_z, columns);
    const auto v10 = sample_index(cell_x + 1U, cell_z, columns);
    const auto v01 = sample_index(cell_x, cell_z + 1U, columns);
    const auto v11 = sample_index(cell_x + 1U, cell_z + 1U, columns);
    if (triangle == HeightTileTriangle::v00_v01_v11) {
        return {{{v00, v01, v11}}};
    }
    return {{{v00, v11, v10}}};
}

[[nodiscard]] constexpr math::Float3 sample_position(
    const HeightTile& tile,
    const std::uint32_t x,
    const std::uint32_t z) noexcept
{
    return math::Float3{
        horizontal_coordinate(tile, x, HorizontalAxis::x),
        tile.origin.y +
            tile.height_offsets[sample_index(
                x,
                z,
                tile.sample_columns)],
        horizontal_coordinate(tile, z, HorizontalAxis::z),
    };
}

struct CellCoordinate final {
    std::uint32_t cell{};
    double local{};
};

[[nodiscard]] CellCoordinate locate_cell_coordinate(
    const HeightTile& tile,
    const float world_coordinate,
    const std::uint32_t sample_count,
    const HorizontalAxis axis) noexcept
{
    // Find the first sample strictly greater than the query. Exact internal
    // samples therefore belong to the cell that starts at that sample, while
    // the tile's maximum sample remains owned by the final cell.
    std::uint32_t first = 0;
    std::uint32_t last = sample_count;
    while (first < last) {
        const auto middle = first + (last - first) / 2U;
        if (horizontal_coordinate(tile, middle, axis) <=
            world_coordinate) {
            first = middle + 1U;
        }
        else {
            last = middle;
        }
    }

    const auto cell = std::min(first - 1U, sample_count - 2U);
    const auto cell_minimum = static_cast<double>(
        horizontal_coordinate(tile, cell, axis));
    const auto cell_maximum = static_cast<double>(
        horizontal_coordinate(tile, cell + 1U, axis));
    const auto local = std::clamp(
        (static_cast<double>(world_coordinate) - cell_minimum) /
            (cell_maximum - cell_minimum),
        0.0,
        1.0);
    return CellCoordinate{cell, local};
}

[[nodiscard]] std::array<math::Float3, 3> fixed_triangle_positions(
    const HeightTile& tile,
    const std::uint32_t cell_x,
    const std::uint32_t cell_z,
    const HeightTileTriangle triangle) noexcept
{
    const auto indices = fixed_triangle_indices(
        cell_x,
        cell_z,
        tile.sample_columns,
        triangle);
    std::array<math::Float3, 3> positions;
    for (std::size_t index = 0; index < positions.size(); ++index) {
        const auto vertex = indices.vertices[index];
        positions[index] = sample_position(
            tile,
            static_cast<std::uint32_t>(
                vertex % tile.sample_columns),
            static_cast<std::uint32_t>(
                vertex / tile.sample_columns));
    }
    return positions;
}

[[nodiscard]] constexpr int squared(const int value) noexcept
{
    return value * value;
}

[[nodiscard]] constexpr float deterministic_height_offset(
    const std::uint32_t x,
    const std::uint32_t z) noexcept
{
    const auto signed_x = static_cast<int>(x);
    const auto signed_z = static_cast<int>(z);
    const auto first_hill = std::max(
        0,
        144 -
            squared(signed_x - 10) -
            squared(signed_z - 18));
    const auto second_hill = std::max(
        0,
        81 -
            squared(signed_x - 24) -
            squared(signed_z - 9));
    const auto basin = std::max(
        0,
        64 -
            squared(signed_x - 21) -
            squared(signed_z - 24));

    // Every denominator is a power of two, so the fixture's landmark values
    // and extrema have exact binary floating-point representations.
    return static_cast<float>(first_hill + signed_x - 16) / 64.0F +
        static_cast<float>(second_hill) / 128.0F -
        static_cast<float>(basin) / 64.0F;
}

struct RollingTerrainOctave final {
    std::uint32_t sample_period{};
    std::int32_t amplitude_meters{};
    std::uint32_t phase_x{};
    std::uint32_t phase_z{};
    bool diagonal{};
};

inline constexpr std::array rolling_terrain_octaves{
    RollingTerrainOctave{128U, 10, 37U, 71U, false},
    RollingTerrainOctave{91U, 6, 19U, 53U, true},
    RollingTerrainOctave{32U, 3, 11U, 23U, false},
    RollingTerrainOctave{23U, 2, 7U, 13U, true},
    RollingTerrainOctave{8U, 1, 3U, 5U, false},
};
inline constexpr std::uint32_t rolling_terrain_seed_step =
    0x9E37'79B9U;
inline constexpr std::int64_t rolling_terrain_fade_scale =
    std::int64_t{1} << 30U;
inline constexpr std::int64_t rolling_terrain_lattice_scale =
    std::int64_t{1} << 23U;
inline constexpr std::int64_t rolling_terrain_q23_to_q8_scale =
    std::int64_t{1} << 15U;

[[nodiscard]] constexpr std::uint32_t rolling_terrain_hash(
    const std::uint32_t x,
    const std::uint32_t z,
    const std::uint32_t seed) noexcept
{
    auto value =
        x * 0x8DA6'B343U ^
        z * 0xD816'3841U ^
        seed;
    value ^= value >> 16U;
    value *= 0x7FEB'352DU;
    value ^= value >> 15U;
    value *= 0x846C'A68BU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] constexpr std::int32_t rolling_terrain_lattice_value_q23(
    const std::uint32_t x,
    const std::uint32_t z,
    const std::uint32_t seed) noexcept
{
    return static_cast<std::int32_t>(
        rolling_terrain_hash(x, z, seed) >> 8U) -
        static_cast<std::int32_t>(rolling_terrain_lattice_scale);
}

[[nodiscard]] constexpr std::int64_t multiply_q30(
    const std::int64_t first,
    const std::int64_t second) noexcept
{
    return first * second / rolling_terrain_fade_scale;
}

[[nodiscard]] constexpr std::int64_t quintic_fade_q30(
    const std::uint32_t remainder,
    const std::uint32_t period) noexcept
{
    const auto value =
        static_cast<std::int64_t>(remainder) *
        rolling_terrain_fade_scale /
        static_cast<std::int64_t>(period);
    const auto squared_value = multiply_q30(value, value);
    const auto cubed_value = multiply_q30(squared_value, value);
    const auto fourth_power = multiply_q30(cubed_value, value);
    const auto fifth_power = multiply_q30(fourth_power, value);
    return 6 * fifth_power -
        15 * fourth_power +
        10 * cubed_value;
}

[[nodiscard]] constexpr std::int32_t linear_interpolation_q23(
    const std::int32_t first,
    const std::int32_t second,
    const std::int64_t amount_q30) noexcept
{
    const auto delta =
        static_cast<std::int64_t>(second) -
        static_cast<std::int64_t>(first);
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(first) +
        delta * amount_q30 / rolling_terrain_fade_scale);
}

[[nodiscard]] constexpr std::pair<std::uint32_t, std::uint32_t>
rolling_terrain_sample_coordinates(
    const std::uint32_t x,
    const std::uint32_t z,
    const RollingTerrainOctave& octave) noexcept
{
    if (octave.diagonal) {
        return {
            x + z + octave.phase_x,
            x +
                (large_capacity_tile_sample_rows - 1U) -
                z +
                octave.phase_z,
        };
    }
    return {
        x + octave.phase_x,
        z + octave.phase_z,
    };
}

[[nodiscard]] constexpr std::int32_t rolling_terrain_value_noise_q23(
    const std::uint32_t x,
    const std::uint32_t z,
    const RollingTerrainOctave& octave,
    const std::uint32_t seed) noexcept
{
    const auto [sample_x, sample_z] =
        rolling_terrain_sample_coordinates(x, z, octave);
    const auto cell_x = sample_x / octave.sample_period;
    const auto cell_z = sample_z / octave.sample_period;
    const auto blend_x = quintic_fade_q30(
        sample_x % octave.sample_period,
        octave.sample_period);
    const auto blend_z = quintic_fade_q30(
        sample_z % octave.sample_period,
        octave.sample_period);

    const auto near_row = linear_interpolation_q23(
        rolling_terrain_lattice_value_q23(cell_x, cell_z, seed),
        rolling_terrain_lattice_value_q23(
            cell_x + 1U,
            cell_z,
            seed),
        blend_x);
    const auto far_row = linear_interpolation_q23(
        rolling_terrain_lattice_value_q23(
            cell_x,
            cell_z + 1U,
            seed),
        rolling_terrain_lattice_value_q23(
            cell_x + 1U,
            cell_z + 1U,
            seed),
        blend_x);
    return linear_interpolation_q23(
        near_row,
        far_row,
        blend_z);
}

[[nodiscard]] float large_rolling_height_offset(
    const std::uint32_t x,
    const std::uint32_t z) noexcept
{
    std::int64_t height_q23 = 0;
    for (std::size_t index = 0;
         index < rolling_terrain_octaves.size();
         ++index) {
        const auto& octave = rolling_terrain_octaves[index];
        const auto seed =
            large_capacity_tile_generation_seed +
            static_cast<std::uint32_t>(index) *
                rolling_terrain_seed_step;
        height_q23 +=
            static_cast<std::int64_t>(octave.amplitude_meters) *
            rolling_terrain_value_noise_q23(
                x,
                z,
                octave,
                seed);
    }

    // Round once, symmetrically, from Q23 to Q8 meters. Every authored height
    // is therefore exact in binary float and stable across build modes.
    const auto magnitude_q23 =
        height_q23 >= 0 ? height_q23 : -height_q23;
    const auto magnitude_q8 =
        (magnitude_q23 + rolling_terrain_q23_to_q8_scale / 2) /
        rolling_terrain_q23_to_q8_scale;
    const auto height_q8 =
        height_q23 >= 0 ? magnitude_q8 : -magnitude_q8;
    return static_cast<float>(height_q8) *
        large_capacity_tile_height_quantum;
}

[[nodiscard]] constexpr math::Float3 subtract(
    const math::Float3 left,
    const math::Float3 right) noexcept
{
    return math::Float3{
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

[[nodiscard]] constexpr math::Float3 cross(
    const math::Float3 left,
    const math::Float3 right) noexcept
{
    return math::Float3{
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

constexpr void add_assign(
    math::Float3& destination,
    const math::Float3 value) noexcept
{
    destination.x += value.x;
    destination.y += value.y;
    destination.z += value.z;
}

[[nodiscard]] constexpr float length_squared(
    const math::Float3 value) noexcept
{
    return value.x * value.x +
        value.y * value.y +
        value.z * value.z;
}

[[nodiscard]] math::Float3 normalized(
    const math::Float3 value) noexcept
{
    const auto inverse_length =
        1.0F / std::sqrt(length_squared(value));
    return math::Float3{
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
    };
}

[[nodiscard]] math::Float3 triangle_normal(
    const HeightTile& tile,
    const std::uint32_t cell_x,
    const std::uint32_t cell_z,
    const HeightTileTriangle triangle) noexcept
{
    const auto positions = fixed_triangle_positions(
        tile,
        cell_x,
        cell_z,
        triangle);
    return normalized(cross(
        subtract(positions[1], positions[0]),
        subtract(positions[2], positions[0])));
}

struct NormalizedRay final {
    std::array<double, 3> origin;
    std::array<double, 3> direction;
};

[[nodiscard]] core::Result<NormalizedRay> normalize_ray(
    const Ray3& ray,
    const float maximum_distance)
{
    if (!math::is_finite(ray.origin) ||
        !math::is_finite(ray.direction)) {
        return core::Result<NormalizedRay>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "Terrain rays require finite origin and direction coordinates"));
    }
    if (!std::isfinite(maximum_distance) ||
        maximum_distance <= 0.0F) {
        return core::Result<NormalizedRay>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "Terrain ray maximum distance must be finite and positive"));
    }

    const std::array<double, 3> direction{{
        static_cast<double>(ray.direction.x),
        static_cast<double>(ray.direction.y),
        static_cast<double>(ray.direction.z),
    }};
    const auto squared_length =
        direction[0] * direction[0] +
        direction[1] * direction[1] +
        direction[2] * direction[2];
    if (!std::isfinite(squared_length) || squared_length <= 0.0) {
        return core::Result<NormalizedRay>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "Terrain ray direction must be nonzero"));
    }
    const auto inverse_length = 1.0 / std::sqrt(squared_length);
    return core::Result<NormalizedRay>::success(NormalizedRay{
        {{
            static_cast<double>(ray.origin.x),
            static_cast<double>(ray.origin.y),
            static_cast<double>(ray.origin.z),
        }},
        {{
            direction[0] * inverse_length,
            direction[1] * inverse_length,
            direction[2] * inverse_length,
        }},
    });
}

[[nodiscard]] bool intersect_slab(
    const double origin,
    const double direction,
    const float minimum,
    const float maximum,
    double& entry,
    double& exit) noexcept
{
    if (direction == 0.0) {
        return origin >= static_cast<double>(minimum) &&
            origin <= static_cast<double>(maximum);
    }
    auto first =
        (static_cast<double>(minimum) - origin) / direction;
    auto second =
        (static_cast<double>(maximum) - origin) / direction;
    if (first > second) {
        std::swap(first, second);
    }
    entry = std::max(entry, first);
    exit = std::min(exit, second);
    return entry <= exit;
}

[[nodiscard]] std::optional<BoundsInterval> intersect_bounds_normalized(
    const Bounds3 bounds,
    const NormalizedRay& ray,
    const float maximum_distance) noexcept
{
    double entry = 0.0;
    double exit = static_cast<double>(maximum_distance);
    if (!intersect_slab(
            ray.origin[0],
            ray.direction[0],
            bounds.minimum.x,
            bounds.maximum.x,
            entry,
            exit) ||
        !intersect_slab(
            ray.origin[1],
            ray.direction[1],
            bounds.minimum.y,
            bounds.maximum.y,
            entry,
            exit) ||
        !intersect_slab(
            ray.origin[2],
            ray.direction[2],
            bounds.minimum.z,
            bounds.maximum.z,
            entry,
            exit)) {
        return std::nullopt;
    }
    return BoundsInterval{
        static_cast<float>(entry),
        static_cast<float>(exit),
    };
}

struct TriangleRayHit final {
    double distance{};
};

[[nodiscard]] std::optional<TriangleRayHit> intersect_triangle(
    const NormalizedRay& ray,
    const std::array<math::Float3, 3>& triangle,
    const double maximum_distance) noexcept
{
    const std::array<double, 3> vertex0{{
        static_cast<double>(triangle[0].x),
        static_cast<double>(triangle[0].y),
        static_cast<double>(triangle[0].z),
    }};
    const std::array<double, 3> edge1{{
        static_cast<double>(triangle[1].x) - vertex0[0],
        static_cast<double>(triangle[1].y) - vertex0[1],
        static_cast<double>(triangle[1].z) - vertex0[2],
    }};
    const std::array<double, 3> edge2{{
        static_cast<double>(triangle[2].x) - vertex0[0],
        static_cast<double>(triangle[2].y) - vertex0[1],
        static_cast<double>(triangle[2].z) - vertex0[2],
    }};
    const std::array<double, 3> ray_cross_edge2{{
        ray.direction[1] * edge2[2] -
            ray.direction[2] * edge2[1],
        ray.direction[2] * edge2[0] -
            ray.direction[0] * edge2[2],
        ray.direction[0] * edge2[1] -
            ray.direction[1] * edge2[0],
    }};
    const auto determinant =
        edge1[0] * ray_cross_edge2[0] +
        edge1[1] * ray_cross_edge2[1] +
        edge1[2] * ray_cross_edge2[2];
    const auto edge1_length = std::sqrt(
        edge1[0] * edge1[0] +
        edge1[1] * edge1[1] +
        edge1[2] * edge1[2]);
    const auto ray_cross_edge2_length = std::sqrt(
        ray_cross_edge2[0] * ray_cross_edge2[0] +
        ray_cross_edge2[1] * ray_cross_edge2[1] +
        ray_cross_edge2[2] * ray_cross_edge2[2]);
    const auto determinant_scale =
        edge1_length * ray_cross_edge2_length;
    constexpr double relative_parallel_epsilon =
        std::numeric_limits<double>::epsilon() * 64.0;
    if (determinant_scale == 0.0 ||
        std::abs(determinant) <=
            determinant_scale * relative_parallel_epsilon) {
        return std::nullopt;
    }

    const auto inverse_determinant = 1.0 / determinant;
    const std::array<double, 3> origin_delta{{
        ray.origin[0] - vertex0[0],
        ray.origin[1] - vertex0[1],
        ray.origin[2] - vertex0[2],
    }};
    const auto second_weight =
        (origin_delta[0] * ray_cross_edge2[0] +
         origin_delta[1] * ray_cross_edge2[1] +
         origin_delta[2] * ray_cross_edge2[2]) *
        inverse_determinant;
    constexpr double edge_epsilon = 1.0e-9;
    if (second_weight < -edge_epsilon ||
        second_weight > 1.0 + edge_epsilon) {
        return std::nullopt;
    }

    const std::array<double, 3> origin_cross_edge1{{
        origin_delta[1] * edge1[2] -
            origin_delta[2] * edge1[1],
        origin_delta[2] * edge1[0] -
            origin_delta[0] * edge1[2],
        origin_delta[0] * edge1[1] -
            origin_delta[1] * edge1[0],
    }};
    const auto third_weight =
        (ray.direction[0] * origin_cross_edge1[0] +
         ray.direction[1] * origin_cross_edge1[1] +
         ray.direction[2] * origin_cross_edge1[2]) *
        inverse_determinant;
    if (third_weight < -edge_epsilon ||
        second_weight + third_weight > 1.0 + edge_epsilon) {
        return std::nullopt;
    }

    const auto distance =
        (edge2[0] * origin_cross_edge1[0] +
         edge2[1] * origin_cross_edge1[1] +
         edge2[2] * origin_cross_edge1[2]) *
        inverse_determinant;
    if (distance < -edge_epsilon ||
        distance > maximum_distance) {
        return std::nullopt;
    }
    return TriangleRayHit{std::max(0.0, distance)};
}

using PrecisePoint = std::array<double, 3>;

[[nodiscard]] PrecisePoint precise_point(
    const math::Float3 value) noexcept
{
    return {{
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z),
    }};
}

[[nodiscard]] PrecisePoint subtract(
    const PrecisePoint& left,
    const PrecisePoint& right) noexcept
{
    return {{
        left[0] - right[0],
        left[1] - right[1],
        left[2] - right[2],
    }};
}

[[nodiscard]] PrecisePoint add_scaled(
    const PrecisePoint& origin,
    const PrecisePoint& direction,
    const double parameter) noexcept
{
    return {{
        origin[0] + direction[0] * parameter,
        origin[1] + direction[1] * parameter,
        origin[2] + direction[2] * parameter,
    }};
}

[[nodiscard]] double dot(
    const PrecisePoint& left,
    const PrecisePoint& right) noexcept
{
    return left[0] * right[0] +
        left[1] * right[1] +
        left[2] * right[2];
}

[[nodiscard]] PrecisePoint cross(
    const PrecisePoint& left,
    const PrecisePoint& right) noexcept
{
    return {{
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    }};
}

struct PreciseTrianglePoint final {
    PrecisePoint position;
    PrecisePoint barycentrics;
};

[[nodiscard]] PreciseTrianglePoint closest_point_on_triangle(
    const PrecisePoint& point,
    const std::array<PrecisePoint, 3>& triangle) noexcept
{
    const auto edge_ab = subtract(triangle[1], triangle[0]);
    const auto edge_ac = subtract(triangle[2], triangle[0]);
    const auto from_a = subtract(point, triangle[0]);
    const auto d1 = dot(edge_ab, from_a);
    const auto d2 = dot(edge_ac, from_a);
    if (d1 <= 0.0 && d2 <= 0.0) {
        return {triangle[0], {{1.0, 0.0, 0.0}}};
    }

    const auto from_b = subtract(point, triangle[1]);
    const auto d3 = dot(edge_ab, from_b);
    const auto d4 = dot(edge_ac, from_b);
    if (d3 >= 0.0 && d4 <= d3) {
        return {triangle[1], {{0.0, 1.0, 0.0}}};
    }

    const auto vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        const auto denominator = d1 - d3;
        const auto v = d1 / denominator;
        return {
            add_scaled(triangle[0], edge_ab, v),
            {{1.0 - v, v, 0.0}},
        };
    }

    const auto from_c = subtract(point, triangle[2]);
    const auto d5 = dot(edge_ab, from_c);
    const auto d6 = dot(edge_ac, from_c);
    if (d6 >= 0.0 && d5 <= d6) {
        return {triangle[2], {{0.0, 0.0, 1.0}}};
    }

    const auto vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        const auto denominator = d2 - d6;
        const auto w = d2 / denominator;
        return {
            add_scaled(triangle[0], edge_ac, w),
            {{1.0 - w, 0.0, w}},
        };
    }

    const auto va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0) {
        const auto first = d4 - d3;
        const auto second = d5 - d6;
        const auto w = first / (first + second);
        const auto edge_bc = subtract(triangle[2], triangle[1]);
        return {
            add_scaled(triangle[1], edge_bc, w),
            {{0.0, 1.0 - w, w}},
        };
    }

    const auto denominator = 1.0 / (va + vb + vc);
    const auto v = vb * denominator;
    const auto w = vc * denominator;
    return {
        add_scaled(
            add_scaled(triangle[0], edge_ab, v),
            edge_ac,
            w),
        {{1.0 - v - w, v, w}},
    };
}

struct SegmentPairClosestPoint final {
    double first_parameter{};
    double second_parameter{};
    PrecisePoint first_position;
    PrecisePoint second_position;
};

[[nodiscard]] SegmentPairClosestPoint closest_segment_pair(
    const PrecisePoint& first_start,
    const PrecisePoint& first_end,
    const PrecisePoint& second_start,
    const PrecisePoint& second_end) noexcept
{
    const auto first_direction = subtract(first_end, first_start);
    const auto second_direction = subtract(second_end, second_start);
    const auto start_offset = subtract(first_start, second_start);
    const auto first_length_squared =
        dot(first_direction, first_direction);
    const auto second_length_squared =
        dot(second_direction, second_direction);
    const auto second_projection =
        dot(second_direction, start_offset);

    double first_parameter = 0.0;
    double second_parameter = 0.0;
    if (first_length_squared == 0.0 &&
        second_length_squared == 0.0) {
        return {
            0.0,
            0.0,
            first_start,
            second_start,
        };
    }
    if (first_length_squared == 0.0) {
        second_parameter = std::clamp(
            second_projection / second_length_squared,
            0.0,
            1.0);
    }
    else {
        const auto first_projection =
            dot(first_direction, start_offset);
        if (second_length_squared == 0.0) {
            first_parameter = std::clamp(
                -first_projection / first_length_squared,
                0.0,
                1.0);
        }
        else {
            const auto shared_projection =
                dot(first_direction, second_direction);
            const auto denominator =
                first_length_squared * second_length_squared -
                shared_projection * shared_projection;
            constexpr double relative_parallel_epsilon =
                std::numeric_limits<double>::epsilon() * 64.0;
            if (denominator >
                first_length_squared * second_length_squared *
                    relative_parallel_epsilon) {
                first_parameter = std::clamp(
                    (shared_projection * second_projection -
                     first_projection * second_length_squared) /
                        denominator,
                    0.0,
                    1.0);
            }
            second_parameter =
                (shared_projection * first_parameter +
                 second_projection) /
                second_length_squared;
            if (second_parameter < 0.0) {
                second_parameter = 0.0;
                first_parameter = std::clamp(
                    -first_projection / first_length_squared,
                    0.0,
                    1.0);
            }
            else if (second_parameter > 1.0) {
                second_parameter = 1.0;
                first_parameter = std::clamp(
                    (shared_projection - first_projection) /
                        first_length_squared,
                    0.0,
                    1.0);
            }
        }
    }

    return {
        first_parameter,
        second_parameter,
        add_scaled(
            first_start,
            first_direction,
            first_parameter),
        add_scaled(
            second_start,
            second_direction,
            second_parameter),
    };
}

struct SegmentTriangleClosestPoint final {
    double distance_squared{
        std::numeric_limits<double>::infinity()};
    double segment_parameter{};
    PrecisePoint segment_position;
    PrecisePoint triangle_position;
    PrecisePoint triangle_barycentrics;
};

void consider_closest_point(
    SegmentTriangleClosestPoint& closest,
    const double segment_parameter,
    const PrecisePoint& segment_position,
    const PrecisePoint& triangle_position,
    const PrecisePoint& triangle_barycentrics) noexcept
{
    const auto offset = subtract(
        triangle_position,
        segment_position);
    const auto distance_squared = dot(offset, offset);
    if (distance_squared < closest.distance_squared) {
        closest = {
            distance_squared,
            segment_parameter,
            segment_position,
            triangle_position,
            triangle_barycentrics,
        };
    }
}

[[nodiscard]] std::optional<PrecisePoint>
segment_triangle_intersection_barycentrics(
    const PrecisePoint& segment_start,
    const PrecisePoint& segment_direction,
    const std::array<PrecisePoint, 3>& triangle,
    double& segment_parameter) noexcept
{
    const auto edge_ab = subtract(triangle[1], triangle[0]);
    const auto edge_ac = subtract(triangle[2], triangle[0]);
    const auto normal = cross(edge_ab, edge_ac);
    const auto denominator = dot(normal, segment_direction);
    const auto denominator_scale = std::sqrt(
        dot(normal, normal) *
        dot(segment_direction, segment_direction));
    constexpr double relative_parallel_epsilon =
        std::numeric_limits<double>::epsilon() * 64.0;
    if (denominator_scale == 0.0 ||
        std::abs(denominator) <=
            denominator_scale * relative_parallel_epsilon) {
        return std::nullopt;
    }

    segment_parameter =
        dot(normal, subtract(triangle[0], segment_start)) /
        denominator;
    constexpr double edge_epsilon = 1.0e-12;
    if (segment_parameter < -edge_epsilon ||
        segment_parameter > 1.0 + edge_epsilon) {
        return std::nullopt;
    }
    segment_parameter = std::clamp(
        segment_parameter,
        0.0,
        1.0);
    const auto intersection = add_scaled(
        segment_start,
        segment_direction,
        segment_parameter);

    const auto from_a = subtract(intersection, triangle[0]);
    const auto ab_ab = dot(edge_ab, edge_ab);
    const auto ab_ac = dot(edge_ab, edge_ac);
    const auto ac_ac = dot(edge_ac, edge_ac);
    const auto point_ab = dot(from_a, edge_ab);
    const auto point_ac = dot(from_a, edge_ac);
    const auto barycentric_denominator =
        ab_ab * ac_ac - ab_ac * ab_ac;
    if (barycentric_denominator == 0.0) {
        return std::nullopt;
    }
    const auto second =
        (ac_ac * point_ab - ab_ac * point_ac) /
        barycentric_denominator;
    const auto third =
        (ab_ab * point_ac - ab_ac * point_ab) /
        barycentric_denominator;
    const auto first = 1.0 - second - third;
    if (first < -edge_epsilon ||
        second < -edge_epsilon ||
        third < -edge_epsilon) {
        return std::nullopt;
    }
    PrecisePoint barycentrics{{
        std::clamp(first, 0.0, 1.0),
        std::clamp(second, 0.0, 1.0),
        std::clamp(third, 0.0, 1.0),
    }};
    const auto sum =
        barycentrics[0] +
        barycentrics[1] +
        barycentrics[2];
    for (auto& component : barycentrics) {
        component /= sum;
    }
    return barycentrics;
}

[[nodiscard]] SegmentTriangleClosestPoint
closest_segment_triangle_points(
    const PrecisePoint& segment_start,
    const PrecisePoint& segment_end,
    const std::array<PrecisePoint, 3>& triangle) noexcept
{
    SegmentTriangleClosestPoint closest;
    const auto segment_direction = subtract(
        segment_end,
        segment_start);
    double intersection_parameter = 0.0;
    const auto intersection_barycentrics =
        segment_triangle_intersection_barycentrics(
            segment_start,
            segment_direction,
            triangle,
            intersection_parameter);
    if (intersection_barycentrics.has_value()) {
        const auto intersection = add_scaled(
            segment_start,
            segment_direction,
            intersection_parameter);
        consider_closest_point(
            closest,
            intersection_parameter,
            intersection,
            intersection,
            *intersection_barycentrics);
    }

    const auto first_triangle_point = closest_point_on_triangle(
        segment_start,
        triangle);
    consider_closest_point(
        closest,
        0.0,
        segment_start,
        first_triangle_point.position,
        first_triangle_point.barycentrics);
    const auto second_triangle_point = closest_point_on_triangle(
        segment_end,
        triangle);
    consider_closest_point(
        closest,
        1.0,
        segment_end,
        second_triangle_point.position,
        second_triangle_point.barycentrics);

    constexpr std::array<std::array<std::size_t, 2>, 3> edges{{
        {{0U, 1U}},
        {{1U, 2U}},
        {{2U, 0U}},
    }};
    for (std::size_t edge_index = 0;
         edge_index < edges.size();
         ++edge_index) {
        const auto first_vertex = edges[edge_index][0];
        const auto second_vertex = edges[edge_index][1];
        const auto edge_closest = closest_segment_pair(
            segment_start,
            segment_end,
            triangle[first_vertex],
            triangle[second_vertex]);
        PrecisePoint barycentrics{};
        barycentrics[first_vertex] =
            1.0 - edge_closest.second_parameter;
        barycentrics[second_vertex] =
            edge_closest.second_parameter;
        consider_closest_point(
            closest,
            edge_closest.first_parameter,
            edge_closest.first_position,
            edge_closest.second_position,
            barycentrics);
    }
    return closest;
}

[[nodiscard]] bool representable_float(
    const double value) noexcept
{
    return std::isfinite(value) &&
        std::abs(value) <=
            static_cast<double>(
                std::numeric_limits<float>::max());
}

[[nodiscard]] bool representable_point(
    const PrecisePoint& point) noexcept
{
    return representable_float(point[0]) &&
        representable_float(point[1]) &&
        representable_float(point[2]);
}

[[nodiscard]] math::Float3 float3(
    const PrecisePoint& point) noexcept
{
    return {
        static_cast<float>(point[0]),
        static_cast<float>(point[1]),
        static_cast<float>(point[2]),
    };
}

[[nodiscard]] BoundsLineGeometry make_bounds_lines(
    const Bounds3 bounds) noexcept
{
    const auto& minimum = bounds.minimum;
    const auto& maximum = bounds.maximum;
    return BoundsLineGeometry{
        {{
            {minimum.x, minimum.y, minimum.z},
            {maximum.x, minimum.y, minimum.z},
            {maximum.x, minimum.y, maximum.z},
            {minimum.x, minimum.y, maximum.z},
            {minimum.x, maximum.y, minimum.z},
            {maximum.x, maximum.y, minimum.z},
            {maximum.x, maximum.y, maximum.z},
            {minimum.x, maximum.y, maximum.z},
        }},
        {{
            0, 1,
            1, 2,
            2, 3,
            3, 0,
            4, 5,
            5, 6,
            6, 7,
            7, 4,
            0, 4,
            1, 5,
            2, 6,
            3, 7,
        }},
    };
}

[[nodiscard]] core::Result<Bounds3> validate_height_tile(
    const HeightTile& tile)
{
    if (tile.sample_columns < 2U || tile.sample_rows < 2U) {
        return core::Result<Bounds3>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "A height tile requires at least two samples on each axis"));
    }
    if (!std::isfinite(tile.sample_spacing) ||
        tile.sample_spacing <= 0.0F) {
        return core::Result<Bounds3>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "Height-tile sample spacing must be finite and positive"));
    }
    if (!math::is_finite(tile.origin)) {
        return core::Result<Bounds3>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "Height-tile origin coordinates must be finite"));
    }

    const auto columns =
        static_cast<std::size_t>(tile.sample_columns);
    const auto rows = static_cast<std::size_t>(tile.sample_rows);
    if (columns >
        std::numeric_limits<std::size_t>::max() / rows) {
        return core::Result<Bounds3>::failure(terrain_error(
            core::ErrorCode::unavailable,
            "Height-tile sample count overflows addressable storage"));
    }
    const auto vertex_count = columns * rows;
    if (tile.height_offsets.size() != vertex_count) {
        return core::Result<Bounds3>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "Height-tile storage does not match its sample dimensions"));
    }

    constexpr std::array axes{
        HorizontalAxis::x,
        HorizontalAxis::z,
    };
    for (const auto axis : axes) {
        const auto sample_count = axis == HorizontalAxis::x
            ? tile.sample_columns
            : tile.sample_rows;
        auto previous = horizontal_coordinate(tile, 0, axis);
        for (std::uint32_t index = 1;
             index < sample_count;
             ++index) {
            const auto current =
                horizontal_coordinate(tile, index, axis);
            if (!std::isfinite(current) || current <= previous) {
                return core::Result<Bounds3>::failure(terrain_error(
                    core::ErrorCode::invalid_argument,
                    "Height-tile horizontal sample coordinates must "
                    "remain finite and strictly increasing"));
            }
            previous = current;
        }
    }

    const auto maximum_float = std::numeric_limits<float>::max();
    Bounds3 bounds{
        {maximum_float, maximum_float, maximum_float},
        {-maximum_float, -maximum_float, -maximum_float},
    };
    for (std::uint32_t z = 0; z < tile.sample_rows; ++z) {
        for (std::uint32_t x = 0; x < tile.sample_columns; ++x) {
            const auto height =
                tile.height_offsets[sample_index(
                    x,
                    z,
                    tile.sample_columns)];
            if (!std::isfinite(height)) {
                return core::Result<Bounds3>::failure(terrain_error(
                    core::ErrorCode::invalid_argument,
                    "Height-tile samples must all be finite"));
            }
            const auto position = sample_position(tile, x, z);
            if (!math::is_finite(position)) {
                return core::Result<Bounds3>::failure(terrain_error(
                    core::ErrorCode::invalid_argument,
                    "Height-tile coordinates exceed finite float storage"));
            }
            bounds.minimum.x = std::min(bounds.minimum.x, position.x);
            bounds.minimum.y = std::min(bounds.minimum.y, position.y);
            bounds.minimum.z = std::min(bounds.minimum.z, position.z);
            bounds.maximum.x = std::max(bounds.maximum.x, position.x);
            bounds.maximum.y = std::max(bounds.maximum.y, position.y);
            bounds.maximum.z = std::max(bounds.maximum.z, position.z);
        }
    }

    constexpr std::array triangles{
        HeightTileTriangle::v00_v01_v11,
        HeightTileTriangle::v00_v11_v10,
    };
    for (std::uint32_t z = 0; z + 1U < tile.sample_rows; ++z) {
        for (std::uint32_t x = 0;
             x + 1U < tile.sample_columns;
            ++x) {
            for (const auto triangle : triangles) {
                const auto positions = fixed_triangle_positions(
                    tile,
                    x,
                    z,
                    triangle);
                const auto face_normal = cross(
                    subtract(positions[1], positions[0]),
                    subtract(positions[2], positions[0]));
                const auto face_length_squared =
                    length_squared(face_normal);
                if (!std::isfinite(face_length_squared) ||
                    face_length_squared <= 0.0F ||
                    face_normal.y <= 0.0F) {
                    return core::Result<Bounds3>::failure(terrain_error(
                        core::ErrorCode::invalid_state,
                        "Height-tile triangulation produced invalid "
                        "winding"));
                }
            }
        }
    }
    return core::Result<Bounds3>::success(bounds);
}

struct SurfacePoint2 final {
    double x{};
    double z{};
};

struct SurfaceVertex final {
    SurfacePoint2 horizontal;
    double height{};
};

struct SurfaceTriangle final {
    std::array<SurfaceVertex, 3> vertices;
};

[[nodiscard]] SurfaceVertex surface_vertex(
    const HeightTile& tile,
    const std::uint16_t vertex_index) noexcept
{
    const auto index = static_cast<std::uint32_t>(vertex_index);
    const auto x = index % tile.sample_columns;
    const auto z = index / tile.sample_columns;
    return SurfaceVertex{
        {
            static_cast<double>(x),
            static_cast<double>(z),
        },
        static_cast<double>(
            sample_position(tile, x, z).y),
    };
}

[[nodiscard]] SurfaceTriangle surface_triangle(
    const HeightTile& tile,
    const std::array<std::uint16_t, 3> indices) noexcept
{
    return SurfaceTriangle{{{
        surface_vertex(tile, indices[0]),
        surface_vertex(tile, indices[1]),
        surface_vertex(tile, indices[2]),
    }}};
}

[[nodiscard]] constexpr double orientation(
    const SurfacePoint2 first,
    const SurfacePoint2 second,
    const SurfacePoint2 point) noexcept
{
    return
        (second.x - first.x) * (point.z - first.z) -
        (second.z - first.z) * (point.x - first.x);
}

[[nodiscard]] bool contains_projected_point(
    const SurfaceTriangle& triangle,
    const SurfacePoint2 point) noexcept
{
    const auto first = orientation(
        triangle.vertices[0].horizontal,
        triangle.vertices[1].horizontal,
        point);
    const auto second = orientation(
        triangle.vertices[1].horizontal,
        triangle.vertices[2].horizontal,
        point);
    const auto third = orientation(
        triangle.vertices[2].horizontal,
        triangle.vertices[0].horizontal,
        point);
    constexpr double edge_tolerance = 1.0e-10;
    return
        (first >= -edge_tolerance &&
         second >= -edge_tolerance &&
         third >= -edge_tolerance) ||
        (first <= edge_tolerance &&
         second <= edge_tolerance &&
         third <= edge_tolerance);
}

[[nodiscard]] double surface_height(
    const SurfaceTriangle& triangle,
    const SurfacePoint2 point) noexcept
{
    const auto denominator = orientation(
        triangle.vertices[0].horizontal,
        triangle.vertices[1].horizontal,
        triangle.vertices[2].horizontal);
    const auto first_weight = orientation(
        triangle.vertices[1].horizontal,
        triangle.vertices[2].horizontal,
        point) / denominator;
    const auto second_weight = orientation(
        triangle.vertices[2].horizontal,
        triangle.vertices[0].horizontal,
        point) / denominator;
    const auto third_weight = 1.0 - first_weight - second_weight;
    return first_weight * triangle.vertices[0].height +
        second_weight * triangle.vertices[1].height +
        third_weight * triangle.vertices[2].height;
}

[[nodiscard]] std::optional<SurfacePoint2>
projected_edge_intersection(
    const SurfacePoint2 first_start,
    const SurfacePoint2 first_end,
    const SurfacePoint2 second_start,
    const SurfacePoint2 second_end) noexcept
{
    const SurfacePoint2 first_direction{
        first_end.x - first_start.x,
        first_end.z - first_start.z,
    };
    const SurfacePoint2 second_direction{
        second_end.x - second_start.x,
        second_end.z - second_start.z,
    };
    const auto denominator =
        first_direction.x * second_direction.z -
        first_direction.z * second_direction.x;
    if (denominator == 0.0) {
        // Collinear overlap endpoints are already supplied by the vertex
        // containment candidates.
        return std::nullopt;
    }

    const SurfacePoint2 start_delta{
        second_start.x - first_start.x,
        second_start.z - first_start.z,
    };
    const auto first_parameter =
        (start_delta.x * second_direction.z -
         start_delta.z * second_direction.x) /
        denominator;
    const auto second_parameter =
        (start_delta.x * first_direction.z -
         start_delta.z * first_direction.x) /
        denominator;
    constexpr double endpoint_tolerance = 1.0e-10;
    if (first_parameter < -endpoint_tolerance ||
        first_parameter > 1.0 + endpoint_tolerance ||
        second_parameter < -endpoint_tolerance ||
        second_parameter > 1.0 + endpoint_tolerance) {
        return std::nullopt;
    }

    const auto clamped_parameter =
        std::clamp(first_parameter, 0.0, 1.0);
    return SurfacePoint2{
        first_start.x +
            first_direction.x * clamped_parameter,
        first_start.z +
            first_direction.z * clamped_parameter,
    };
}

void measure_overlay_candidate(
    const SurfaceTriangle& coarse,
    const SurfaceTriangle& lod0,
    const SurfacePoint2 point,
    double& maximum_error) noexcept
{
    if (!contains_projected_point(coarse, point) ||
        !contains_projected_point(lod0, point)) {
        return;
    }
    maximum_error = std::max(
        maximum_error,
        std::abs(
            surface_height(coarse, point) -
            surface_height(lod0, point)));
}

[[nodiscard]] double maximum_triangle_overlay_error(
    const SurfaceTriangle& coarse,
    const SurfaceTriangle& lod0) noexcept
{
    double maximum_error = 0.0;
    for (const auto& vertex : coarse.vertices) {
        measure_overlay_candidate(
            coarse,
            lod0,
            vertex.horizontal,
            maximum_error);
    }
    for (const auto& vertex : lod0.vertices) {
        measure_overlay_candidate(
            coarse,
            lod0,
            vertex.horizontal,
            maximum_error);
    }
    for (std::size_t coarse_edge = 0;
         coarse_edge < coarse.vertices.size();
         ++coarse_edge) {
        const auto coarse_next =
            (coarse_edge + 1U) % coarse.vertices.size();
        for (std::size_t lod0_edge = 0;
             lod0_edge < lod0.vertices.size();
             ++lod0_edge) {
            const auto lod0_next =
                (lod0_edge + 1U) % lod0.vertices.size();
            const auto intersection = projected_edge_intersection(
                coarse.vertices[coarse_edge].horizontal,
                coarse.vertices[coarse_next].horizontal,
                lod0.vertices[lod0_edge].horizontal,
                lod0.vertices[lod0_next].horizontal);
            if (intersection.has_value()) {
                measure_overlay_candidate(
                    coarse,
                    lod0,
                    *intersection,
                    maximum_error);
            }
        }
    }
    return maximum_error;
}

[[nodiscard]] std::array<SurfaceTriangle, 8>
patch_lod0_triangles(
    const HeightTile& tile,
    const std::uint32_t first_cell_x,
    const std::uint32_t first_cell_z) noexcept
{
    std::array<SurfaceTriangle, 8> triangles;
    std::size_t output_index = 0;
    constexpr std::array triangle_types{
        HeightTileTriangle::v00_v01_v11,
        HeightTileTriangle::v00_v11_v10,
    };
    for (std::uint32_t local_z = 0; local_z < 2U; ++local_z) {
        for (std::uint32_t local_x = 0;
             local_x < 2U;
             ++local_x) {
            for (const auto triangle_type : triangle_types) {
                const auto source_indices = fixed_triangle_indices(
                    first_cell_x + local_x,
                    first_cell_z + local_z,
                    tile.sample_columns,
                    triangle_type);
                triangles[output_index] = surface_triangle(
                    tile,
                    {{
                        static_cast<std::uint16_t>(
                            source_indices.vertices[0]),
                        static_cast<std::uint16_t>(
                            source_indices.vertices[1]),
                        static_cast<std::uint16_t>(
                            source_indices.vertices[2]),
                    }});
                ++output_index;
            }
        }
    }
    return triangles;
}

[[nodiscard]] constexpr std::uint16_t tile_vertex_index(
    const std::uint32_t x,
    const std::uint32_t z,
    const std::uint32_t columns) noexcept
{
    return static_cast<std::uint16_t>(
        sample_index(x, z, columns));
}

} // namespace

HeightTile make_deterministic_height_tile()
{
    HeightTile tile{
        .sample_columns = deterministic_tile_sample_columns,
        .sample_rows = deterministic_tile_sample_rows,
        .sample_spacing = deterministic_tile_sample_spacing,
        .origin = deterministic_tile_origin,
    };
    tile.height_offsets.reserve(deterministic_tile_vertex_count);
    for (std::uint32_t z = 0;
         z < deterministic_tile_sample_rows;
         ++z) {
        for (std::uint32_t x = 0;
             x < deterministic_tile_sample_columns;
             ++x) {
            tile.height_offsets.push_back(
                deterministic_height_offset(x, z));
        }
    }
    return tile;
}

HeightTile make_large_capacity_height_tile()
{
    HeightTile tile{
        .sample_columns = large_capacity_tile_sample_columns,
        .sample_rows = large_capacity_tile_sample_rows,
        .sample_spacing = large_capacity_tile_sample_spacing,
        .origin = large_capacity_tile_origin,
    };
    tile.height_offsets.reserve(large_capacity_tile_vertex_count);
    for (std::uint32_t z = 0;
         z < large_capacity_tile_sample_rows;
         ++z) {
        for (std::uint32_t x = 0;
             x < large_capacity_tile_sample_columns;
             ++x) {
            tile.height_offsets.push_back(
                large_rolling_height_offset(x, z));
        }
    }
    return tile;
}

HeightTileSurface::HeightTileSurface(
    HeightTile tile,
    const Bounds3 bounds)
    : tile_(std::move(tile)),
      bounds_(bounds)
{
}

core::Result<HeightTileSurface> HeightTileSurface::create(
    HeightTile tile)
{
    auto bounds_result = validate_height_tile(tile);
    if (!bounds_result) {
        return core::Result<HeightTileSurface>::failure(
            std::move(bounds_result).error());
    }
    const auto bounds = bounds_result.value();
    return core::Result<HeightTileSurface>::success(
        HeightTileSurface{std::move(tile), bounds});
}

const HeightTile& HeightTileSurface::tile() const noexcept
{
    return tile_;
}

const Bounds3& HeightTileSurface::bounds() const noexcept
{
    return bounds_;
}

std::optional<HeightTileSurfaceSample>
HeightTileSurface::sample_lod0_surface(
    const float world_x,
    const float world_z) const noexcept
{
    if (!std::isfinite(world_x) || !std::isfinite(world_z) ||
        world_x < bounds_.minimum.x ||
        world_x > bounds_.maximum.x ||
        world_z < bounds_.minimum.z ||
        world_z > bounds_.maximum.z) {
        return std::nullopt;
    }

    const auto x_coordinate = locate_cell_coordinate(
        tile_,
        world_x,
        tile_.sample_columns,
        HorizontalAxis::x);
    const auto z_coordinate = locate_cell_coordinate(
        tile_,
        world_z,
        tile_.sample_rows,
        HorizontalAxis::z);
    const auto cell_x = x_coordinate.cell;
    const auto cell_z = z_coordinate.cell;
    const auto u = x_coordinate.local;
    const auto v = z_coordinate.local;

    const auto triangle = u <= v
        ? HeightTileTriangle::v00_v01_v11
        : HeightTileTriangle::v00_v11_v10;
    const std::array<double, 3> precise_barycentrics =
        triangle == HeightTileTriangle::v00_v01_v11
        ? std::array<double, 3>{{1.0 - v, v - u, u}}
        : std::array<double, 3>{{1.0 - u, v, u - v}};
    const math::Float3 barycentrics{
        static_cast<float>(precise_barycentrics[0]),
        static_cast<float>(precise_barycentrics[1]),
        static_cast<float>(precise_barycentrics[2]),
    };
    const auto positions = fixed_triangle_positions(
        tile_,
        cell_x,
        cell_z,
        triangle);
    const auto world_y = static_cast<float>(
        precise_barycentrics[0] *
            static_cast<double>(positions[0].y) +
        precise_barycentrics[1] *
            static_cast<double>(positions[1].y) +
        precise_barycentrics[2] *
            static_cast<double>(positions[2].y));
    return HeightTileSurfaceSample{
        .position = {world_x, world_y, world_z},
        .normal = triangle_normal(
            tile_,
            cell_x,
            cell_z,
            triangle),
        .cell_x = cell_x,
        .cell_z = cell_z,
        .triangle = triangle,
        .barycentrics = barycentrics,
    };
}

std::optional<float> HeightTileSurface::sample_lod0_height(
    const float world_x,
    const float world_z) const noexcept
{
    const auto sample = sample_lod0_surface(world_x, world_z);
    if (!sample.has_value()) {
        return std::nullopt;
    }
    return sample->position.y;
}

std::optional<math::Float3> HeightTileSurface::sample_lod0_normal(
    const float world_x,
    const float world_z) const noexcept
{
    const auto sample = sample_lod0_surface(world_x, world_z);
    if (!sample.has_value()) {
        return std::nullopt;
    }
    return sample->normal;
}

core::Result<std::optional<BoundsInterval>>
HeightTileSurface::intersect_bounds(
    const Ray3& ray,
    const float maximum_distance) const
{
    auto normalized_result = normalize_ray(ray, maximum_distance);
    if (!normalized_result) {
        return core::Result<std::optional<BoundsInterval>>::failure(
            std::move(normalized_result).error());
    }
    return core::Result<std::optional<BoundsInterval>>::success(
        intersect_bounds_normalized(
            bounds_,
            normalized_result.value(),
            maximum_distance));
}

core::Result<std::optional<HeightTileRayHit>>
HeightTileSurface::raycast_lod0(
    const Ray3& ray,
    const float maximum_distance) const
{
    auto normalized_result = normalize_ray(ray, maximum_distance);
    if (!normalized_result) {
        return core::Result<
            std::optional<HeightTileRayHit>>::failure(
                std::move(normalized_result).error());
    }
    const auto& normalized_ray = normalized_result.value();
    const auto bounds_interval = intersect_bounds_normalized(
        bounds_,
        normalized_ray,
        maximum_distance);
    if (!bounds_interval.has_value()) {
        return core::Result<
            std::optional<HeightTileRayHit>>::success(std::nullopt);
    }

    auto nearest_distance = static_cast<double>(maximum_distance);
    bool found_hit = false;
    constexpr std::array triangles{
        HeightTileTriangle::v00_v01_v11,
        HeightTileTriangle::v00_v11_v10,
    };
    for (std::uint32_t cell_z = 0;
         cell_z + 1U < tile_.sample_rows;
         ++cell_z) {
        for (std::uint32_t cell_x = 0;
             cell_x + 1U < tile_.sample_columns;
             ++cell_x) {
            for (const auto triangle : triangles) {
                const auto positions = fixed_triangle_positions(
                    tile_,
                    cell_x,
                    cell_z,
                    triangle);
                const auto hit = intersect_triangle(
                    normalized_ray,
                    positions,
                    nearest_distance);
                if (!hit.has_value() ||
                    (found_hit &&
                     hit->distance >= nearest_distance)) {
                    continue;
                }
                nearest_distance = hit->distance;
                found_hit = true;
            }
        }
    }
    if (!found_hit) {
        return core::Result<
            std::optional<HeightTileRayHit>>::success(std::nullopt);
    }

    const auto hit_distance = static_cast<float>(nearest_distance);
    const math::Float3 hit_position{
        static_cast<float>(
            normalized_ray.origin[0] +
            normalized_ray.direction[0] *
                static_cast<double>(hit_distance)),
        static_cast<float>(
            normalized_ray.origin[1] +
            normalized_ray.direction[1] *
                static_cast<double>(hit_distance)),
        static_cast<float>(
            normalized_ray.origin[2] +
            normalized_ray.direction[2] *
                static_cast<double>(hit_distance)),
    };
    const auto ownership_x = std::clamp(
        static_cast<float>(
            normalized_ray.origin[0] +
            normalized_ray.direction[0] * nearest_distance),
        bounds_.minimum.x,
        bounds_.maximum.x);
    const auto ownership_z = std::clamp(
        static_cast<float>(
            normalized_ray.origin[2] +
            normalized_ray.direction[2] * nearest_distance),
        bounds_.minimum.z,
        bounds_.maximum.z);
    const auto sample = sample_lod0_surface(
        ownership_x,
        ownership_z);
    if (!sample.has_value()) {
        return core::Result<
            std::optional<HeightTileRayHit>>::failure(terrain_error(
                core::ErrorCode::invalid_state,
                "A terrain triangle hit could not be canonicalized"));
    }
    return core::Result<
        std::optional<HeightTileRayHit>>::success(HeightTileRayHit{
            .distance = hit_distance,
            .position = hit_position,
            .normal = sample->normal,
            .cell_x = sample->cell_x,
            .cell_z = sample->cell_z,
            .triangle = sample->triangle,
            .barycentrics = sample->barycentrics,
        });
}

core::Result<std::optional<HeightTileSegmentClosestPoint>>
HeightTileSurface::closest_lod0_point_to_segment(
    const Segment3& segment,
    const float maximum_distance) const
{
    if (!math::is_finite(segment.first_endpoint) ||
        !math::is_finite(segment.second_endpoint) ||
        !std::isfinite(maximum_distance) ||
        maximum_distance <= 0.0F) {
        return core::Result<
            std::optional<HeightTileSegmentClosestPoint>>::failure(
                terrain_error(
                    core::ErrorCode::invalid_argument,
                    "Terrain segment queries require finite endpoints "
                    "and a finite positive maximum distance"));
    }

    const auto first = precise_point(segment.first_endpoint);
    const auto second = precise_point(segment.second_endpoint);
    const auto expansion = static_cast<double>(maximum_distance);
    const std::array<double, 3> query_minimum{{
        std::min(first[0], second[0]) - expansion,
        std::min(first[1], second[1]) - expansion,
        std::min(first[2], second[2]) - expansion,
    }};
    const std::array<double, 3> query_maximum{{
        std::max(first[0], second[0]) + expansion,
        std::max(first[1], second[1]) + expansion,
        std::max(first[2], second[2]) + expansion,
    }};
    if (query_maximum[0] <
            static_cast<double>(bounds_.minimum.x) ||
        query_minimum[0] >
            static_cast<double>(bounds_.maximum.x) ||
        query_maximum[1] <
            static_cast<double>(bounds_.minimum.y) ||
        query_minimum[1] >
            static_cast<double>(bounds_.maximum.y) ||
        query_maximum[2] <
            static_cast<double>(bounds_.minimum.z) ||
        query_minimum[2] >
            static_cast<double>(bounds_.maximum.z)) {
        return core::Result<
            std::optional<HeightTileSegmentClosestPoint>>::success(
                std::nullopt);
    }

    const auto clipped_minimum_x = static_cast<float>(std::clamp(
        query_minimum[0],
        static_cast<double>(bounds_.minimum.x),
        static_cast<double>(bounds_.maximum.x)));
    const auto clipped_maximum_x = static_cast<float>(std::clamp(
        query_maximum[0],
        static_cast<double>(bounds_.minimum.x),
        static_cast<double>(bounds_.maximum.x)));
    const auto clipped_minimum_z = static_cast<float>(std::clamp(
        query_minimum[2],
        static_cast<double>(bounds_.minimum.z),
        static_cast<double>(bounds_.maximum.z)));
    const auto clipped_maximum_z = static_cast<float>(std::clamp(
        query_maximum[2],
        static_cast<double>(bounds_.minimum.z),
        static_cast<double>(bounds_.maximum.z)));
    auto first_cell_x = locate_cell_coordinate(
        tile_,
        clipped_minimum_x,
        tile_.sample_columns,
        HorizontalAxis::x).cell;
    auto last_cell_x = locate_cell_coordinate(
        tile_,
        clipped_maximum_x,
        tile_.sample_columns,
        HorizontalAxis::x).cell;
    auto first_cell_z = locate_cell_coordinate(
        tile_,
        clipped_minimum_z,
        tile_.sample_rows,
        HorizontalAxis::z).cell;
    auto last_cell_z = locate_cell_coordinate(
        tile_,
        clipped_maximum_z,
        tile_.sample_rows,
        HorizontalAxis::z).cell;

    // Include one conservative neighboring cell on every available side.
    // Exact grid-line features are shared, and stable row-major traversal
    // decides equal-distance ownership.
    first_cell_x = first_cell_x > 0U
        ? first_cell_x - 1U
        : 0U;
    first_cell_z = first_cell_z > 0U
        ? first_cell_z - 1U
        : 0U;
    last_cell_x = std::min(
        last_cell_x + 1U,
        tile_.sample_columns - 2U);
    last_cell_z = std::min(
        last_cell_z + 1U,
        tile_.sample_rows - 2U);

    SegmentTriangleClosestPoint closest;
    std::uint32_t closest_cell_x{};
    std::uint32_t closest_cell_z{};
    HeightTileTriangle closest_triangle{
        HeightTileTriangle::v00_v01_v11};
    bool found_candidate = false;
    constexpr std::array triangles{
        HeightTileTriangle::v00_v01_v11,
        HeightTileTriangle::v00_v11_v10,
    };
    for (std::uint32_t cell_z = first_cell_z;
         cell_z <= last_cell_z;
         ++cell_z) {
        for (std::uint32_t cell_x = first_cell_x;
             cell_x <= last_cell_x;
             ++cell_x) {
            for (const auto triangle : triangles) {
                const auto positions = fixed_triangle_positions(
                    tile_,
                    cell_x,
                    cell_z,
                    triangle);
                const std::array<PrecisePoint, 3>
                    precise_positions{{
                        precise_point(positions[0]),
                        precise_point(positions[1]),
                        precise_point(positions[2]),
                    }};
                const auto candidate =
                    closest_segment_triangle_points(
                        first,
                        second,
                        precise_positions);
                if (!std::isfinite(candidate.distance_squared)) {
                    return core::Result<std::optional<
                        HeightTileSegmentClosestPoint>>::failure(
                            terrain_error(
                                core::ErrorCode::unavailable,
                                "Terrain segment closest-feature math "
                                "exceeded finite range"));
                }
                if (found_candidate &&
                    candidate.distance_squared >=
                        closest.distance_squared) {
                    continue;
                }
                closest = candidate;
                closest_cell_x = cell_x;
                closest_cell_z = cell_z;
                closest_triangle = triangle;
                found_candidate = true;
            }
        }
    }

    const auto maximum_distance_squared =
        expansion * expansion;
    if (!found_candidate ||
        closest.distance_squared > maximum_distance_squared) {
        return core::Result<
            std::optional<HeightTileSegmentClosestPoint>>::success(
                std::nullopt);
    }

    const auto distance = std::sqrt(closest.distance_squared);
    if (!representable_float(distance) ||
        !representable_float(closest.segment_parameter) ||
        !representable_point(closest.segment_position) ||
        !representable_point(closest.triangle_position) ||
        !representable_point(closest.triangle_barycentrics)) {
        return core::Result<
            std::optional<HeightTileSegmentClosestPoint>>::failure(
                terrain_error(
                    core::ErrorCode::unavailable,
                    "Terrain segment closest feature exceeded finite "
                    "float range"));
    }

    return core::Result<
        std::optional<HeightTileSegmentClosestPoint>>::success(
            HeightTileSegmentClosestPoint{
                .segment_position =
                    float3(closest.segment_position),
                .segment_parameter = static_cast<float>(
                    closest.segment_parameter),
                .surface = HeightTileSurfaceSample{
                    .position =
                        float3(closest.triangle_position),
                    .normal = triangle_normal(
                        tile_,
                        closest_cell_x,
                        closest_cell_z,
                        closest_triangle),
                    .cell_x = closest_cell_x,
                    .cell_z = closest_cell_z,
                    .triangle = closest_triangle,
                    .barycentrics =
                        float3(closest.triangle_barycentrics),
                },
                .distance = static_cast<float>(distance),
            });
}

core::Result<HeightTileMesh> build_lod0_mesh(
    const HeightTile& tile)
{
    auto bounds_result = validate_height_tile(tile);
    if (!bounds_result) {
        return core::Result<HeightTileMesh>::failure(
            std::move(bounds_result).error());
    }

    const auto columns =
        static_cast<std::size_t>(tile.sample_columns);
    const auto rows = static_cast<std::size_t>(tile.sample_rows);
    const auto vertex_count = columns * rows;
    constexpr auto maximum_vertex_count =
        static_cast<std::size_t>(
            std::numeric_limits<std::uint16_t>::max()) +
        1U;
    if (vertex_count > maximum_vertex_count) {
        return core::Result<HeightTileMesh>::failure(terrain_error(
            core::ErrorCode::unsupported,
            "Height-tile LOD0 exceeds the uint16 index contract"));
    }

    HeightTileMesh mesh;
    mesh.positions.reserve(vertex_count);
    mesh.normals.resize(vertex_count);

    for (std::uint32_t z = 0; z < tile.sample_rows; ++z) {
        for (std::uint32_t x = 0; x < tile.sample_columns; ++x) {
            mesh.positions.push_back(sample_position(tile, x, z));
        }
    }

    const auto cell_columns = columns - 1U;
    const auto cell_rows = rows - 1U;
    const auto cell_count = cell_columns * cell_rows;
    mesh.indices.reserve(cell_count * 6U);
    for (std::uint32_t z = 0; z + 1U < tile.sample_rows; ++z) {
        for (std::uint32_t x = 0;
             x + 1U < tile.sample_columns;
             ++x) {
            constexpr std::array triangles{
                HeightTileTriangle::v00_v01_v11,
                HeightTileTriangle::v00_v11_v10,
            };
            for (const auto triangle : triangles) {
                const auto indices = fixed_triangle_indices(
                    x,
                    z,
                    tile.sample_columns,
                    triangle);
                for (const auto index : indices.vertices) {
                    mesh.indices.push_back(
                        static_cast<std::uint16_t>(index));
                }
            }
        }
    }

    for (std::size_t triangle = 0;
         triangle < mesh.indices.size();
         triangle += 3U) {
        const auto first = mesh.indices[triangle];
        const auto second = mesh.indices[triangle + 1U];
        const auto third = mesh.indices[triangle + 2U];
        const auto first_edge = subtract(
            mesh.positions[second],
            mesh.positions[first]);
        const auto second_edge = subtract(
            mesh.positions[third],
            mesh.positions[first]);
        const auto face_normal = cross(first_edge, second_edge);
        const auto face_length_squared = length_squared(face_normal);
        if (!std::isfinite(face_length_squared) ||
            face_length_squared <= 0.0F ||
            face_normal.y <= 0.0F) {
            return core::Result<HeightTileMesh>::failure(terrain_error(
                core::ErrorCode::invalid_state,
                "Height-tile triangulation produced invalid winding"));
        }
        add_assign(mesh.normals[first], face_normal);
        add_assign(mesh.normals[second], face_normal);
        add_assign(mesh.normals[third], face_normal);
    }

    for (auto& normal : mesh.normals) {
        const auto normal_length_squared = length_squared(normal);
        if (!std::isfinite(normal_length_squared) ||
            normal_length_squared <= 0.0F) {
            return core::Result<HeightTileMesh>::failure(terrain_error(
                core::ErrorCode::invalid_state,
                "Height-tile render normals cannot be normalized"));
        }
        const auto inverse_length =
            1.0F / std::sqrt(normal_length_squared);
        normal.x *= inverse_length;
        normal.y *= inverse_length;
        normal.z *= inverse_length;
        if (!math::is_finite(normal) || normal.y <= 0.0F) {
            return core::Result<HeightTileMesh>::failure(terrain_error(
                core::ErrorCode::invalid_state,
                "Height-tile render normals are invalid"));
        }
    }

    mesh.bounds = bounds_result.value();
    mesh.bounds_lines = make_bounds_lines(mesh.bounds);
    return core::Result<HeightTileMesh>::success(std::move(mesh));
}

core::Result<HeightTileChunkLayout> build_lod0_chunk_layout(
    const HeightTile& tile,
    const std::uint32_t chunk_cell_columns,
    const std::uint32_t chunk_cell_rows)
{
    if (chunk_cell_columns == 0U || chunk_cell_rows == 0U) {
        return core::Result<HeightTileChunkLayout>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "Terrain chunk cell dimensions must be positive"));
    }

    auto bounds_result = validate_height_tile(tile);
    if (!bounds_result) {
        return core::Result<HeightTileChunkLayout>::failure(
            std::move(bounds_result).error());
    }

    const auto columns = static_cast<std::size_t>(tile.sample_columns);
    const auto rows = static_cast<std::size_t>(tile.sample_rows);
    const auto vertex_count = columns * rows;
    constexpr auto maximum_vertex_count =
        static_cast<std::size_t>(
            std::numeric_limits<std::uint16_t>::max()) +
        1U;
    if (vertex_count > maximum_vertex_count) {
        return core::Result<HeightTileChunkLayout>::failure(terrain_error(
            core::ErrorCode::unsupported,
            "Height-tile chunk layout exceeds the uint16 index contract"));
    }

    const auto cell_columns = tile.sample_columns - 1U;
    const auto cell_rows = tile.sample_rows - 1U;
    const auto chunk_columns =
        1U + (cell_columns - 1U) / chunk_cell_columns;
    const auto chunk_rows =
        1U + (cell_rows - 1U) / chunk_cell_rows;
    const auto cell_count =
        static_cast<std::size_t>(cell_columns) *
        static_cast<std::size_t>(cell_rows);

    HeightTileChunkLayout layout;
    layout.indices.reserve(cell_count * 6U);
    layout.chunks.reserve(
        static_cast<std::size_t>(chunk_columns) * chunk_rows);

    for (std::uint32_t first_cell_z = 0;
         first_cell_z < cell_rows;) {
        const auto current_cell_rows = std::min(
            chunk_cell_rows,
            cell_rows - first_cell_z);
        for (std::uint32_t first_cell_x = 0;
             first_cell_x < cell_columns;) {
            const auto current_cell_columns = std::min(
                chunk_cell_columns,
                cell_columns - first_cell_x);
            const auto first_index = layout.indices.size();

            for (std::uint32_t local_cell_z = 0;
                 local_cell_z < current_cell_rows;
                 ++local_cell_z) {
                const auto cell_z = first_cell_z + local_cell_z;
                for (std::uint32_t local_cell_x = 0;
                     local_cell_x < current_cell_columns;
                     ++local_cell_x) {
                    const auto cell_x = first_cell_x + local_cell_x;
                    constexpr std::array triangles{
                        HeightTileTriangle::v00_v01_v11,
                        HeightTileTriangle::v00_v11_v10,
                    };
                    for (const auto triangle : triangles) {
                        const auto triangle_indices =
                            fixed_triangle_indices(
                                cell_x,
                                cell_z,
                                tile.sample_columns,
                                triangle);
                        for (const auto index :
                             triangle_indices.vertices) {
                            layout.indices.push_back(
                                static_cast<std::uint16_t>(index));
                        }
                    }
                }
            }

            const auto maximum_float =
                std::numeric_limits<float>::max();
            Bounds3 chunk_bounds{
                {maximum_float, maximum_float, maximum_float},
                {-maximum_float, -maximum_float, -maximum_float},
            };
            for (std::uint32_t local_sample_z = 0;
                 local_sample_z <= current_cell_rows;
                 ++local_sample_z) {
                const auto sample_z = first_cell_z + local_sample_z;
                for (std::uint32_t local_sample_x = 0;
                     local_sample_x <= current_cell_columns;
                     ++local_sample_x) {
                    const auto sample_x =
                        first_cell_x + local_sample_x;
                    const auto position = sample_position(
                        tile,
                        sample_x,
                        sample_z);
                    chunk_bounds.minimum.x = std::min(
                        chunk_bounds.minimum.x,
                        position.x);
                    chunk_bounds.minimum.y = std::min(
                        chunk_bounds.minimum.y,
                        position.y);
                    chunk_bounds.minimum.z = std::min(
                        chunk_bounds.minimum.z,
                        position.z);
                    chunk_bounds.maximum.x = std::max(
                        chunk_bounds.maximum.x,
                        position.x);
                    chunk_bounds.maximum.y = std::max(
                        chunk_bounds.maximum.y,
                        position.y);
                    chunk_bounds.maximum.z = std::max(
                        chunk_bounds.maximum.z,
                        position.z);
                }
            }

            layout.chunks.push_back(HeightTileChunk{
                .first_cell_x = first_cell_x,
                .first_cell_z = first_cell_z,
                .cell_columns = current_cell_columns,
                .cell_rows = current_cell_rows,
                .first_index = first_index,
                .index_count = layout.indices.size() - first_index,
                .bounds = chunk_bounds,
                .bounds_lines = make_bounds_lines(chunk_bounds),
            });
            first_cell_x += current_cell_columns;
        }
        first_cell_z += current_cell_rows;
    }

    return core::Result<HeightTileChunkLayout>::success(
        std::move(layout));
}

core::Result<HeightTileCoarseChunkLayout>
build_boundary_preserving_coarse_chunk_layout(
    const HeightTile& tile,
    const std::uint32_t chunk_cell_columns,
    const std::uint32_t chunk_cell_rows)
{
    auto lod0_result = build_lod0_chunk_layout(
        tile,
        chunk_cell_columns,
        chunk_cell_rows);
    if (!lod0_result) {
        return core::Result<
            HeightTileCoarseChunkLayout>::failure(
                std::move(lod0_result).error());
    }
    const auto& lod0 = lod0_result.value();

    HeightTileCoarseChunkLayout coarse;
    coarse.indices.reserve(lod0.indices.size());
    coarse.chunks.reserve(lod0.chunks.size());

    for (const auto& lod0_chunk : lod0.chunks) {
        const auto first_index = coarse.indices.size();
        double chunk_maximum_error = 0.0;
        const auto complete_even_chunk =
            lod0_chunk.cell_columns == chunk_cell_columns &&
            lod0_chunk.cell_rows == chunk_cell_rows &&
            lod0_chunk.cell_columns % 2U == 0U &&
            lod0_chunk.cell_rows % 2U == 0U;
        if (!complete_even_chunk) {
            const auto range_end =
                lod0_chunk.first_index + lod0_chunk.index_count;
            for (auto index = lod0_chunk.first_index;
                 index < range_end;
                 ++index) {
                coarse.indices.push_back(lod0.indices[index]);
            }
        }
        else {
            for (std::uint32_t local_cell_z = 0;
                 local_cell_z < lod0_chunk.cell_rows;
                 local_cell_z += 2U) {
                for (std::uint32_t local_cell_x = 0;
                     local_cell_x < lod0_chunk.cell_columns;
                     local_cell_x += 2U) {
                    const auto first_cell_x =
                        lod0_chunk.first_cell_x + local_cell_x;
                    const auto first_cell_z =
                        lod0_chunk.first_cell_z + local_cell_z;
                    const auto patch_lod0 = patch_lod0_triangles(
                        tile,
                        first_cell_x,
                        first_cell_z);
                    const auto v00 = tile_vertex_index(
                        first_cell_x,
                        first_cell_z,
                        tile.sample_columns);
                    const auto v20 = tile_vertex_index(
                        first_cell_x + 2U,
                        first_cell_z,
                        tile.sample_columns);
                    const auto v22 = tile_vertex_index(
                        first_cell_x + 2U,
                        first_cell_z + 2U,
                        tile.sample_columns);
                    const auto v02 = tile_vertex_index(
                        first_cell_x,
                        first_cell_z + 2U,
                        tile.sample_columns);
                    const auto center = tile_vertex_index(
                        first_cell_x + 1U,
                        first_cell_z + 1U,
                        tile.sample_columns);
                    const auto top_midpoint = tile_vertex_index(
                        first_cell_x + 1U,
                        first_cell_z,
                        tile.sample_columns);
                    const auto right_midpoint = tile_vertex_index(
                        first_cell_x + 2U,
                        first_cell_z + 1U,
                        tile.sample_columns);
                    const auto bottom_midpoint = tile_vertex_index(
                        first_cell_x + 1U,
                        first_cell_z + 2U,
                        tile.sample_columns);
                    const auto left_midpoint = tile_vertex_index(
                        first_cell_x,
                        first_cell_z + 1U,
                        tile.sample_columns);

                    const auto emit_triangle =
                        [&](const std::uint16_t first,
                            const std::uint16_t second,
                            const std::uint16_t third) {
                            const std::array indices{
                                first,
                                second,
                                third,
                            };
                            coarse.indices.insert(
                                coarse.indices.end(),
                                indices.begin(),
                                indices.end());
                            const auto coarse_triangle =
                                surface_triangle(tile, indices);
                            for (const auto& lod0_triangle :
                                 patch_lod0) {
                                chunk_maximum_error = std::max(
                                    chunk_maximum_error,
                                    maximum_triangle_overlay_error(
                                        coarse_triangle,
                                        lod0_triangle));
                            }
                        };

                    if (local_cell_z == 0U) {
                        emit_triangle(v00, center, top_midpoint);
                        emit_triangle(top_midpoint, center, v20);
                    }
                    else {
                        emit_triangle(v00, center, v20);
                    }

                    if (local_cell_x + 2U ==
                        lod0_chunk.cell_columns) {
                        emit_triangle(v20, center, right_midpoint);
                        emit_triangle(right_midpoint, center, v22);
                    }
                    else {
                        emit_triangle(v20, center, v22);
                    }

                    if (local_cell_z + 2U ==
                        lod0_chunk.cell_rows) {
                        emit_triangle(v22, center, bottom_midpoint);
                        emit_triangle(bottom_midpoint, center, v02);
                    }
                    else {
                        emit_triangle(v22, center, v02);
                    }

                    if (local_cell_x == 0U) {
                        emit_triangle(v02, center, left_midpoint);
                        emit_triangle(left_midpoint, center, v00);
                    }
                    else {
                        emit_triangle(v02, center, v00);
                    }
                }
            }
        }

        coarse.chunks.push_back(HeightTileCoarseChunk{
            .first_index = first_index,
            .index_count = coarse.indices.size() - first_index,
            .maximum_geometric_error = chunk_maximum_error,
        });
        coarse.maximum_geometric_error = std::max(
            coarse.maximum_geometric_error,
            chunk_maximum_error);
    }

    return core::Result<HeightTileCoarseChunkLayout>::success(
        std::move(coarse));
}

} // namespace shark::terrain
