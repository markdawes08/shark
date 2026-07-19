#include "terrain_frustum_culling.hpp"
#include "terrain_lod_selection.hpp"

#include <shark/terrain/height_tile.hpp>
#include <shark/world/camera.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

TEST_CASE(
    "terrain LOD distance uses the nearest point on the chunk bounds",
    "[renderer][terrain][lod][metric]")
{
    using namespace shark;
    using namespace shark::renderer::detail;

    constexpr math::Float3 minimum{-1.0F, -2.0F, -3.0F};
    constexpr math::Float3 maximum{1.0F, 2.0F, 3.0F};

    const auto inside = select_terrain_lod(
        {0.0F, 0.0F, 0.0F},
        minimum,
        maximum,
        1.0);
    REQUIRE(inside);
    REQUIRE(inside->camera_distance == 0.0);
    REQUIRE(inside->lod == TerrainLod::lod0);

    const auto one_axis = select_terrain_lod(
        {4.0F, 0.0F, 0.0F},
        minimum,
        maximum,
        1.0);
    REQUIRE(one_axis);
    REQUIRE(one_axis->camera_distance == Catch::Approx(3.0));

    const auto three_axes = select_terrain_lod(
        {4.0F, 6.0F, 15.0F},
        minimum,
        maximum,
        1.0);
    REQUIRE(three_axes);
    REQUIRE(three_axes->camera_distance == Catch::Approx(13.0));
}

TEST_CASE(
    "terrain LOD selection keeps the inclusive relative-error tie",
    "[renderer][terrain][lod][threshold]")
{
    using namespace shark;
    using namespace shark::renderer::detail;

    constexpr math::Float3 minimum{0.0F, 0.0F, 0.0F};
    constexpr math::Float3 maximum{1.0F, 1.0F, 1.0F};
    constexpr math::Float3 camera{101.0F, 0.5F, 0.5F};
    constexpr double distance = 100.0;
    constexpr double tie_error =
        terrain_coarse_relative_error_limit * distance;

    const auto below = select_terrain_lod(
        camera,
        minimum,
        maximum,
        tie_error * 0.999);
    const auto tie = select_terrain_lod(
        camera,
        minimum,
        maximum,
        tie_error);
    const auto above = select_terrain_lod(
        camera,
        minimum,
        maximum,
        tie_error * 1.001);
    REQUIRE(below);
    REQUIRE(tie);
    REQUIRE(above);
    REQUIRE(below->lod == TerrainLod::coarse);
    REQUIRE(tie->lod == TerrainLod::coarse);
    REQUIRE(above->lod == TerrainLod::lod0);
}

TEST_CASE(
    "terrain LOD zero-error surface remains exact at zero distance",
    "[renderer][terrain][lod][zero]")
{
    using namespace shark;
    using namespace shark::renderer::detail;

    constexpr math::Float3 minimum{-1.0F, -1.0F, -1.0F};
    constexpr math::Float3 maximum{1.0F, 1.0F, 1.0F};

    const auto exact_inside = select_terrain_lod(
        {},
        minimum,
        maximum,
        0.0);
    const auto approximate_inside = select_terrain_lod(
        {},
        minimum,
        maximum,
        0.000001);
    const auto exact_outside = select_terrain_lod(
        {2.0F, 0.0F, 0.0F},
        minimum,
        maximum,
        0.0);
    REQUIRE(exact_inside);
    REQUIRE(approximate_inside);
    REQUIRE(exact_outside);
    REQUIRE(exact_inside->lod == TerrainLod::coarse);
    REQUIRE(approximate_inside->lod == TerrainLod::lod0);
    REQUIRE(exact_outside->lod == TerrainLod::coarse);
}

TEST_CASE(
    "terrain LOD selection rejects invalid metrics",
    "[renderer][terrain][lod][validation]")
{
    using namespace shark;
    using namespace shark::renderer::detail;

    constexpr math::Float3 minimum{-1.0F, -1.0F, -1.0F};
    constexpr math::Float3 maximum{1.0F, 1.0F, 1.0F};
    const auto infinity = std::numeric_limits<float>::infinity();
    const auto double_infinity =
        std::numeric_limits<double>::infinity();
    const auto quiet_nan =
        std::numeric_limits<double>::quiet_NaN();

    REQUIRE_FALSE(select_terrain_lod(
        {infinity, 0.0F, 0.0F},
        minimum,
        maximum,
        0.0));
    REQUIRE_FALSE(select_terrain_lod(
        {},
        {2.0F, -1.0F, -1.0F},
        maximum,
        0.0));
    REQUIRE_FALSE(select_terrain_lod(
        {},
        minimum,
        {infinity, 1.0F, 1.0F},
        0.0));
    REQUIRE_FALSE(select_terrain_lod(
        {},
        minimum,
        maximum,
        -0.001));
    REQUIRE_FALSE(select_terrain_lod(
        {},
        minimum,
        maximum,
        double_infinity));
    REQUIRE_FALSE(select_terrain_lod(
        {},
        minimum,
        maximum,
        quiet_nan));
}

TEST_CASE(
    "deterministic smoke poses select both terrain LODs",
    "[renderer][terrain][lod][smoke]")
{
    using namespace shark;
    using namespace shark::renderer::detail;

    const auto layout = terrain::build_lod0_chunk_layout(
        terrain::make_deterministic_height_tile(),
        terrain::deterministic_tile_chunk_cell_columns,
        terrain::deterministic_tile_chunk_cell_rows);
    REQUIRE(layout);
    const auto coarse =
        terrain::build_boundary_preserving_coarse_chunk_layout(
            terrain::make_deterministic_height_tile(),
            terrain::deterministic_tile_chunk_cell_columns,
            terrain::deterministic_tile_chunk_cell_rows);
    REQUIRE(coarse);
    REQUIRE(layout.value().chunks.size() ==
        terrain::deterministic_tile_chunk_count);
    REQUIRE(coarse.value().chunks.size() ==
        layout.value().chunks.size());

    const auto counts = [&layout, &coarse](
                            const world::Camera& camera,
                            const float aspect_ratio) {
        const auto matrices = world::build_camera_matrices(
            camera,
            aspect_ratio);
        REQUIRE(matrices);
        const auto frustum = make_view_frustum(
            matrices.value().view_projection);
        REQUIRE(frustum);

        std::array<std::size_t, 3> result{};
        for (std::size_t index = 0;
             index < layout.value().chunks.size();
             ++index) {
            const auto& chunk = layout.value().chunks[index];
            if (!intersects_view_frustum(
                    *frustum,
                    chunk.bounds.minimum,
                    chunk.bounds.maximum)) {
                continue;
            }
            const auto decision = select_terrain_lod(
                camera.transform.position,
                chunk.bounds.minimum,
                chunk.bounds.maximum,
                coarse.value()
                    .chunks[index]
                    .maximum_geometric_error);
            REQUIRE(decision);
            ++result[0];
            ++result[
                decision->lod == TerrainLod::lod0 ? 1U : 2U];
        }
        return result;
    };

    world::Camera initial;
    initial.transform.position = {0.0F, 4.0F, 10.0F};
    initial.transform.pitch_radians = -0.35F;
    REQUIRE(counts(initial, 16.0F / 9.0F) ==
        std::array<std::size_t, 3>{16, 8, 8});

    auto culling_pose = initial;
    culling_pose.transform.yaw_radians = 1.25F;
    REQUIRE(counts(culling_pose, 960.0F / 600.0F) ==
        std::array<std::size_t, 3>{5, 3, 2});
}
