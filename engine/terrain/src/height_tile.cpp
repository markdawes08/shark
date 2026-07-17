#include <shark/terrain/height_tile.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
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

core::Result<HeightTileMesh> build_lod0_mesh(
    const HeightTile& tile)
{
    if (tile.sample_columns < 2U || tile.sample_rows < 2U) {
        return core::Result<HeightTileMesh>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "A height tile requires at least two samples on each axis"));
    }
    if (!std::isfinite(tile.sample_spacing) ||
        tile.sample_spacing <= 0.0F) {
        return core::Result<HeightTileMesh>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "Height-tile sample spacing must be finite and positive"));
    }
    if (!math::is_finite(tile.origin)) {
        return core::Result<HeightTileMesh>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "Height-tile origin coordinates must be finite"));
    }

    const auto columns =
        static_cast<std::size_t>(tile.sample_columns);
    const auto rows = static_cast<std::size_t>(tile.sample_rows);
    if (columns >
        std::numeric_limits<std::size_t>::max() / rows) {
        return core::Result<HeightTileMesh>::failure(terrain_error(
            core::ErrorCode::unavailable,
            "Height-tile sample count overflows addressable storage"));
    }
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
    if (tile.height_offsets.size() != vertex_count) {
        return core::Result<HeightTileMesh>::failure(terrain_error(
            core::ErrorCode::invalid_argument,
            "Height-tile storage does not match its sample dimensions"));
    }

    HeightTileMesh mesh;
    mesh.positions.reserve(vertex_count);
    mesh.normals.resize(vertex_count);

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
                return core::Result<HeightTileMesh>::failure(
                    terrain_error(
                        core::ErrorCode::invalid_argument,
                        "Height-tile samples must all be finite"));
            }

            const math::Float3 position{
                tile.origin.x +
                    static_cast<float>(x) * tile.sample_spacing,
                tile.origin.y + height,
                tile.origin.z +
                    static_cast<float>(z) * tile.sample_spacing,
            };
            if (!math::is_finite(position)) {
                return core::Result<HeightTileMesh>::failure(
                    terrain_error(
                        core::ErrorCode::invalid_argument,
                        "Height-tile coordinates exceed finite float "
                        "storage"));
            }
            mesh.positions.push_back(position);
            bounds.minimum.x = std::min(
                bounds.minimum.x,
                position.x);
            bounds.minimum.y = std::min(
                bounds.minimum.y,
                position.y);
            bounds.minimum.z = std::min(
                bounds.minimum.z,
                position.z);
            bounds.maximum.x = std::max(
                bounds.maximum.x,
                position.x);
            bounds.maximum.y = std::max(
                bounds.maximum.y,
                position.y);
            bounds.maximum.z = std::max(
                bounds.maximum.z,
                position.z);
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
            const auto v00 = sample_index(
                x,
                z,
                tile.sample_columns);
            const auto v10 = sample_index(
                x + 1U,
                z,
                tile.sample_columns);
            const auto v01 = sample_index(
                x,
                z + 1U,
                tile.sample_columns);
            const auto v11 = sample_index(
                x + 1U,
                z + 1U,
                tile.sample_columns);
            mesh.indices.push_back(
                static_cast<std::uint16_t>(v00));
            mesh.indices.push_back(
                static_cast<std::uint16_t>(v01));
            mesh.indices.push_back(
                static_cast<std::uint16_t>(v11));
            mesh.indices.push_back(
                static_cast<std::uint16_t>(v00));
            mesh.indices.push_back(
                static_cast<std::uint16_t>(v11));
            mesh.indices.push_back(
                static_cast<std::uint16_t>(v10));
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

    mesh.bounds = bounds;
    mesh.bounds_lines = make_bounds_lines(bounds);
    return core::Result<HeightTileMesh>::success(std::move(mesh));
}

} // namespace shark::terrain
