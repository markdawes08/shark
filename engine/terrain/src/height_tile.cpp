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

} // namespace shark::terrain
