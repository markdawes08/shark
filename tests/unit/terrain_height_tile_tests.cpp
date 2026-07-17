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
