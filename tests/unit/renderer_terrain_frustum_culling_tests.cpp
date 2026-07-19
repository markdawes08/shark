#include "terrain_frustum_culling.hpp"

#include <shark/terrain/height_tile.hpp>
#include <shark/world/camera.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

TEST_CASE(
    "terrain frustum extraction follows Direct3D clip half spaces",
    "[renderer][terrain][culling][frustum]")
{
    using namespace shark;
    using namespace shark::renderer::detail;

    const auto frustum = make_view_frustum(math::identity_matrix());
    REQUIRE(frustum);
    const std::array expected_planes{
        FrustumPlane{{1.0F, 0.0F, 0.0F}, 1.0F},
        FrustumPlane{{-1.0F, 0.0F, 0.0F}, 1.0F},
        FrustumPlane{{0.0F, 1.0F, 0.0F}, 1.0F},
        FrustumPlane{{0.0F, -1.0F, 0.0F}, 1.0F},
        FrustumPlane{{0.0F, 0.0F, 1.0F}, 0.0F},
        FrustumPlane{{0.0F, 0.0F, -1.0F}, 1.0F},
    };
    for (std::size_t index = 0;
         index < expected_planes.size();
         ++index) {
        const auto& actual = frustum->planes[index];
        const auto& expected = expected_planes[index];
        REQUIRE(actual.normal.x == Catch::Approx(expected.normal.x));
        REQUIRE(actual.normal.y == Catch::Approx(expected.normal.y));
        REQUIRE(actual.normal.z == Catch::Approx(expected.normal.z));
        REQUIRE(actual.distance == Catch::Approx(expected.distance));
        const auto normal_length = std::sqrt(
            actual.normal.x * actual.normal.x +
            actual.normal.y * actual.normal.y +
            actual.normal.z * actual.normal.z);
        REQUIRE(normal_length ==
            Catch::Approx(1.0F).margin(0.000001F));
    }

    REQUIRE(intersects_view_frustum(
        *frustum,
        {-0.5F, -0.5F, 0.25F},
        {0.5F, 0.5F, 0.75F}));
    REQUIRE(intersects_view_frustum(
        *frustum,
        {-2.0F, -2.0F, -1.0F},
        {2.0F, 2.0F, 2.0F}));

    const std::array outside_bounds{
        std::pair{
            math::Float3{-2.0F, -0.5F, 0.25F},
            math::Float3{-1.1F, 0.5F, 0.75F}},
        std::pair{
            math::Float3{1.1F, -0.5F, 0.25F},
            math::Float3{2.0F, 0.5F, 0.75F}},
        std::pair{
            math::Float3{-0.5F, -2.0F, 0.25F},
            math::Float3{0.5F, -1.1F, 0.75F}},
        std::pair{
            math::Float3{-0.5F, 1.1F, 0.25F},
            math::Float3{0.5F, 2.0F, 0.75F}},
        std::pair{
            math::Float3{-0.5F, -0.5F, -1.0F},
            math::Float3{0.5F, 0.5F, -0.1F}},
        std::pair{
            math::Float3{-0.5F, -0.5F, 1.1F},
            math::Float3{0.5F, 0.5F, 2.0F}},
    };
    for (const auto& [minimum, maximum] : outside_bounds) {
        REQUIRE_FALSE(intersects_view_frustum(
            *frustum,
            minimum,
            maximum));
    }

    REQUIRE(intersects_view_frustum(
        *frustum,
        {-1.25F, -0.25F, 0.25F},
        {-1.0F, 0.25F, 0.75F}));
    REQUIRE(intersects_view_frustum(
        *frustum,
        {-0.25F, -0.25F, 0.0F},
        {0.25F, 0.25F, 0.25F}));
}

TEST_CASE(
    "terrain frustum rejects degenerate and nonfinite transforms",
    "[renderer][terrain][culling][frustum][validation]")
{
    using namespace shark;
    using namespace shark::renderer::detail;

    REQUIRE_FALSE(make_view_frustum(math::Matrix4x4{}));

    auto nonfinite = math::identity_matrix();
    nonfinite.elements[2][1] =
        std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(make_view_frustum(nonfinite));

    auto singular = math::identity_matrix();
    singular.elements[3][3] = 0.0F;
    REQUIRE_FALSE(make_view_frustum(singular));
}

TEST_CASE(
    "terrain frustum extraction is positive-scale invariant",
    "[renderer][terrain][culling][frustum][scale]")
{
    using namespace shark;
    using namespace shark::renderer::detail;

    auto transform = math::identity_matrix();
    transform.elements[0][0] = 2.0F;
    transform.elements[0][3] = 1.5F;
    const auto reference = make_view_frustum(transform);
    REQUIRE(reference);
    for (const auto scale : {1.0e-30F, 1.0e30F}) {
        auto scaled = transform;
        for (auto& row : scaled.elements) {
            for (auto& element : row) {
                element *= scale;
            }
        }
        const auto frustum = make_view_frustum(scaled);
        REQUIRE(frustum);
        for (std::size_t index = 0;
             index < reference->planes.size();
             ++index) {
            REQUIRE(frustum->planes[index].normal.x ==
                Catch::Approx(
                    reference->planes[index].normal.x));
            REQUIRE(frustum->planes[index].normal.y ==
                Catch::Approx(
                    reference->planes[index].normal.y));
            REQUIRE(frustum->planes[index].normal.z ==
                Catch::Approx(
                    reference->planes[index].normal.z));
            REQUIRE(frustum->planes[index].distance ==
                Catch::Approx(
                    reference->planes[index].distance));
        }
    }

    auto near_float_limit = transform;
    const auto extreme_scale =
        std::numeric_limits<float>::max() * 0.5F;
    for (auto& row : near_float_limit.elements) {
        for (auto& element : row) {
            element *= extreme_scale;
        }
    }
    REQUIRE(math::is_finite(near_float_limit));
    const auto extreme_frustum =
        make_view_frustum(near_float_limit);
    REQUIRE(extreme_frustum);
    for (std::size_t index = 0;
         index < reference->planes.size();
         ++index) {
        REQUIRE(extreme_frustum->planes[index].normal.x ==
            Catch::Approx(
                reference->planes[index].normal.x));
        REQUIRE(extreme_frustum->planes[index].normal.y ==
            Catch::Approx(
                reference->planes[index].normal.y));
        REQUIRE(extreme_frustum->planes[index].normal.z ==
            Catch::Approx(
                reference->planes[index].normal.z));
        REQUIRE(extreme_frustum->planes[index].distance ==
            Catch::Approx(
                reference->planes[index].distance));
    }
}

TEST_CASE(
    "terrain frustum honors the camera reversed-Z range",
    "[renderer][terrain][culling][frustum][camera]")
{
    using namespace shark;
    using namespace shark::renderer::detail;

    world::Camera camera;
    const auto matrices = world::build_camera_matrices(camera, 16.0F / 9.0F);
    REQUIRE(matrices);
    const auto frustum = make_view_frustum(
        matrices.value().view_projection);
    REQUIRE(frustum);

    REQUIRE(intersects_view_frustum(
        *frustum,
        {-1.0F, -1.0F, -2.0F},
        {1.0F, 1.0F, 1.0F}));
    REQUIRE_FALSE(intersects_view_frustum(
        *frustum,
        {-1.0F, -1.0F, 5.0F},
        {1.0F, 1.0F, 6.0F}));
    REQUIRE_FALSE(intersects_view_frustum(
        *frustum,
        {-1.0F, -1.0F, -102.0F},
        {1.0F, 1.0F, -101.0F}));
    REQUIRE_FALSE(intersects_view_frustum(
        *frustum,
        {100.0F, -1.0F, -2.0F},
        {101.0F, 1.0F, 1.0F}));

    camera.lens.far_plane = 100'000'000.0F;
    const auto extreme_matrices =
        world::build_camera_matrices(camera, 16.0F / 9.0F);
    REQUIRE(extreme_matrices);
    const auto extreme_frustum = make_view_frustum(
        extreme_matrices.value().view_projection);
    REQUIRE(extreme_frustum);
    REQUIRE(intersects_view_frustum(
        *extreme_frustum,
        {-1.0F, -1.0F, -2.0F},
        {1.0F, 1.0F, 1.0F}));
    REQUIRE_FALSE(intersects_view_frustum(
        *extreme_frustum,
        {-1.0F, -1.0F, -100'000'010.0F},
        {1.0F, 1.0F, -100'000'001.0F}));
}

TEST_CASE(
    "compact deterministic smoke poses exercise terrain chunk culling",
    "[renderer][terrain][culling][frustum][smoke][compact]")
{
    using namespace shark;
    using namespace shark::renderer::detail;

    const auto layout = terrain::build_lod0_chunk_layout(
        terrain::make_deterministic_height_tile(),
        terrain::deterministic_tile_chunk_cell_columns,
        terrain::deterministic_tile_chunk_cell_rows);
    REQUIRE(layout);
    REQUIRE(layout.value().chunks.size() ==
        terrain::deterministic_tile_chunk_count);

    const auto visible_count = [&layout](
                                   const world::Camera& camera,
                                   const float aspect_ratio) {
        const auto matrices = world::build_camera_matrices(
            camera,
            aspect_ratio);
        REQUIRE(matrices);
        const auto frustum = make_view_frustum(
            matrices.value().view_projection);
        REQUIRE(frustum);

        std::size_t visible = 0;
        for (const auto& chunk : layout.value().chunks) {
            visible += static_cast<std::size_t>(
                intersects_view_frustum(
                    *frustum,
                    chunk.bounds.minimum,
                    chunk.bounds.maximum));
        }
        return visible;
    };

    world::Camera initial;
    initial.transform.position = {0.0F, 4.0F, 10.0F};
    initial.transform.pitch_radians = -0.35F;
    REQUIRE(visible_count(initial, 16.0F / 9.0F) == 16);

    auto culling_pose = initial;
    culling_pose.transform.yaw_radians = 1.25F;
    REQUIRE(visible_count(culling_pose, 960.0F / 600.0F) == 5);
}

TEST_CASE(
    "large capacity smoke poses exercise terrain chunk culling",
    "[renderer][terrain][culling][frustum][smoke]")
{
    using namespace shark;
    using namespace shark::renderer::detail;

    const auto layout = terrain::build_lod0_chunk_layout(
        terrain::make_large_capacity_height_tile(),
        terrain::large_capacity_tile_chunk_cell_columns,
        terrain::large_capacity_tile_chunk_cell_rows);
    REQUIRE(layout);
    REQUIRE(layout.value().chunks.size() ==
        terrain::large_capacity_tile_chunk_count);

    const auto visible_count = [&layout](
                                   const world::Camera& camera,
                                   const float aspect_ratio) {
        const auto matrices = world::build_camera_matrices(
            camera,
            aspect_ratio);
        REQUIRE(matrices);
        const auto frustum = make_view_frustum(
            matrices.value().view_projection);
        REQUIRE(frustum);

        std::size_t visible = 0;
        for (const auto& chunk : layout.value().chunks) {
            visible += static_cast<std::size_t>(
                intersects_view_frustum(
                    *frustum,
                    chunk.bounds.minimum,
                    chunk.bounds.maximum));
        }
        return visible;
    };

    world::Camera initial;
    initial.transform.position = {0.0F, 28.0F, 112.0F};
    initial.transform.pitch_radians = -0.25F;
    initial.lens.far_plane = 1'500.0F;
    REQUIRE(visible_count(initial, 16.0F / 9.0F) == 93);
    REQUIRE(visible_count(initial, 960.0F / 600.0F) == 93);

    auto culling_pose = initial;
    culling_pose.transform.yaw_radians = 1.25F;
    REQUIRE(visible_count(culling_pose, 960.0F / 600.0F) == 71);
}
