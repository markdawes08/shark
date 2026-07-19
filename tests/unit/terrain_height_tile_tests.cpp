#include <shark/terrain/height_tile.hpp>

#include <shark/core/error.hpp>
#include <shark/core/math.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] constexpr std::size_t sample_index(
    const std::uint32_t x,
    const std::uint32_t z,
    const std::uint32_t columns) noexcept
{
    return static_cast<std::size_t>(z) *
        static_cast<std::size_t>(columns) +
        static_cast<std::size_t>(x);
}

[[nodiscard]] constexpr shark::math::Float3 subtract(
    const shark::math::Float3 left,
    const shark::math::Float3 right) noexcept
{
    return shark::math::Float3{
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

[[nodiscard]] constexpr shark::math::Float3 cross(
    const shark::math::Float3 left,
    const shark::math::Float3 right) noexcept
{
    return shark::math::Float3{
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] constexpr float length_squared(
    const shark::math::Float3 value) noexcept
{
    return value.x * value.x +
        value.y * value.y +
        value.z * value.z;
}

constexpr void add_assign(
    shark::math::Float3& destination,
    const shark::math::Float3 value) noexcept
{
    destination.x += value.x;
    destination.y += value.y;
    destination.z += value.z;
}

[[nodiscard]] shark::terrain::HeightTile make_flat_tile(
    const std::uint32_t columns,
    const std::uint32_t rows)
{
    return shark::terrain::HeightTile{
        .sample_columns = columns,
        .sample_rows = rows,
        .sample_spacing = 1.0F,
        .origin = {},
        .height_offsets = std::vector<float>(
            static_cast<std::size_t>(columns) *
                static_cast<std::size_t>(rows),
            0.0F),
    };
}

[[nodiscard]] shark::math::Float3 tile_sample_position(
    const shark::terrain::HeightTile& tile,
    const std::uint32_t x,
    const std::uint32_t z)
{
    return {
        tile.origin.x +
            static_cast<float>(x) * tile.sample_spacing,
        tile.origin.y +
            tile.height_offsets[sample_index(
                x,
                z,
                tile.sample_columns)],
        tile.origin.z +
            static_cast<float>(z) * tile.sample_spacing,
    };
}

[[nodiscard]] shark::terrain::Bounds3 expected_chunk_bounds(
    const shark::terrain::HeightTile& tile,
    const std::uint32_t first_cell_x,
    const std::uint32_t first_cell_z,
    const std::uint32_t cell_columns,
    const std::uint32_t cell_rows)
{
    auto result = shark::terrain::Bounds3{
        tile_sample_position(tile, first_cell_x, first_cell_z),
        tile_sample_position(tile, first_cell_x, first_cell_z),
    };
    for (std::uint32_t local_z = 0;
         local_z <= cell_rows;
         ++local_z) {
        for (std::uint32_t local_x = 0;
             local_x <= cell_columns;
             ++local_x) {
            const auto position = tile_sample_position(
                tile,
                first_cell_x + local_x,
                first_cell_z + local_z);
            result.minimum.x = std::min(
                result.minimum.x,
                position.x);
            result.minimum.y = std::min(
                result.minimum.y,
                position.y);
            result.minimum.z = std::min(
                result.minimum.z,
                position.z);
            result.maximum.x = std::max(
                result.maximum.x,
                position.x);
            result.maximum.y = std::max(
                result.maximum.y,
                position.y);
            result.maximum.z = std::max(
                result.maximum.z,
                position.z);
        }
    }
    return result;
}

[[nodiscard]] std::array<std::uint16_t, 6> expected_cell_indices(
    const std::uint32_t cell_x,
    const std::uint32_t cell_z,
    const std::uint32_t sample_columns)
{
    const auto v00 = sample_index(cell_x, cell_z, sample_columns);
    const auto v10 = sample_index(
        cell_x + 1U,
        cell_z,
        sample_columns);
    const auto v01 = sample_index(
        cell_x,
        cell_z + 1U,
        sample_columns);
    const auto v11 = sample_index(
        cell_x + 1U,
        cell_z + 1U,
        sample_columns);
    return {
        static_cast<std::uint16_t>(v00),
        static_cast<std::uint16_t>(v01),
        static_cast<std::uint16_t>(v11),
        static_cast<std::uint16_t>(v00),
        static_cast<std::uint16_t>(v11),
        static_cast<std::uint16_t>(v10),
    };
}

inline constexpr std::array<std::uint16_t, 24>
    expected_bounds_line_indices{{
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
    }};

[[nodiscard]] std::array<shark::math::Float3, 8>
expected_bounds_line_positions(
    const shark::terrain::Bounds3 bounds)
{
    const auto& minimum = bounds.minimum;
    const auto& maximum = bounds.maximum;
    return {{
        {minimum.x, minimum.y, minimum.z},
        {maximum.x, minimum.y, minimum.z},
        {maximum.x, minimum.y, maximum.z},
        {minimum.x, minimum.y, maximum.z},
        {minimum.x, maximum.y, minimum.z},
        {maximum.x, maximum.y, minimum.z},
        {maximum.x, maximum.y, maximum.z},
        {minimum.x, maximum.y, maximum.z},
    }};
}

using TerrainEdge = std::pair<std::uint16_t, std::uint16_t>;

[[nodiscard]] constexpr TerrainEdge normalized_edge(
    const std::uint16_t first,
    const std::uint16_t second) noexcept
{
    return {
        std::min(first, second),
        std::max(first, second),
    };
}

[[nodiscard]] std::vector<TerrainEdge> expected_chunk_boundary_edges(
    const shark::terrain::HeightTile& tile,
    const shark::terrain::HeightTileChunk& chunk)
{
    std::vector<TerrainEdge> result;
    result.reserve(
        static_cast<std::size_t>(
            chunk.cell_columns + chunk.cell_rows) *
        2U);
    const auto first_x = chunk.first_cell_x;
    const auto final_x = first_x + chunk.cell_columns;
    const auto first_z = chunk.first_cell_z;
    const auto final_z = first_z + chunk.cell_rows;
    for (std::uint32_t x = first_x; x < final_x; ++x) {
        result.push_back(normalized_edge(
            static_cast<std::uint16_t>(
                sample_index(x, first_z, tile.sample_columns)),
            static_cast<std::uint16_t>(
                sample_index(x + 1U, first_z, tile.sample_columns))));
        result.push_back(normalized_edge(
            static_cast<std::uint16_t>(
                sample_index(x, final_z, tile.sample_columns)),
            static_cast<std::uint16_t>(
                sample_index(x + 1U, final_z, tile.sample_columns))));
    }
    for (std::uint32_t z = first_z; z < final_z; ++z) {
        result.push_back(normalized_edge(
            static_cast<std::uint16_t>(
                sample_index(first_x, z, tile.sample_columns)),
            static_cast<std::uint16_t>(
                sample_index(first_x, z + 1U, tile.sample_columns))));
        result.push_back(normalized_edge(
            static_cast<std::uint16_t>(
                sample_index(final_x, z, tile.sample_columns)),
            static_cast<std::uint16_t>(
                sample_index(final_x, z + 1U, tile.sample_columns))));
    }
    std::sort(result.begin(), result.end());
    return result;
}

[[nodiscard]] std::vector<TerrainEdge> emitted_chunk_boundary_edges(
    const shark::terrain::HeightTile& tile,
    const shark::terrain::HeightTileChunk& chunk,
    const std::vector<std::uint16_t>& indices,
    const std::size_t first_index,
    const std::size_t index_count)
{
    std::vector<TerrainEdge> result;
    const auto first_x = chunk.first_cell_x;
    const auto final_x = first_x + chunk.cell_columns;
    const auto first_z = chunk.first_cell_z;
    const auto final_z = first_z + chunk.cell_rows;
    const auto range_end = first_index + index_count;
    for (auto index = first_index; index < range_end; index += 3U) {
        const std::array triangle{
            indices[index],
            indices[index + 1U],
            indices[index + 2U],
        };
        for (std::size_t edge = 0; edge < triangle.size(); ++edge) {
            const auto first = triangle[edge];
            const auto second =
                triangle[(edge + 1U) % triangle.size()];
            const auto first_x_coordinate =
                static_cast<std::uint32_t>(first) %
                tile.sample_columns;
            const auto first_z_coordinate =
                static_cast<std::uint32_t>(first) /
                tile.sample_columns;
            const auto second_x_coordinate =
                static_cast<std::uint32_t>(second) %
                tile.sample_columns;
            const auto second_z_coordinate =
                static_cast<std::uint32_t>(second) /
                tile.sample_columns;
            const auto on_vertical_boundary =
                first_x_coordinate == second_x_coordinate &&
                (first_x_coordinate == first_x ||
                 first_x_coordinate == final_x);
            const auto on_horizontal_boundary =
                first_z_coordinate == second_z_coordinate &&
                (first_z_coordinate == first_z ||
                 first_z_coordinate == final_z);
            if (on_vertical_boundary || on_horizontal_boundary) {
                result.push_back(normalized_edge(first, second));
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace

TEST_CASE(
    "the deterministic height tile has an exact repeatable fixture",
    "[terrain][height-tile][contract]")
{
    using namespace shark;

    STATIC_REQUIRE(terrain::deterministic_tile_sample_columns == 33);
    STATIC_REQUIRE(terrain::deterministic_tile_sample_rows == 33);
    STATIC_REQUIRE(terrain::deterministic_tile_sample_spacing == 0.5F);
    STATIC_REQUIRE(terrain::deterministic_tile_vertex_count == 1'089);
    STATIC_REQUIRE(terrain::deterministic_tile_triangle_count == 2'048);
    STATIC_REQUIRE(terrain::deterministic_tile_index_count == 6'144);

    const auto first = terrain::make_deterministic_height_tile();
    const auto second = terrain::make_deterministic_height_tile();
    REQUIRE(first == second);
    REQUIRE(first.sample_columns ==
        terrain::deterministic_tile_sample_columns);
    REQUIRE(first.sample_rows ==
        terrain::deterministic_tile_sample_rows);
    REQUIRE(first.sample_spacing ==
        terrain::deterministic_tile_sample_spacing);
    REQUIRE(first.origin == terrain::deterministic_tile_origin);
    REQUIRE(first.height_offsets.size() ==
        terrain::deterministic_tile_vertex_count);

    const auto height_at = [&first](
                               const std::uint32_t x,
                               const std::uint32_t z) {
        return first.height_offsets[sample_index(
            x,
            z,
            first.sample_columns)];
    };
    REQUIRE(height_at(0, 0) == -0.25F);
    REQUIRE(height_at(10, 18) == 2.15625F);
    REQUIRE(height_at(24, 9) == 0.7578125F);
    REQUIRE(height_at(21, 24) == -0.921875F);
    REQUIRE(height_at(32, 32) == 0.25F);

    const auto [minimum, maximum] = std::minmax_element(
        first.height_offsets.begin(),
        first.height_offsets.end());
    REQUIRE(*minimum == -0.921875F);
    REQUIRE(*maximum == 2.15625F);
    REQUIRE(std::all_of(
        first.height_offsets.begin(),
        first.height_offsets.end(),
        [](const float height) {
            return std::isfinite(height);
        }));
}

TEST_CASE(
    "LOD0 mesh preserves every canonical sample and fixed cell split",
    "[terrain][height-tile][mesh]")
{
    using namespace shark;

    const auto tile = terrain::make_deterministic_height_tile();
    const auto mesh_result = terrain::build_lod0_mesh(tile);
    REQUIRE(mesh_result);
    const auto& mesh = mesh_result.value();

    REQUIRE(mesh.positions.size() ==
        terrain::deterministic_tile_vertex_count);
    REQUIRE(mesh.normals.size() ==
        terrain::deterministic_tile_vertex_count);
    REQUIRE(mesh.indices.size() ==
        terrain::deterministic_tile_index_count);
    REQUIRE(mesh.bounds ==
        terrain::deterministic_tile_expected_bounds);

    for (std::uint32_t z = 0; z < tile.sample_rows; ++z) {
        for (std::uint32_t x = 0; x < tile.sample_columns; ++x) {
            const auto index = sample_index(
                x,
                z,
                tile.sample_columns);
            const math::Float3 expected{
                tile.origin.x +
                    static_cast<float>(x) * tile.sample_spacing,
                tile.origin.y + tile.height_offsets[index],
                tile.origin.z +
                    static_cast<float>(z) * tile.sample_spacing,
            };
            REQUIRE(mesh.positions[index] == expected);
            REQUIRE(math::is_finite(mesh.positions[index]));
        }
    }

    std::size_t index_offset = 0;
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
            const std::array<std::uint16_t, 6> expected{
                static_cast<std::uint16_t>(v00),
                static_cast<std::uint16_t>(v01),
                static_cast<std::uint16_t>(v11),
                static_cast<std::uint16_t>(v00),
                static_cast<std::uint16_t>(v11),
                static_cast<std::uint16_t>(v10),
            };
            for (const auto expected_index : expected) {
                REQUIRE(mesh.indices[index_offset] == expected_index);
                ++index_offset;
            }
        }
    }
    REQUIRE(index_offset == mesh.indices.size());
}

TEST_CASE(
    "LOD0 triangles and area-weighted render normals remain valid",
    "[terrain][height-tile][normals]")
{
    using namespace shark;

    const auto mesh_result = terrain::build_lod0_mesh(
        terrain::make_deterministic_height_tile());
    REQUIRE(mesh_result);
    const auto& mesh = mesh_result.value();

    std::vector<bool> referenced(mesh.positions.size());
    std::vector<math::Float3> expected_normal_sums(
        mesh.positions.size());
    for (std::size_t triangle = 0;
         triangle < mesh.indices.size();
         triangle += 3U) {
        const auto first = mesh.indices[triangle];
        const auto second = mesh.indices[triangle + 1U];
        const auto third = mesh.indices[triangle + 2U];
        REQUIRE(first < mesh.positions.size());
        REQUIRE(second < mesh.positions.size());
        REQUIRE(third < mesh.positions.size());
        REQUIRE(first != second);
        REQUIRE(second != third);
        REQUIRE(first != third);
        referenced[first] = true;
        referenced[second] = true;
        referenced[third] = true;

        const auto first_edge = subtract(
            mesh.positions[second],
            mesh.positions[first]);
        const auto second_edge = subtract(
            mesh.positions[third],
            mesh.positions[first]);
        const auto face_normal = cross(first_edge, second_edge);
        REQUIRE(math::is_finite(face_normal));
        REQUIRE(length_squared(face_normal) > 0.0F);
        REQUIRE(face_normal.y > 0.0F);
        add_assign(expected_normal_sums[first], face_normal);
        add_assign(expected_normal_sums[second], face_normal);
        add_assign(expected_normal_sums[third], face_normal);
    }
    REQUIRE(std::all_of(
        referenced.begin(),
        referenced.end(),
        [](const bool value) { return value; }));

    for (std::size_t index = 0;
         index < mesh.normals.size();
         ++index) {
        const auto normal = mesh.normals[index];
        REQUIRE(math::is_finite(normal));
        REQUIRE(normal.y > 0.0F);
        REQUIRE(length_squared(normal) ==
            Catch::Approx(1.0F).margin(0.00001F));

        const auto expected_length = std::sqrt(
            length_squared(expected_normal_sums[index]));
        REQUIRE(expected_length > 0.0F);
        const math::Float3 expected{
            expected_normal_sums[index].x / expected_length,
            expected_normal_sums[index].y / expected_length,
            expected_normal_sums[index].z / expected_length,
        };
        REQUIRE(normal.x ==
            Catch::Approx(expected.x).margin(0.00001F));
        REQUIRE(normal.y ==
            Catch::Approx(expected.y).margin(0.00001F));
        REQUIRE(normal.z ==
            Catch::Approx(expected.z).margin(0.00001F));
    }
}

TEST_CASE(
    "derived bounds lines describe exactly twelve AABB edges",
    "[terrain][height-tile][bounds]")
{
    using namespace shark;

    const auto mesh_result = terrain::build_lod0_mesh(
        terrain::make_deterministic_height_tile());
    REQUIRE(mesh_result);
    const auto& mesh = mesh_result.value();
    const auto& lines = mesh.bounds_lines;

    const std::array<math::Float3, 8> expected_positions{{
        {-8.0F, -3.171875F, -12.0F},
        {8.0F, -3.171875F, -12.0F},
        {8.0F, -3.171875F, 4.0F},
        {-8.0F, -3.171875F, 4.0F},
        {-8.0F, -0.09375F, -12.0F},
        {8.0F, -0.09375F, -12.0F},
        {8.0F, -0.09375F, 4.0F},
        {-8.0F, -0.09375F, 4.0F},
    }};
    const std::array<std::uint16_t, 24> expected_indices{{
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
    }};
    REQUIRE(lines.positions == expected_positions);
    REQUIRE(lines.indices == expected_indices);

    std::array<std::uint32_t, 8> vertex_degrees{};
    std::set<std::pair<std::uint16_t, std::uint16_t>> unique_edges;
    for (std::size_t line = 0;
         line < lines.indices.size();
         line += 2U) {
        const auto first = lines.indices[line];
        const auto second = lines.indices[line + 1U];
        REQUIRE(first < lines.positions.size());
        REQUIRE(second < lines.positions.size());
        REQUIRE(first != second);
        ++vertex_degrees[first];
        ++vertex_degrees[second];
        unique_edges.emplace(
            std::min(first, second),
            std::max(first, second));

        const auto delta = subtract(
            lines.positions[second],
            lines.positions[first]);
        const auto changing_axes =
            static_cast<unsigned int>(delta.x != 0.0F) +
            static_cast<unsigned int>(delta.y != 0.0F) +
            static_cast<unsigned int>(delta.z != 0.0F);
        REQUIRE(changing_axes == 1U);
    }
    REQUIRE(unique_edges.size() == 12);
    REQUIRE(std::all_of(
        vertex_degrees.begin(),
        vertex_degrees.end(),
        [](const std::uint32_t degree) {
            return degree == 3U;
        }));
}

TEST_CASE(
    "LOD0 chunk layout partitions the deterministic tile exactly",
    "[terrain][height-tile][chunks][contract]")
{
    using namespace shark;

    STATIC_REQUIRE(
        terrain::deterministic_tile_chunk_cell_columns == 8);
    STATIC_REQUIRE(
        terrain::deterministic_tile_chunk_cell_rows == 8);
    STATIC_REQUIRE(terrain::deterministic_tile_chunk_columns == 4);
    STATIC_REQUIRE(terrain::deterministic_tile_chunk_rows == 4);
    STATIC_REQUIRE(terrain::deterministic_tile_chunk_count == 16);
    STATIC_REQUIRE(
        terrain::deterministic_tile_chunk_index_count == 384);

    const auto tile = terrain::make_deterministic_height_tile();
    const auto layout_result = terrain::build_lod0_chunk_layout(
        tile,
        terrain::deterministic_tile_chunk_cell_columns,
        terrain::deterministic_tile_chunk_cell_rows);
    REQUIRE(layout_result);
    const auto& layout = layout_result.value();

    REQUIRE(layout.chunks.size() ==
        terrain::deterministic_tile_chunk_count);
    REQUIRE(layout.indices.size() ==
        terrain::deterministic_tile_index_count);

    constexpr auto indices_per_chunk =
        terrain::deterministic_tile_chunk_index_count;
    std::vector<std::uint32_t> cell_visits(
        (tile.sample_columns - 1U) *
        (tile.sample_rows - 1U));
    std::size_t expected_first_index = 0;
    for (std::uint32_t chunk_z = 0;
         chunk_z < terrain::deterministic_tile_chunk_rows;
         ++chunk_z) {
        for (std::uint32_t chunk_x = 0;
             chunk_x < terrain::deterministic_tile_chunk_columns;
             ++chunk_x) {
            const auto chunk_index =
                static_cast<std::size_t>(chunk_z) *
                    terrain::deterministic_tile_chunk_columns +
                chunk_x;
            const auto& chunk = layout.chunks[chunk_index];
            const auto expected_first_cell_x =
                chunk_x *
                terrain::deterministic_tile_chunk_cell_columns;
            const auto expected_first_cell_z =
                chunk_z *
                terrain::deterministic_tile_chunk_cell_rows;

            REQUIRE(chunk.first_cell_x == expected_first_cell_x);
            REQUIRE(chunk.first_cell_z == expected_first_cell_z);
            REQUIRE(chunk.cell_columns ==
                terrain::deterministic_tile_chunk_cell_columns);
            REQUIRE(chunk.cell_rows ==
                terrain::deterministic_tile_chunk_cell_rows);
            REQUIRE(chunk.first_index == expected_first_index);
            REQUIRE(chunk.index_count == indices_per_chunk);
            REQUIRE(chunk.first_index + chunk.index_count <=
                layout.indices.size());

            const auto expected_bounds = expected_chunk_bounds(
                tile,
                chunk.first_cell_x,
                chunk.first_cell_z,
                chunk.cell_columns,
                chunk.cell_rows);
            REQUIRE(chunk.bounds == expected_bounds);
            REQUIRE(chunk.bounds_lines.positions ==
                expected_bounds_line_positions(expected_bounds));
            REQUIRE(chunk.bounds_lines.indices ==
                expected_bounds_line_indices);

            auto current_index = chunk.first_index;
            for (std::uint32_t local_z = 0;
                 local_z < chunk.cell_rows;
                 ++local_z) {
                const auto cell_z = chunk.first_cell_z + local_z;
                for (std::uint32_t local_x = 0;
                     local_x < chunk.cell_columns;
                     ++local_x) {
                    const auto cell_x =
                        chunk.first_cell_x + local_x;
                    const auto expected = expected_cell_indices(
                        cell_x,
                        cell_z,
                        tile.sample_columns);
                    for (const auto index : expected) {
                        REQUIRE(layout.indices[current_index] == index);
                        ++current_index;
                    }
                    const auto cell_index =
                        static_cast<std::size_t>(cell_z) *
                            (tile.sample_columns - 1U) +
                        cell_x;
                    ++cell_visits[cell_index];
                }
            }
            REQUIRE(current_index ==
                chunk.first_index + chunk.index_count);
            expected_first_index += chunk.index_count;
        }
    }
    REQUIRE(expected_first_index == layout.indices.size());
    REQUIRE(std::all_of(
        cell_visits.begin(),
        cell_visits.end(),
        [](const std::uint32_t visits) {
            return visits == 1U;
        }));

    REQUIRE((
        layout.chunks.front().bounds == terrain::Bounds3{
            {-8.0F, -2.5F, -12.0F},
            {-4.0F, -1.75F, -8.0F},
        }));
    REQUIRE((
        layout.chunks[10].bounds == terrain::Bounds3{
            {0.0F, -3.171875F, -4.0F},
            {4.0F, -0.578125F, 0.0F},
        }));
    REQUIRE((
        layout.chunks.back().bounds == terrain::Bounds3{
            {4.0F, -2.984375F, 0.0F},
            {8.0F, -2.0F, 4.0F},
        }));
}

TEST_CASE(
    "coarse chunk layout is bounded and preserves every deterministic seam",
    "[terrain][height-tile][lod][contract]")
{
    using namespace shark;

    STATIC_REQUIRE(
        terrain::deterministic_tile_coarse_chunk_index_count == 240);
    STATIC_REQUIRE(
        terrain::deterministic_tile_coarse_index_count == 3'840);
    STATIC_REQUIRE(
        terrain::deterministic_tile_coarse_maximum_geometric_error ==
        0.140625);

    const auto tile = terrain::make_deterministic_height_tile();
    const auto mesh_result = terrain::build_lod0_mesh(tile);
    const auto lod0_result = terrain::build_lod0_chunk_layout(
        tile,
        terrain::deterministic_tile_chunk_cell_columns,
        terrain::deterministic_tile_chunk_cell_rows);
    const auto coarse_result =
        terrain::build_boundary_preserving_coarse_chunk_layout(
            tile,
            terrain::deterministic_tile_chunk_cell_columns,
            terrain::deterministic_tile_chunk_cell_rows);
    REQUIRE(mesh_result);
    REQUIRE(lod0_result);
    REQUIRE(coarse_result);
    const auto& mesh = mesh_result.value();
    const auto& lod0 = lod0_result.value();
    const auto& coarse = coarse_result.value();

    REQUIRE(coarse.chunks.size() == lod0.chunks.size());
    REQUIRE(coarse.chunks.size() ==
        terrain::deterministic_tile_chunk_count);
    REQUIRE(coarse.indices.size() ==
        terrain::deterministic_tile_coarse_index_count);
    REQUIRE(coarse.maximum_geometric_error ==
        terrain::deterministic_tile_coarse_maximum_geometric_error);

    constexpr std::array<double, 16> expected_chunk_errors{{
        0.109375,
        0.109375,
        0.0625,
        0.0546875,
        0.125,
        0.046875,
        0.125,
        0.03515625,
        0.015625,
        0.1015625,
        0.125,
        0.0859375,
        0.125,
        0.109375,
        0.140625,
        0.0859375,
    }};

    std::size_t expected_first_index = 0;
    for (std::size_t chunk_index = 0;
         chunk_index < coarse.chunks.size();
         ++chunk_index) {
        const auto& lod0_chunk = lod0.chunks[chunk_index];
        const auto& coarse_chunk = coarse.chunks[chunk_index];
        REQUIRE(coarse_chunk.first_index == expected_first_index);
        REQUIRE(coarse_chunk.index_count ==
            terrain::deterministic_tile_coarse_chunk_index_count);
        REQUIRE(coarse_chunk.maximum_geometric_error ==
            expected_chunk_errors[chunk_index]);
        REQUIRE(coarse_chunk.first_index +
                coarse_chunk.index_count <=
            coarse.indices.size());

        double projected_twice_area = 0.0;
        const auto range_end =
            coarse_chunk.first_index + coarse_chunk.index_count;
        for (auto index = coarse_chunk.first_index;
             index < range_end;
             index += 3U) {
            const auto first = coarse.indices[index];
            const auto second = coarse.indices[index + 1U];
            const auto third = coarse.indices[index + 2U];
            REQUIRE(first < mesh.positions.size());
            REQUIRE(second < mesh.positions.size());
            REQUIRE(third < mesh.positions.size());
            REQUIRE(first != second);
            REQUIRE(second != third);
            REQUIRE(first != third);

            const auto first_x =
                static_cast<std::uint32_t>(first) %
                tile.sample_columns;
            const auto first_z =
                static_cast<std::uint32_t>(first) /
                tile.sample_columns;
            const auto second_x =
                static_cast<std::uint32_t>(second) %
                tile.sample_columns;
            const auto second_z =
                static_cast<std::uint32_t>(second) /
                tile.sample_columns;
            const auto third_x =
                static_cast<std::uint32_t>(third) %
                tile.sample_columns;
            const auto third_z =
                static_cast<std::uint32_t>(third) /
                tile.sample_columns;
            REQUIRE(first_x >= lod0_chunk.first_cell_x);
            REQUIRE(first_x <=
                lod0_chunk.first_cell_x +
                    lod0_chunk.cell_columns);
            REQUIRE(first_z >= lod0_chunk.first_cell_z);
            REQUIRE(first_z <=
                lod0_chunk.first_cell_z + lod0_chunk.cell_rows);
            REQUIRE(second_x >= lod0_chunk.first_cell_x);
            REQUIRE(second_x <=
                lod0_chunk.first_cell_x +
                    lod0_chunk.cell_columns);
            REQUIRE(second_z >= lod0_chunk.first_cell_z);
            REQUIRE(second_z <=
                lod0_chunk.first_cell_z + lod0_chunk.cell_rows);
            REQUIRE(third_x >= lod0_chunk.first_cell_x);
            REQUIRE(third_x <=
                lod0_chunk.first_cell_x +
                    lod0_chunk.cell_columns);
            REQUIRE(third_z >= lod0_chunk.first_cell_z);
            REQUIRE(third_z <=
                lod0_chunk.first_cell_z + lod0_chunk.cell_rows);

            const auto first_edge = subtract(
                mesh.positions[second],
                mesh.positions[first]);
            const auto second_edge = subtract(
                mesh.positions[third],
                mesh.positions[first]);
            const auto face_normal = cross(
                first_edge,
                second_edge);
            REQUIRE(math::is_finite(face_normal));
            REQUIRE(face_normal.y > 0.0F);
            projected_twice_area +=
                static_cast<double>(face_normal.y);
        }
        const auto chunk_width =
            static_cast<double>(lod0_chunk.cell_columns) *
            tile.sample_spacing;
        const auto chunk_depth =
            static_cast<double>(lod0_chunk.cell_rows) *
            tile.sample_spacing;
        REQUIRE(projected_twice_area ==
            Catch::Approx(2.0 * chunk_width * chunk_depth)
                .margin(0.000001));

        const auto expected_boundary =
            expected_chunk_boundary_edges(tile, lod0_chunk);
        REQUIRE(emitted_chunk_boundary_edges(
                    tile,
                    lod0_chunk,
                    lod0.indices,
                    lod0_chunk.first_index,
                    lod0_chunk.index_count) ==
            expected_boundary);
        REQUIRE(emitted_chunk_boundary_edges(
                    tile,
                    lod0_chunk,
                    coarse.indices,
                    coarse_chunk.first_index,
                    coarse_chunk.index_count) ==
            expected_boundary);

        expected_first_index += coarse_chunk.index_count;
    }
    REQUIRE(expected_first_index == coarse.indices.size());

    // The first 2x2 patch is on the chunk's top and left edges. Its four
    // center-fan triangles therefore become six triangles with exact
    // one-sample boundary segments.
    constexpr std::array<std::uint16_t, 18>
        expected_first_patch_indices{{
            0, 34, 1,
            1, 34, 2,
            2, 34, 68,
            68, 34, 66,
            66, 34, 33,
            33, 34, 0,
        }};
    REQUIRE(std::equal(
        expected_first_patch_indices.begin(),
        expected_first_patch_indices.end(),
        coarse.indices.begin()));

    const auto lod0_after_result =
        terrain::build_lod0_chunk_layout(
            tile,
            terrain::deterministic_tile_chunk_cell_columns,
            terrain::deterministic_tile_chunk_cell_rows);
    REQUIRE(lod0_after_result);
    REQUIRE(lod0_after_result.value() == lod0);
}

TEST_CASE(
    "LOD0 chunk layout supports partial edge chunks",
    "[terrain][height-tile][chunks][partial]")
{
    using namespace shark;

    auto tile = make_flat_tile(12, 7);
    tile.sample_spacing = 0.25F;
    tile.origin = {-3.0F, -5.0F, 2.0F};
    for (std::uint32_t z = 0; z < tile.sample_rows; ++z) {
        for (std::uint32_t x = 0;
             x < tile.sample_columns;
             ++x) {
            tile.height_offsets[sample_index(
                x,
                z,
                tile.sample_columns)] =
                static_cast<float>(x + z * 10U) / 8.0F;
        }
    }

    const auto layout_result =
        terrain::build_lod0_chunk_layout(tile, 4, 4);
    REQUIRE(layout_result);
    const auto& layout = layout_result.value();
    REQUIRE(layout.chunks.size() == 6);
    REQUIRE(layout.indices.size() == 11U * 6U * 6U);

    struct ExpectedChunk final {
        std::uint32_t first_cell_x;
        std::uint32_t first_cell_z;
        std::uint32_t cell_columns;
        std::uint32_t cell_rows;
    };
    constexpr std::array expected_chunks{
        ExpectedChunk{0, 0, 4, 4},
        ExpectedChunk{4, 0, 4, 4},
        ExpectedChunk{8, 0, 3, 4},
        ExpectedChunk{0, 4, 4, 2},
        ExpectedChunk{4, 4, 4, 2},
        ExpectedChunk{8, 4, 3, 2},
    };

    std::vector<std::uint32_t> cell_visits(11U * 6U);
    std::size_t expected_first_index = 0;
    for (std::size_t chunk_index = 0;
         chunk_index < expected_chunks.size();
         ++chunk_index) {
        const auto& chunk = layout.chunks[chunk_index];
        const auto& expected_chunk = expected_chunks[chunk_index];
        REQUIRE(chunk.first_cell_x ==
            expected_chunk.first_cell_x);
        REQUIRE(chunk.first_cell_z ==
            expected_chunk.first_cell_z);
        REQUIRE(chunk.cell_columns ==
            expected_chunk.cell_columns);
        REQUIRE(chunk.cell_rows ==
            expected_chunk.cell_rows);
        REQUIRE(chunk.first_index == expected_first_index);
        REQUIRE(chunk.index_count ==
            static_cast<std::size_t>(
                chunk.cell_columns * chunk.cell_rows) *
                6U);

        const auto expected_bounds = expected_chunk_bounds(
            tile,
            chunk.first_cell_x,
            chunk.first_cell_z,
            chunk.cell_columns,
            chunk.cell_rows);
        REQUIRE(chunk.bounds == expected_bounds);
        REQUIRE(chunk.bounds_lines.positions ==
            expected_bounds_line_positions(expected_bounds));
        REQUIRE(chunk.bounds_lines.indices ==
            expected_bounds_line_indices);

        auto current_index = chunk.first_index;
        for (std::uint32_t local_z = 0;
             local_z < chunk.cell_rows;
             ++local_z) {
            const auto cell_z = chunk.first_cell_z + local_z;
            for (std::uint32_t local_x = 0;
                 local_x < chunk.cell_columns;
                 ++local_x) {
                const auto cell_x = chunk.first_cell_x + local_x;
                const auto expected = expected_cell_indices(
                    cell_x,
                    cell_z,
                    tile.sample_columns);
                for (const auto index : expected) {
                    REQUIRE(layout.indices[current_index] == index);
                    ++current_index;
                }
                ++cell_visits[
                    static_cast<std::size_t>(cell_z) * 11U +
                    cell_x];
            }
        }
        REQUIRE(current_index ==
            chunk.first_index + chunk.index_count);
        expected_first_index += chunk.index_count;
    }
    REQUIRE(expected_first_index == layout.indices.size());
    REQUIRE(std::all_of(
        cell_visits.begin(),
        cell_visits.end(),
        [](const std::uint32_t visits) {
            return visits == 1U;
        }));

    const auto one_chunk_result =
        terrain::build_lod0_chunk_layout(tile, 100, 100);
    const auto mesh_result = terrain::build_lod0_mesh(tile);
    REQUIRE(one_chunk_result);
    REQUIRE(mesh_result);
    REQUIRE(one_chunk_result.value().chunks.size() == 1);
    REQUIRE(one_chunk_result.value().indices ==
        mesh_result.value().indices);
    REQUIRE(one_chunk_result.value().chunks.front().bounds ==
        mesh_result.value().bounds);
}

TEST_CASE(
    "coarse layout falls back exactly for odd and partial chunks",
    "[terrain][height-tile][lod][partial]")
{
    using namespace shark;

    auto tile = make_flat_tile(12, 7);
    tile.sample_spacing = 0.25F;
    tile.origin = {-3.0F, -5.0F, 2.0F};
    for (std::uint32_t z = 0; z < tile.sample_rows; ++z) {
        for (std::uint32_t x = 0;
             x < tile.sample_columns;
             ++x) {
            tile.height_offsets[sample_index(
                x,
                z,
                tile.sample_columns)] =
                static_cast<float>(x + z * 10U) / 8.0F;
        }
    }

    const auto lod0_result =
        terrain::build_lod0_chunk_layout(tile, 4, 4);
    const auto coarse_result =
        terrain::build_boundary_preserving_coarse_chunk_layout(
            tile,
            4,
            4);
    REQUIRE(lod0_result);
    REQUIRE(coarse_result);
    const auto& lod0 = lod0_result.value();
    const auto& coarse = coarse_result.value();
    REQUIRE(coarse.chunks.size() == lod0.chunks.size());
    REQUIRE(coarse.chunks.size() == 6);
    REQUIRE(coarse.indices.size() == 348);
    REQUIRE(coarse.maximum_geometric_error == 0.0);

    constexpr std::array<std::size_t, 6> expected_index_counts{{
        72,
        72,
        72,
        48,
        48,
        36,
    }};
    std::size_t expected_first_index = 0;
    for (std::size_t chunk_index = 0;
         chunk_index < coarse.chunks.size();
         ++chunk_index) {
        const auto& lod0_chunk = lod0.chunks[chunk_index];
        const auto& coarse_chunk = coarse.chunks[chunk_index];
        REQUIRE(coarse_chunk.first_index == expected_first_index);
        REQUIRE(coarse_chunk.index_count ==
            expected_index_counts[chunk_index]);
        REQUIRE(coarse_chunk.maximum_geometric_error == 0.0);
        REQUIRE(emitted_chunk_boundary_edges(
                    tile,
                    lod0_chunk,
                    coarse.indices,
                    coarse_chunk.first_index,
                    coarse_chunk.index_count) ==
            expected_chunk_boundary_edges(tile, lod0_chunk));

        if (chunk_index >= 2U) {
            REQUIRE(coarse_chunk.index_count ==
                lod0_chunk.index_count);
            for (std::size_t local_index = 0;
                 local_index < coarse_chunk.index_count;
                 ++local_index) {
                REQUIRE(coarse.indices[
                            coarse_chunk.first_index +
                            local_index] ==
                    lod0.indices[
                        lod0_chunk.first_index + local_index]);
            }
        }
        expected_first_index += coarse_chunk.index_count;
    }
    REQUIRE(expected_first_index == coarse.indices.size());

    SECTION("one exact even chunk is reduced")
    {
        const auto one_chunk =
            terrain::build_boundary_preserving_coarse_chunk_layout(
                make_flat_tile(9, 9),
                8,
                8);
        REQUIRE(one_chunk);
        REQUIRE(one_chunk.value().chunks.size() == 1);
        REQUIRE(one_chunk.value().indices.size() == 240);
        REQUIRE(one_chunk.value().chunks.front().first_index == 0);
        REQUIRE(one_chunk.value().chunks.front().index_count == 240);
        REQUIRE(
            one_chunk.value().maximum_geometric_error == 0.0);
    }

    SECTION("one oversized partial chunk copies LOD0")
    {
        const auto deterministic =
            terrain::make_deterministic_height_tile();
        const auto one_lod0 = terrain::build_lod0_chunk_layout(
            deterministic,
            100,
            100);
        const auto one_coarse =
            terrain::build_boundary_preserving_coarse_chunk_layout(
                deterministic,
                100,
                100);
        REQUIRE(one_lod0);
        REQUIRE(one_coarse);
        REQUIRE(one_coarse.value().chunks.size() == 1);
        REQUIRE(one_coarse.value().indices ==
            one_lod0.value().indices);
        REQUIRE(one_coarse.value().chunks.front().index_count ==
            terrain::deterministic_tile_index_count);
        REQUIRE(
            one_coarse.value().maximum_geometric_error == 0.0);
    }
}

TEST_CASE(
    "LOD0 chunk layout rejects invalid dimensions and tiles",
    "[terrain][height-tile][chunks][validation]")
{
    using namespace shark;

    SECTION("zero chunk width")
    {
        const auto result = terrain::build_lod0_chunk_layout(
            make_flat_tile(2, 2),
            0,
            1);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("zero chunk height")
    {
        const auto result = terrain::build_lod0_chunk_layout(
            make_flat_tile(2, 2),
            1,
            0);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("malformed source tile")
    {
        auto tile = make_flat_tile(3, 3);
        tile.height_offsets.pop_back();
        const auto result =
            terrain::build_lod0_chunk_layout(tile, 1, 1);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("nonfinite source tile")
    {
        auto tile = make_flat_tile(3, 3);
        tile.height_offsets[4] =
            std::numeric_limits<float>::quiet_NaN();
        const auto result =
            terrain::build_lod0_chunk_layout(tile, 1, 1);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("uint16 vertex capacity")
    {
        const auto result = terrain::build_lod0_chunk_layout(
            make_flat_tile(257, 257),
            8,
            8);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unsupported);
    }
}

TEST_CASE(
    "coarse chunk layout rejects invalid dimensions and tiles",
    "[terrain][height-tile][lod][validation]")
{
    using namespace shark;

    SECTION("zero chunk width")
    {
        const auto result =
            terrain::build_boundary_preserving_coarse_chunk_layout(
                make_flat_tile(2, 2),
                0,
                1);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("zero chunk height")
    {
        const auto result =
            terrain::build_boundary_preserving_coarse_chunk_layout(
                make_flat_tile(2, 2),
                1,
                0);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("malformed source tile")
    {
        auto tile = make_flat_tile(3, 3);
        tile.height_offsets.pop_back();
        const auto result =
            terrain::build_boundary_preserving_coarse_chunk_layout(
                tile,
                2,
                2);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("nonfinite source tile")
    {
        auto tile = make_flat_tile(3, 3);
        tile.height_offsets[4] =
            std::numeric_limits<float>::quiet_NaN();
        const auto result =
            terrain::build_boundary_preserving_coarse_chunk_layout(
                tile,
                2,
                2);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("uint16 vertex capacity")
    {
        const auto result =
            terrain::build_boundary_preserving_coarse_chunk_layout(
                make_flat_tile(257, 257),
                8,
                8);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unsupported);
    }
}

TEST_CASE(
    "LOD0 mesh construction rejects malformed or unsupported tiles",
    "[terrain][height-tile][validation]")
{
    using namespace shark;

    SECTION("insufficient sample dimensions")
    {
        auto tile = make_flat_tile(2, 2);
        tile.sample_columns = 1;
        const auto result = terrain::build_lod0_mesh(tile);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("invalid sample spacing")
    {
        auto tile = make_flat_tile(2, 2);
        tile.sample_spacing = 0.0F;
        const auto zero_result = terrain::build_lod0_mesh(tile);
        REQUIRE_FALSE(zero_result);
        REQUIRE(zero_result.error().code() ==
            core::ErrorCode::invalid_argument);

        tile.sample_spacing =
            std::numeric_limits<float>::quiet_NaN();
        const auto nonfinite_result =
            terrain::build_lod0_mesh(tile);
        REQUIRE_FALSE(nonfinite_result);
        REQUIRE(nonfinite_result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("nonfinite origin")
    {
        auto tile = make_flat_tile(2, 2);
        tile.origin.x = std::numeric_limits<float>::infinity();
        const auto result = terrain::build_lod0_mesh(tile);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("mismatched height storage")
    {
        auto tile = make_flat_tile(2, 2);
        tile.height_offsets.pop_back();
        const auto result = terrain::build_lod0_mesh(tile);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("nonfinite height sample")
    {
        auto tile = make_flat_tile(2, 2);
        tile.height_offsets[2] =
            std::numeric_limits<float>::quiet_NaN();
        const auto result = terrain::build_lod0_mesh(tile);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("uint16 vertex capacity")
    {
        const auto tile = make_flat_tile(257, 257);
        const auto result = terrain::build_lod0_mesh(tile);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unsupported);
    }
}
