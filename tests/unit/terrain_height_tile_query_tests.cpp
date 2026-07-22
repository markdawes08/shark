#include <shark/terrain/height_tile.hpp>

#include <shark/core/error.hpp>
#include <shark/core/math.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

constexpr float comparison_margin = 0.00001F;

[[nodiscard]] shark::terrain::HeightTile make_tile(
    const std::uint32_t columns,
    const std::uint32_t rows,
    const float spacing,
    const shark::math::Float3 origin,
    std::vector<float> heights)
{
    return shark::terrain::HeightTile{
        .sample_columns = columns,
        .sample_rows = rows,
        .sample_spacing = spacing,
        .origin = origin,
        .height_offsets = std::move(heights),
    };
}

[[nodiscard]] shark::terrain::HeightTile make_twisted_tile()
{
    // Only v11 is raised. The two canonical triangles are therefore
    // intentionally non-coplanar and have distinguishable exact normals.
    return make_tile(
        2,
        2,
        1.0F,
        {},
        {
            0.0F, 0.0F,
            0.0F, 1.0F,
        });
}

[[nodiscard]] shark::terrain::HeightTile make_ramp_tile()
{
    // height_offset = 2 * x + 3 * z over a 3x3 sample grid.
    return make_tile(
        3,
        3,
        1.0F,
        {0.0F, 5.0F, 0.0F},
        {
            0.0F, 2.0F, 4.0F,
            3.0F, 5.0F, 7.0F,
            6.0F, 8.0F, 10.0F,
        });
}

[[nodiscard]] shark::terrain::HeightTile make_flat_tile()
{
    return make_tile(
        3,
        2,
        1.0F,
        {},
        {
            0.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 0.0F,
        });
}

void require_float3(
    const shark::math::Float3 actual,
    const shark::math::Float3 expected,
    const float margin = comparison_margin)
{
    REQUIRE(actual.x == Catch::Approx(expected.x).margin(margin));
    REQUIRE(actual.y == Catch::Approx(expected.y).margin(margin));
    REQUIRE(actual.z == Catch::Approx(expected.z).margin(margin));
}

void require_hit_matches_sample(
    const shark::terrain::HeightTileRayHit& hit,
    const shark::terrain::HeightTileSurfaceSample& sample)
{
    require_float3(hit.position, sample.position);
    require_float3(hit.normal, sample.normal);
    REQUIRE(hit.cell_x == sample.cell_x);
    REQUIRE(hit.cell_z == sample.cell_z);
    REQUIRE(hit.triangle == sample.triangle);
    require_float3(hit.barycentrics, sample.barycentrics);
}

void require_hit_on_ray(
    const shark::terrain::HeightTileRayHit& hit,
    const shark::terrain::Ray3 ray,
    const float margin = comparison_margin)
{
    const auto direction_length = std::sqrt(
        ray.direction.x * ray.direction.x +
        ray.direction.y * ray.direction.y +
        ray.direction.z * ray.direction.z);
    const shark::math::Float3 expected{
        ray.origin.x +
            ray.direction.x / direction_length * hit.distance,
        ray.origin.y +
            ray.direction.y / direction_length * hit.distance,
        ray.origin.z +
            ray.direction.z / direction_length * hit.distance,
    };
    require_float3(hit.position, expected, margin);
}

void require_invalid_ray(
    const shark::terrain::HeightTileSurface& surface,
    const shark::terrain::Ray3 ray,
    const float maximum_distance)
{
    const auto bounds_result =
        surface.intersect_bounds(ray, maximum_distance);
    REQUIRE_FALSE(bounds_result);
    REQUIRE(bounds_result.error().code() ==
        shark::core::ErrorCode::invalid_argument);

    const auto raycast_result =
        surface.raycast_lod0(ray, maximum_distance);
    REQUIRE_FALSE(raycast_result);
    REQUIRE(raycast_result.error().code() ==
        shark::core::ErrorCode::invalid_argument);
}

void require_segment_hit_consistent(
    const shark::terrain::HeightTileSegmentClosestPoint& hit,
    const shark::terrain::Segment3 segment,
    const float margin = comparison_margin)
{
    REQUIRE(hit.segment_parameter >= 0.0F);
    REQUIRE(hit.segment_parameter <= 1.0F);
    const shark::math::Float3 expected_segment_position{
        segment.first_endpoint.x +
            (segment.second_endpoint.x -
             segment.first_endpoint.x) *
                hit.segment_parameter,
        segment.first_endpoint.y +
            (segment.second_endpoint.y -
             segment.first_endpoint.y) *
                hit.segment_parameter,
        segment.first_endpoint.z +
            (segment.second_endpoint.z -
             segment.first_endpoint.z) *
                hit.segment_parameter,
    };
    require_float3(
        hit.segment_position,
        expected_segment_position,
        margin);

    const auto offset_x =
        hit.segment_position.x - hit.surface.position.x;
    const auto offset_y =
        hit.segment_position.y - hit.surface.position.y;
    const auto offset_z =
        hit.segment_position.z - hit.surface.position.z;
    const auto expected_distance = std::sqrt(
        offset_x * offset_x +
        offset_y * offset_y +
        offset_z * offset_z);
    REQUIRE(
        hit.distance ==
        Catch::Approx(expected_distance).margin(margin));
    REQUIRE(hit.distance >= 0.0F);

    const auto barycentric_sum =
        hit.surface.barycentrics.x +
        hit.surface.barycentrics.y +
        hit.surface.barycentrics.z;
    REQUIRE(
        barycentric_sum ==
        Catch::Approx(1.0F).margin(margin));
    REQUIRE(hit.surface.barycentrics.x >= 0.0F);
    REQUIRE(hit.surface.barycentrics.y >= 0.0F);
    REQUIRE(hit.surface.barycentrics.z >= 0.0F);
}

void require_segment_hit_matches_sample(
    const shark::terrain::HeightTileSegmentClosestPoint& hit,
    const shark::terrain::HeightTileSurfaceSample& sample)
{
    require_float3(hit.surface.position, sample.position);
    require_float3(hit.surface.normal, sample.normal);
    REQUIRE(hit.surface.cell_x == sample.cell_x);
    REQUIRE(hit.surface.cell_z == sample.cell_z);
    REQUIRE(hit.surface.triangle == sample.triangle);
    require_float3(
        hit.surface.barycentrics,
        sample.barycentrics);
}

void require_invalid_segment(
    const shark::terrain::HeightTileSurface& surface,
    const shark::terrain::Segment3 segment,
    const float maximum_distance)
{
    const auto result =
        surface.closest_lod0_point_to_segment(
            segment,
            maximum_distance);
    REQUIRE_FALSE(result);
    REQUIRE(
        result.error().code() ==
        shark::core::ErrorCode::invalid_argument);
}

void require_triangle_geometry(
    const shark::terrain::HeightTileTriangleGeometry& actual,
    const std::uint32_t expected_cell_x,
    const std::uint32_t expected_cell_z,
    const shark::terrain::HeightTileTriangle expected_triangle,
    const std::array<shark::math::Float3, 3>& expected_positions,
    const shark::math::Float3 expected_normal)
{
    REQUIRE(actual.cell_x == expected_cell_x);
    REQUIRE(actual.cell_z == expected_cell_z);
    REQUIRE(actual.triangle == expected_triangle);
    for (std::size_t vertex = 0U;
         vertex < expected_positions.size();
         ++vertex) {
        CAPTURE(vertex);
        require_float3(
            actual.positions[vertex],
            expected_positions[vertex]);
    }
    require_float3(actual.normal, expected_normal);
}

void require_invalid_triangle_bounds(
    const shark::terrain::HeightTileSurface& surface,
    const shark::terrain::Bounds3 query_bounds)
{
    const auto result =
        surface.lod0_triangles_overlapping_bounds(query_bounds);
    REQUIRE_FALSE(result);
    REQUIRE(
        result.error().code() ==
        shark::core::ErrorCode::invalid_argument);
}

} // namespace

TEST_CASE(
    "height-tile surfaces reject malformed canonical storage",
    "[terrain][height-tile][query][validation]")
{
    using namespace shark;

    SECTION("sample dimensions")
    {
        auto tile = make_twisted_tile();
        tile.sample_columns = 1;
        const auto result =
            terrain::HeightTileSurface::create(std::move(tile));
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("sample spacing")
    {
        auto tile = make_twisted_tile();
        tile.sample_spacing = 0.0F;
        const auto zero_result =
            terrain::HeightTileSurface::create(std::move(tile));
        REQUIRE_FALSE(zero_result);
        REQUIRE(zero_result.error().code() ==
            core::ErrorCode::invalid_argument);

        tile = make_twisted_tile();
        tile.sample_spacing =
            std::numeric_limits<float>::quiet_NaN();
        const auto nonfinite_result =
            terrain::HeightTileSurface::create(std::move(tile));
        REQUIRE_FALSE(nonfinite_result);
        REQUIRE(nonfinite_result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("origin")
    {
        auto tile = make_twisted_tile();
        tile.origin.z = std::numeric_limits<float>::infinity();
        const auto result =
            terrain::HeightTileSurface::create(std::move(tile));
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("sample count")
    {
        auto tile = make_twisted_tile();
        tile.height_offsets.pop_back();
        const auto result =
            terrain::HeightTileSurface::create(std::move(tile));
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("height values")
    {
        auto tile = make_twisted_tile();
        tile.height_offsets[2] =
            std::numeric_limits<float>::quiet_NaN();
        const auto result =
            terrain::HeightTileSurface::create(std::move(tile));
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("generated coordinates")
    {
        auto tile = make_twisted_tile();
        tile.origin.x = std::numeric_limits<float>::max();
        tile.sample_spacing = std::numeric_limits<float>::max();
        const auto result =
            terrain::HeightTileSurface::create(std::move(tile));
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    SECTION("collapsed horizontal coordinates")
    {
        auto tile = make_twisted_tile();
        tile.origin.x = 10'000'000'000.0F;
        tile.sample_spacing = 1.0F;
        const auto result =
            terrain::HeightTileSurface::create(std::move(tile));
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }
}

TEST_CASE(
    "height-tile surfaces own canonical data and derive exact bounds",
    "[terrain][height-tile][query][bounds]")
{
    using namespace shark;

    const auto expected_tile =
        terrain::make_deterministic_height_tile();
    auto surface_result =
        terrain::HeightTileSurface::create(expected_tile);
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();

    REQUIRE(surface.tile() == expected_tile);
    REQUIRE(surface.bounds() ==
        terrain::deterministic_tile_expected_bounds);

    const auto mesh_result =
        terrain::build_lod0_mesh(surface.tile());
    REQUIRE(mesh_result);
    REQUIRE(mesh_result.value().bounds == surface.bounds());

    auto ramp_result =
        terrain::HeightTileSurface::create(make_ramp_tile());
    REQUIRE(ramp_result);
    const auto ramp = std::move(ramp_result).value();
    REQUIRE((ramp.bounds() == terrain::Bounds3{
        {0.0F, 5.0F, 0.0F},
        {2.0F, 15.0F, 2.0F},
    }));
}

TEST_CASE(
    "canonical terrain queries do not inherit render index limits",
    "[terrain][height-tile][query][ownership]")
{
    using namespace shark;

    constexpr std::uint32_t sample_count = 257;
    auto tile = make_tile(
        sample_count,
        sample_count,
        1.0F,
        {},
        std::vector<float>(
            static_cast<std::size_t>(sample_count) *
                static_cast<std::size_t>(sample_count),
            0.0F));

    const auto mesh_result = terrain::build_lod0_mesh(tile);
    REQUIRE_FALSE(mesh_result);
    REQUIRE(mesh_result.error().code() ==
        core::ErrorCode::unsupported);

    auto surface_result =
        terrain::HeightTileSurface::create(std::move(tile));
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    REQUIRE((surface.bounds() == terrain::Bounds3{
        {0.0F, 0.0F, 0.0F},
        {256.0F, 0.0F, 256.0F},
    }));

    const auto maximum =
        surface.sample_lod0_surface(256.0F, 256.0F);
    REQUIRE(maximum);
    REQUIRE(maximum->cell_x == 255);
    REQUIRE(maximum->cell_z == 255);
    require_float3(maximum->normal, {0.0F, 1.0F, 0.0F});
}

TEST_CASE(
    "LOD0 sampling uses the float-rounded rendered grid at large origins",
    "[terrain][height-tile][query][precision][surface]")
{
    using namespace shark;

    auto tile = make_tile(
        4,
        2,
        1'000.0F,
        {10'000'000'000.0F, 0.0F, 0.0F},
        {
            0.0F, 10.0F, 20.0F, 30.0F,
            0.0F, 10.0F, 20.0F, 30.0F,
        });
    const auto mesh_result = terrain::build_lod0_mesh(tile);
    REQUIRE(mesh_result);
    const auto rendered_vertex = mesh_result.value().positions[1];
    REQUIRE(rendered_vertex.x - tile.origin.x == 1'024.0F);

    auto surface_result =
        terrain::HeightTileSurface::create(std::move(tile));
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    const auto sample = surface.sample_lod0_surface(
        rendered_vertex.x,
        rendered_vertex.z);
    REQUIRE(sample);
    require_float3(sample->position, rendered_vertex);
    REQUIRE(sample->cell_x == 1);
    REQUIRE(sample->cell_z == 0);
    REQUIRE(sample->triangle ==
        terrain::HeightTileTriangle::v00_v01_v11);
    require_float3(sample->barycentrics, {1.0F, 0.0F, 0.0F});
}

TEST_CASE(
    "LOD0 sampling follows both fixed triangles and the diagonal tie",
    "[terrain][height-tile][query][surface]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_twisted_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    constexpr auto inverse_sqrt_two = 0.7071067811865475F;

    const auto first =
        surface.sample_lod0_surface(0.25F, 0.75F);
    REQUIRE(first);
    require_float3(first->position, {0.25F, 0.25F, 0.75F});
    require_float3(
        first->normal,
        {-inverse_sqrt_two, inverse_sqrt_two, 0.0F});
    REQUIRE(first->cell_x == 0);
    REQUIRE(first->cell_z == 0);
    REQUIRE(first->triangle ==
        terrain::HeightTileTriangle::v00_v01_v11);
    require_float3(first->barycentrics, {0.25F, 0.50F, 0.25F});

    const auto second =
        surface.sample_lod0_surface(0.75F, 0.25F);
    REQUIRE(second);
    require_float3(second->position, {0.75F, 0.25F, 0.25F});
    require_float3(
        second->normal,
        {0.0F, inverse_sqrt_two, -inverse_sqrt_two});
    REQUIRE(second->cell_x == 0);
    REQUIRE(second->cell_z == 0);
    REQUIRE(second->triangle ==
        terrain::HeightTileTriangle::v00_v11_v10);
    require_float3(second->barycentrics, {0.25F, 0.25F, 0.50F});

    const auto diagonal =
        surface.sample_lod0_surface(0.50F, 0.50F);
    REQUIRE(diagonal);
    REQUIRE(diagonal->triangle ==
        terrain::HeightTileTriangle::v00_v01_v11);
    require_float3(
        diagonal->barycentrics,
        {0.50F, 0.0F, 0.50F});
    require_float3(
        diagonal->normal,
        {-inverse_sqrt_two, inverse_sqrt_two, 0.0F});

    const auto height =
        surface.sample_lod0_height(0.25F, 0.75F);
    const auto normal =
        surface.sample_lod0_normal(0.25F, 0.75F);
    REQUIRE(height);
    REQUIRE(normal);
    REQUIRE(*height == Catch::Approx(first->position.y));
    require_float3(*normal, first->normal);
}

TEST_CASE(
    "flat and ramp queries return exact planar height and normals",
    "[terrain][height-tile][query][surface]")
{
    using namespace shark;

    auto flat_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(flat_result);
    const auto flat = std::move(flat_result).value();
    const auto flat_sample =
        flat.sample_lod0_surface(1.25F, 0.75F);
    REQUIRE(flat_sample);
    require_float3(
        flat_sample->position,
        {1.25F, 0.0F, 0.75F});
    require_float3(flat_sample->normal, {0.0F, 1.0F, 0.0F});

    auto ramp_result =
        terrain::HeightTileSurface::create(make_ramp_tile());
    REQUIRE(ramp_result);
    const auto ramp = std::move(ramp_result).value();
    constexpr auto inverse_sqrt_fourteen = 0.2672612419124244F;
    const math::Float3 expected_normal{
        -2.0F * inverse_sqrt_fourteen,
        inverse_sqrt_fourteen,
        -3.0F * inverse_sqrt_fourteen,
    };

    const auto first =
        ramp.sample_lod0_surface(0.25F, 0.75F);
    const auto second =
        ramp.sample_lod0_surface(0.75F, 0.25F);
    REQUIRE(first);
    REQUIRE(second);
    require_float3(first->position, {0.25F, 7.75F, 0.75F});
    require_float3(second->position, {0.75F, 7.25F, 0.25F});
    require_float3(first->normal, expected_normal);
    require_float3(second->normal, expected_normal);
}

TEST_CASE(
    "LOD0 sampling includes maximum edges and rejects invalid points",
    "[terrain][height-tile][query][boundaries]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_ramp_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();

    const auto minimum =
        surface.sample_lod0_surface(0.0F, 0.0F);
    const auto maximum =
        surface.sample_lod0_surface(2.0F, 2.0F);
    const auto maximum_x =
        surface.sample_lod0_surface(2.0F, 0.0F);
    const auto maximum_z =
        surface.sample_lod0_surface(0.0F, 2.0F);
    REQUIRE(minimum);
    REQUIRE(maximum);
    REQUIRE(maximum_x);
    REQUIRE(maximum_z);
    REQUIRE(minimum->cell_x == 0);
    REQUIRE(minimum->cell_z == 0);
    REQUIRE(maximum->cell_x == 1);
    REQUIRE(maximum->cell_z == 1);
    REQUIRE(maximum->position.y == Catch::Approx(15.0F));
    REQUIRE(maximum_x->position.y == Catch::Approx(9.0F));
    REQUIRE(maximum_z->position.y == Catch::Approx(11.0F));

    const auto internal_x_edge =
        surface.sample_lod0_surface(1.0F, 0.25F);
    const auto internal_z_edge =
        surface.sample_lod0_surface(0.25F, 1.0F);
    REQUIRE(internal_x_edge);
    REQUIRE(internal_z_edge);
    REQUIRE(internal_x_edge->cell_x == 1);
    REQUIRE(internal_x_edge->cell_z == 0);
    REQUIRE(internal_z_edge->cell_x == 0);
    REQUIRE(internal_z_edge->cell_z == 1);

    const auto below_minimum =
        std::nextafter(0.0F, -std::numeric_limits<float>::infinity());
    const auto above_maximum =
        std::nextafter(2.0F, std::numeric_limits<float>::infinity());
    REQUIRE_FALSE(surface.sample_lod0_surface(below_minimum, 1.0F));
    REQUIRE_FALSE(surface.sample_lod0_surface(above_maximum, 1.0F));
    REQUIRE_FALSE(surface.sample_lod0_surface(1.0F, below_minimum));
    REQUIRE_FALSE(surface.sample_lod0_surface(1.0F, above_maximum));
    REQUIRE_FALSE(surface.sample_lod0_height(
        std::numeric_limits<float>::quiet_NaN(),
        1.0F));
    REQUIRE_FALSE(surface.sample_lod0_normal(
        1.0F,
        std::numeric_limits<float>::infinity()));
}

TEST_CASE(
    "terrain bounds intersection returns clipped metric slab intervals",
    "[terrain][height-tile][query][bounds][ray]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_ramp_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();

    const terrain::Ray3 downward{
        {1.0F, 25.0F, 1.0F},
        {0.0F, -2.0F, 0.0F},
    };
    const auto downward_result =
        surface.intersect_bounds(downward, 30.0F);
    REQUIRE(downward_result);
    REQUIRE(downward_result.value());
    REQUIRE(downward_result.value()->entry_distance ==
        Catch::Approx(10.0F));
    REQUIRE(downward_result.value()->exit_distance ==
        Catch::Approx(20.0F));

    const terrain::Ray3 inside{
        {1.0F, 10.0F, 1.0F},
        {2.0F, 0.0F, 0.0F},
    };
    const auto inside_result =
        surface.intersect_bounds(inside, 0.5F);
    REQUIRE(inside_result);
    REQUIRE(inside_result.value());
    REQUIRE(inside_result.value()->entry_distance ==
        Catch::Approx(0.0F));
    REQUIRE(inside_result.value()->exit_distance ==
        Catch::Approx(0.5F));

    const auto exact_limit =
        surface.intersect_bounds(downward, 10.0F);
    REQUIRE(exact_limit);
    REQUIRE(exact_limit.value());
    REQUIRE(exact_limit.value()->entry_distance ==
        Catch::Approx(10.0F));
    REQUIRE(exact_limit.value()->exit_distance ==
        Catch::Approx(10.0F));

    const auto short_result =
        surface.intersect_bounds(downward, 9.99F);
    REQUIRE(short_result);
    REQUIRE_FALSE(short_result.value());

    const auto parallel_miss = surface.intersect_bounds(
        terrain::Ray3{
            {3.0F, 25.0F, 1.0F},
            {0.0F, -1.0F, 0.0F},
        },
        30.0F);
    REQUIRE(parallel_miss);
    REQUIRE_FALSE(parallel_miss.value());
}

TEST_CASE(
    "LOD0 raycasts return metric hits consistent with surface samples",
    "[terrain][height-tile][query][ray][hit]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_twisted_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    const auto expected_sample =
        surface.sample_lod0_surface(0.25F, 0.75F);
    REQUIRE(expected_sample);

    const terrain::Ray3 downward{
        {0.25F, 5.0F, 0.75F},
        {0.0F, -4.0F, 0.0F},
    };
    const auto hit_result =
        surface.raycast_lod0(downward, 10.0F);
    REQUIRE(hit_result);
    REQUIRE(hit_result.value());
    const auto& hit = *hit_result.value();
    REQUIRE(hit.distance == Catch::Approx(4.75F));
    require_hit_matches_sample(hit, *expected_sample);
    require_hit_on_ray(hit, downward);

    const auto exact_limit =
        surface.raycast_lod0(downward, 4.75F);
    REQUIRE(exact_limit);
    REQUIRE(exact_limit.value());
    REQUIRE(exact_limit.value()->distance ==
        Catch::Approx(4.75F));

    const auto short_result =
        surface.raycast_lod0(downward, 4.70F);
    REQUIRE(short_result);
    REQUIRE_FALSE(short_result.value());

    const terrain::Ray3 starts_on_surface{
        expected_sample->position,
        {0.0F, -1.0F, 0.0F},
    };
    const auto surface_result_hit =
        surface.raycast_lod0(starts_on_surface, 1.0F);
    REQUIRE(surface_result_hit);
    REQUIRE(surface_result_hit.value());
    REQUIRE(surface_result_hit.value()->distance ==
        Catch::Approx(0.0F).margin(comparison_margin));
    require_hit_matches_sample(
        *surface_result_hit.value(),
        *expected_sample);
}

TEST_CASE(
    "LOD0 raycasts remain scale relative for small valid cells",
    "[terrain][height-tile][query][ray][precision]")
{
    using namespace shark;

    constexpr float spacing = 5.0e-7F;
    auto surface_result = terrain::HeightTileSurface::create(make_tile(
        2,
        2,
        spacing,
        {},
        {
            0.0F, 0.0F,
            0.0F, 0.0F,
        }));
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    const terrain::Ray3 ray{
        {spacing * 0.5F, 1.0F, spacing * 0.5F},
        {0.0F, -1.0F, 0.0F},
    };
    const auto expected_sample = surface.sample_lod0_surface(
        ray.origin.x,
        ray.origin.z);
    REQUIRE(expected_sample);

    const auto hit_result = surface.raycast_lod0(ray, 2.0F);
    REQUIRE(hit_result);
    REQUIRE(hit_result.value());
    REQUIRE(hit_result.value()->distance ==
        Catch::Approx(1.0F).margin(comparison_margin));
    require_hit_matches_sample(
        *hit_result.value(),
        *expected_sample);
    require_hit_on_ray(*hit_result.value(), ray);
}

TEST_CASE(
    "LOD0 raycasts handle oblique and multiple-hit terrain",
    "[terrain][height-tile][query][ray][nearest]")
{
    using namespace shark;

    auto flat_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(flat_result);
    const auto flat = std::move(flat_result).value();
    const terrain::Ray3 oblique{
        {-0.50F, 1.0F, 0.25F},
        {1.0F, -1.0F, 0.0F},
    };
    const auto oblique_result =
        flat.raycast_lod0(oblique, 3.0F);
    REQUIRE(oblique_result);
    REQUIRE(oblique_result.value());
    REQUIRE(oblique_result.value()->distance ==
        Catch::Approx(std::sqrt(2.0F)).margin(comparison_margin));
    require_float3(
        oblique_result.value()->position,
        {0.50F, 0.0F, 0.25F});
    const auto oblique_sample =
        flat.sample_lod0_surface(0.50F, 0.25F);
    REQUIRE(oblique_sample);
    require_hit_matches_sample(
        *oblique_result.value(),
        *oblique_sample);
    require_hit_on_ray(*oblique_result.value(), oblique);

    auto wave_result = terrain::HeightTileSurface::create(make_tile(
        4,
        2,
        1.0F,
        {},
        {
            0.0F, 2.0F, 0.0F, 2.0F,
            0.0F, 2.0F, 0.0F, 2.0F,
        }));
    REQUIRE(wave_result);
    const auto wave = std::move(wave_result).value();
    const auto nearest_result = wave.raycast_lod0(
        terrain::Ray3{
            {-1.0F, 1.0F, 0.25F},
            {5.0F, 0.0F, 0.0F},
        },
        10.0F);
    REQUIRE(nearest_result);
    REQUIRE(nearest_result.value());
    REQUIRE(nearest_result.value()->distance ==
        Catch::Approx(1.5F).margin(comparison_margin));
    require_float3(
        nearest_result.value()->position,
        {0.50F, 1.0F, 0.25F});
    REQUIRE(nearest_result.value()->cell_x == 0);
    REQUIRE(nearest_result.value()->triangle ==
        terrain::HeightTileTriangle::v00_v11_v10);
    constexpr auto inverse_sqrt_five = 0.4472135954999579F;
    require_float3(
        nearest_result.value()->normal,
        {
            -2.0F * inverse_sqrt_five,
            inverse_sqrt_five,
            0.0F,
        });
    require_hit_on_ray(
        *nearest_result.value(),
        terrain::Ray3{
            {-1.0F, 1.0F, 0.25F},
            {5.0F, 0.0F, 0.0F},
        });
}

TEST_CASE(
    "LOD0 ray ownership matches sampling on shared boundaries and backfaces",
    "[terrain][height-tile][query][ray][ownership]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_ramp_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    constexpr std::array<math::Float3, 3> boundary_points{{
        {0.50F, 0.0F, 0.50F},
        {1.00F, 0.0F, 0.25F},
        {2.00F, 0.0F, 2.00F},
    }};
    for (const auto point : boundary_points) {
        const auto expected_sample =
            surface.sample_lod0_surface(point.x, point.z);
        REQUIRE(expected_sample);
        const terrain::Ray3 ray{
            {point.x, 25.0F, point.z},
            {0.0F, -3.0F, 0.0F},
        };
        const auto hit_result = surface.raycast_lod0(ray, 30.0F);
        REQUIRE(hit_result);
        REQUIRE(hit_result.value());
        require_hit_matches_sample(
            *hit_result.value(),
            *expected_sample);
        require_hit_on_ray(*hit_result.value(), ray);
    }

    const auto expected_backface =
        surface.sample_lod0_surface(0.75F, 0.25F);
    REQUIRE(expected_backface);
    const terrain::Ray3 upward{
        {0.75F, 0.0F, 0.25F},
        {0.0F, 2.0F, 0.0F},
    };
    const auto upward_result =
        surface.raycast_lod0(upward, 10.0F);
    REQUIRE(upward_result);
    REQUIRE(upward_result.value());
    require_hit_matches_sample(
        *upward_result.value(),
        *expected_backface);
    require_hit_on_ray(*upward_result.value(), upward);
}

TEST_CASE(
    "terrain ray queries distinguish valid misses from invalid input",
    "[terrain][height-tile][query][ray][validation]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();

    const std::array valid_misses{
        terrain::Ray3{
            {1.0F, 1.0F, 0.5F},
            {0.0F, 1.0F, 0.0F},
        },
        terrain::Ray3{
            {3.0F, 1.0F, 0.5F},
            {0.0F, -1.0F, 0.0F},
        },
        terrain::Ray3{
            {1.0F, 1.0F, 0.5F},
            {1.0F, 0.0F, 0.0F},
        },
    };
    for (const auto& ray : valid_misses) {
        const auto bounds_result =
            surface.intersect_bounds(ray, 10.0F);
        const auto raycast_result =
            surface.raycast_lod0(ray, 10.0F);
        REQUIRE(bounds_result);
        REQUIRE(raycast_result);
        REQUIRE_FALSE(raycast_result.value());
    }

    require_invalid_ray(
        surface,
        terrain::Ray3{{}, {}},
        1.0F);
    require_invalid_ray(
        surface,
        terrain::Ray3{
            {
                std::numeric_limits<float>::quiet_NaN(),
                0.0F,
                0.0F,
            },
            {0.0F, -1.0F, 0.0F},
        },
        1.0F);
    require_invalid_ray(
        surface,
        terrain::Ray3{
            {},
            {
                0.0F,
                -std::numeric_limits<float>::infinity(),
                0.0F,
            },
        },
        1.0F);

    const terrain::Ray3 valid_ray{
        {1.0F, 1.0F, 0.5F},
        {0.0F, -1.0F, 0.0F},
    };
    require_invalid_ray(surface, valid_ray, 0.0F);
    require_invalid_ray(surface, valid_ray, -1.0F);
    require_invalid_ray(
        surface,
        valid_ray,
        std::numeric_limits<float>::infinity());
    require_invalid_ray(
        surface,
        valid_ray,
        std::numeric_limits<float>::quiet_NaN());
}

TEST_CASE(
    "LOD0 segment queries find face interiors and crossings",
    "[terrain][height-tile][query][segment][face]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();

    const terrain::Segment3 crossing{
        {0.25F, 1.0F, 0.75F},
        {0.25F, -1.0F, 0.75F},
    };
    const auto crossing_result =
        surface.closest_lod0_point_to_segment(
            crossing,
            0.25F);
    REQUIRE(crossing_result);
    REQUIRE(crossing_result.value());
    const auto& crossing_hit = *crossing_result.value();
    REQUIRE(
        crossing_hit.segment_parameter ==
        Catch::Approx(0.5F).margin(0.000001F));
    REQUIRE(
        crossing_hit.distance ==
        Catch::Approx(0.0F).margin(0.000001F));
    require_float3(
        crossing_hit.segment_position,
        {0.25F, 0.0F, 0.75F});
    require_float3(
        crossing_hit.surface.position,
        {0.25F, 0.0F, 0.75F});
    require_float3(
        crossing_hit.surface.normal,
        {0.0F, 1.0F, 0.0F});
    REQUIRE(crossing_hit.surface.cell_x == 0);
    REQUIRE(crossing_hit.surface.cell_z == 0);
    REQUIRE(
        crossing_hit.surface.triangle ==
        terrain::HeightTileTriangle::v00_v01_v11);
    require_float3(
        crossing_hit.surface.barycentrics,
        {0.25F, 0.50F, 0.25F});
    require_segment_hit_consistent(crossing_hit, crossing);

    const terrain::Segment3 endpoint_above_face{
        {0.25F, 2.0F, 0.75F},
        {0.25F, 1.0F, 0.75F},
    };
    const auto endpoint_result =
        surface.closest_lod0_point_to_segment(
            endpoint_above_face,
            1.0F);
    REQUIRE(endpoint_result);
    REQUIRE(endpoint_result.value());
    const auto& endpoint_hit = *endpoint_result.value();
    REQUIRE(endpoint_hit.segment_parameter == 1.0F);
    REQUIRE(endpoint_hit.distance == 1.0F);
    require_float3(
        endpoint_hit.segment_position,
        endpoint_above_face.second_endpoint);
    require_float3(
        endpoint_hit.surface.position,
        {0.25F, 0.0F, 0.75F});
    require_segment_hit_consistent(
        endpoint_hit,
        endpoint_above_face);
}

TEST_CASE(
    "LOD0 segment queries retain deterministic parallel and coplanar ties",
    "[terrain][height-tile][query][segment][parallel][coplanar]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();

    const terrain::Segment3 parallel{
        {0.20F, 2.0F, 0.80F},
        {0.80F, 2.0F, 0.80F},
    };
    const auto parallel_result =
        surface.closest_lod0_point_to_segment(parallel, 2.0F);
    REQUIRE(parallel_result);
    REQUIRE(parallel_result.value());
    const auto& parallel_hit = *parallel_result.value();
    REQUIRE(parallel_hit.segment_parameter == 0.0F);
    REQUIRE(parallel_hit.distance == 2.0F);
    require_float3(
        parallel_hit.segment_position,
        parallel.first_endpoint);
    require_float3(
        parallel_hit.surface.position,
        {0.20F, 0.0F, 0.80F});
    REQUIRE(
        parallel_hit.surface.triangle ==
        terrain::HeightTileTriangle::v00_v01_v11);
    require_float3(
        parallel_hit.surface.barycentrics,
        {0.20F, 0.60F, 0.20F});
    require_segment_hit_consistent(parallel_hit, parallel);

    const terrain::Segment3 coplanar{
        {0.20F, 0.0F, 0.80F},
        {0.80F, 0.0F, 0.80F},
    };
    const auto coplanar_result =
        surface.closest_lod0_point_to_segment(coplanar, 0.10F);
    REQUIRE(coplanar_result);
    REQUIRE(coplanar_result.value());
    const auto& coplanar_hit = *coplanar_result.value();
    REQUIRE(coplanar_hit.segment_parameter == 0.0F);
    REQUIRE(coplanar_hit.distance == 0.0F);
    require_float3(
        coplanar_hit.segment_position,
        coplanar.first_endpoint);
    require_float3(
        coplanar_hit.surface.position,
        coplanar.first_endpoint);
    REQUIRE(
        coplanar_hit.surface.triangle ==
        terrain::HeightTileTriangle::v00_v01_v11);
    require_segment_hit_consistent(coplanar_hit, coplanar);
}

TEST_CASE(
    "LOD0 segment queries select exact edge vertex and endpoint features",
    "[terrain][height-tile][query][segment][edge][vertex][endpoint]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();

    const terrain::Segment3 beside_edge{
        {-1.0F, -1.0F, 0.50F},
        {-1.0F, 1.0F, 0.50F},
    };
    const auto edge_result =
        surface.closest_lod0_point_to_segment(
            beside_edge,
            1.0F);
    REQUIRE(edge_result);
    REQUIRE(edge_result.value());
    const auto& edge_hit = *edge_result.value();
    REQUIRE(
        edge_hit.segment_parameter ==
        Catch::Approx(0.5F).margin(0.000001F));
    REQUIRE(edge_hit.distance == 1.0F);
    require_float3(
        edge_hit.segment_position,
        {-1.0F, 0.0F, 0.50F});
    require_float3(
        edge_hit.surface.position,
        {0.0F, 0.0F, 0.50F});
    REQUIRE(
        edge_hit.surface.triangle ==
        terrain::HeightTileTriangle::v00_v01_v11);
    require_float3(
        edge_hit.surface.barycentrics,
        {0.50F, 0.50F, 0.0F});
    require_segment_hit_consistent(edge_hit, beside_edge);

    const terrain::Segment3 beyond_vertex{
        {-1.0F, 0.0F, -1.0F},
        {-2.0F, 0.0F, -1.0F},
    };
    const auto vertex_result =
        surface.closest_lod0_point_to_segment(
            beyond_vertex,
            2.0F);
    REQUIRE(vertex_result);
    REQUIRE(vertex_result.value());
    const auto& vertex_hit = *vertex_result.value();
    REQUIRE(vertex_hit.segment_parameter == 0.0F);
    REQUIRE(
        vertex_hit.distance ==
        Catch::Approx(std::sqrt(2.0F))
            .margin(0.000001F));
    require_float3(
        vertex_hit.segment_position,
        beyond_vertex.first_endpoint);
    require_float3(vertex_hit.surface.position, {});
    REQUIRE(
        vertex_hit.surface.triangle ==
        terrain::HeightTileTriangle::v00_v01_v11);
    require_float3(
        vertex_hit.surface.barycentrics,
        {1.0F, 0.0F, 0.0F});
    require_segment_hit_consistent(vertex_hit, beyond_vertex);
}

TEST_CASE(
    "LOD0 segment queries accept zero-length segments",
    "[terrain][height-tile][query][segment][zero]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    const terrain::Segment3 point_segment{
        {0.25F, 1.0F, 0.75F},
        {0.25F, 1.0F, 0.75F},
    };

    const auto result =
        surface.closest_lod0_point_to_segment(
            point_segment,
            1.0F);
    REQUIRE(result);
    REQUIRE(result.value());
    const auto& hit = *result.value();
    REQUIRE(hit.segment_parameter == 0.0F);
    REQUIRE(hit.distance == 1.0F);
    require_float3(hit.segment_position, point_segment.first_endpoint);
    const auto expected_sample =
        surface.sample_lod0_surface(0.25F, 0.75F);
    REQUIRE(expected_sample);
    require_segment_hit_matches_sample(hit, *expected_sample);
    require_segment_hit_consistent(hit, point_segment);
}

TEST_CASE(
    "LOD0 segment ownership is stable across cells diagonals and maximum edges",
    "[terrain][height-tile][query][segment][ownership]")
{
    using namespace shark;

    auto flat_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(flat_result);
    const auto flat = std::move(flat_result).value();
    constexpr std::array<math::Float3, 2> flat_points{{
        {1.50F, 0.0F, 0.25F},
        {2.00F, 0.0F, 1.00F},
    }};
    for (const auto point : flat_points) {
        const auto expected_sample =
            flat.sample_lod0_surface(point.x, point.z);
        REQUIRE(expected_sample);
        const terrain::Segment3 segment{
            {
                point.x,
                expected_sample->position.y + 1.0F,
                point.z,
            },
            {
                point.x,
                expected_sample->position.y - 1.0F,
                point.z,
            },
        };
        const auto result =
            flat.closest_lod0_point_to_segment(segment, 0.25F);
        REQUIRE(result);
        REQUIRE(result.value());
        const auto& hit = *result.value();
        REQUIRE(
            hit.segment_parameter ==
            Catch::Approx(0.5F).margin(0.000001F));
        REQUIRE(
            hit.distance ==
            Catch::Approx(0.0F).margin(0.000001F));
        require_segment_hit_matches_sample(hit, *expected_sample);
        require_segment_hit_consistent(hit, segment);
    }

    // The internal X=1 grid line belongs to both adjacent cells. The segment
    // query's documented row-major tie keeps cell 0, whereas point sampling
    // independently selects cell 1 from its positive-side coordinate.
    const terrain::Segment3 shared_cell_edge{
        {1.00F, 1.0F, 0.25F},
        {1.00F, -1.0F, 0.25F},
    };
    const auto shared_result =
        flat.closest_lod0_point_to_segment(
            shared_cell_edge,
            0.25F);
    REQUIRE(shared_result);
    REQUIRE(shared_result.value());
    const auto& shared_hit = *shared_result.value();
    REQUIRE(shared_hit.surface.cell_x == 0);
    REQUIRE(shared_hit.surface.cell_z == 0);
    REQUIRE(
        shared_hit.surface.triangle ==
        terrain::HeightTileTriangle::v00_v11_v10);
    require_float3(
        shared_hit.surface.position,
        {1.00F, 0.0F, 0.25F});
    require_float3(
        shared_hit.surface.barycentrics,
        {0.0F, 0.25F, 0.75F});
    require_segment_hit_consistent(
        shared_hit,
        shared_cell_edge);

    auto twisted_result =
        terrain::HeightTileSurface::create(make_twisted_tile());
    REQUIRE(twisted_result);
    const auto twisted = std::move(twisted_result).value();
    const auto diagonal_sample =
        twisted.sample_lod0_surface(0.50F, 0.50F);
    REQUIRE(diagonal_sample);
    REQUIRE(
        diagonal_sample->triangle ==
        terrain::HeightTileTriangle::v00_v01_v11);
    const terrain::Segment3 diagonal{
        {0.50F, diagonal_sample->position.y + 1.0F, 0.50F},
        {0.50F, diagonal_sample->position.y - 1.0F, 0.50F},
    };
    const auto diagonal_result =
        twisted.closest_lod0_point_to_segment(
            diagonal,
            0.25F);
    REQUIRE(diagonal_result);
    REQUIRE(diagonal_result.value());
    require_segment_hit_matches_sample(
        *diagonal_result.value(),
        *diagonal_sample);
    require_segment_hit_consistent(
        *diagonal_result.value(),
        diagonal);
}

TEST_CASE(
    "terrain segment queries distinguish misses from invalid input",
    "[terrain][height-tile][query][segment][validation]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();

    const std::array valid_misses{
        std::pair{
            terrain::Segment3{
                {10.0F, 0.0F, 0.50F},
                {11.0F, 0.0F, 0.50F},
            },
            1.0F,
        },
        std::pair{
            terrain::Segment3{
                {0.20F, 2.0F, 0.80F},
                {0.80F, 2.0F, 0.80F},
            },
            std::nextafter(2.0F, 0.0F),
        },
        std::pair{
            terrain::Segment3{
                {0.50F, -2.0F, 0.50F},
                {1.50F, -2.0F, 0.50F},
            },
            1.0F,
        },
    };
    for (const auto& [segment, maximum_distance] : valid_misses) {
        const auto result =
            surface.closest_lod0_point_to_segment(
                segment,
                maximum_distance);
        REQUIRE(result);
        REQUIRE_FALSE(result.value());
    }

    const terrain::Segment3 valid_segment{
        {0.25F, 1.0F, 0.75F},
        {0.25F, -1.0F, 0.75F},
    };
    constexpr auto nan =
        std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity =
        std::numeric_limits<float>::infinity();
    require_invalid_segment(
        surface,
        terrain::Segment3{{nan, 0.0F, 0.0F}, {}},
        1.0F);
    require_invalid_segment(
        surface,
        terrain::Segment3{{}, {0.0F, infinity, 0.0F}},
        1.0F);
    require_invalid_segment(surface, valid_segment, 0.0F);
    require_invalid_segment(surface, valid_segment, -1.0F);
    require_invalid_segment(surface, valid_segment, infinity);
    require_invalid_segment(surface, valid_segment, nan);
}

TEST_CASE(
    "terrain segment closest results are exactly repeatable",
    "[terrain][height-tile][query][segment][determinism]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_twisted_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    const auto tile_before = surface.tile();
    const auto bounds_before = surface.bounds();
    const terrain::Segment3 diagonal{
        {0.50F, 1.50F, 0.50F},
        {0.50F, -0.50F, 0.50F},
    };
    const auto baseline =
        surface.closest_lod0_point_to_segment(
            diagonal,
            0.25F);
    REQUIRE(baseline);
    REQUIRE(baseline.value());

    constexpr std::size_t repeat_count = 64;
    for (std::size_t repeat = 0;
         repeat < repeat_count;
         ++repeat) {
        CAPTURE(repeat);
        const auto repeated =
            surface.closest_lod0_point_to_segment(
                diagonal,
                0.25F);
        REQUIRE(repeated);
        REQUIRE(repeated.value() == baseline.value());
    }
    REQUIRE(surface.tile() == tile_before);
    REQUIRE(surface.bounds() == bounds_before);
}

TEST_CASE(
    "terrain triangle bounds queries return canonical triangle geometry",
    "[terrain][height-tile][query][triangle-bounds]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_twisted_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();

    const auto result =
        surface.lod0_triangles_overlapping_bounds(
            terrain::Bounds3{
                .minimum = {0.0F, 0.0F, 0.0F},
                .maximum = {1.0F, 1.0F, 1.0F},
            });
    REQUIRE(result);
    REQUIRE(result.value().size() == 2U);

    const auto inverse_sqrt_two = 1.0F / std::sqrt(2.0F);
    require_triangle_geometry(
        result.value()[0],
        0U,
        0U,
        terrain::HeightTileTriangle::v00_v01_v11,
        std::array<math::Float3, 3>{
            math::Float3{0.0F, 0.0F, 0.0F},
            math::Float3{0.0F, 0.0F, 1.0F},
            math::Float3{1.0F, 1.0F, 1.0F},
        },
        {-inverse_sqrt_two, inverse_sqrt_two, 0.0F});
    require_triangle_geometry(
        result.value()[1],
        0U,
        0U,
        terrain::HeightTileTriangle::v00_v11_v10,
        std::array<math::Float3, 3>{
            math::Float3{0.0F, 0.0F, 0.0F},
            math::Float3{1.0F, 1.0F, 1.0F},
            math::Float3{1.0F, 0.0F, 0.0F},
        },
        {0.0F, inverse_sqrt_two, -inverse_sqrt_two});
}

TEST_CASE(
    "terrain triangle bounds queries use stable row-major cell order",
    "[terrain][height-tile][query][triangle-bounds]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_ramp_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    const auto result =
        surface.lod0_triangles_overlapping_bounds(surface.bounds());
    REQUIRE(result);
    REQUIRE(result.value().size() == 8U);

    constexpr std::array expected_cells{
        std::pair{0U, 0U},
        std::pair{1U, 0U},
        std::pair{0U, 1U},
        std::pair{1U, 1U},
    };
    const auto expected_height = [](const float x, const float z) {
        return 5.0F + 2.0F * x + 3.0F * z;
    };
    const auto inverse_normal_length = 1.0F / std::sqrt(14.0F);
    const math::Float3 expected_normal{
        -2.0F * inverse_normal_length,
        inverse_normal_length,
        -3.0F * inverse_normal_length,
    };
    for (std::size_t cell_index = 0U;
         cell_index < expected_cells.size();
         ++cell_index) {
        CAPTURE(cell_index);
        const auto [cell_x, cell_z] = expected_cells[cell_index];
        const auto x = static_cast<float>(cell_x);
        const auto z = static_cast<float>(cell_z);
        const math::Float3 v00{x, expected_height(x, z), z};
        const math::Float3 v01{
            x,
            expected_height(x, z + 1.0F),
            z + 1.0F,
        };
        const math::Float3 v10{
            x + 1.0F,
            expected_height(x + 1.0F, z),
            z,
        };
        const math::Float3 v11{
            x + 1.0F,
            expected_height(x + 1.0F, z + 1.0F),
            z + 1.0F,
        };

        require_triangle_geometry(
            result.value()[cell_index * 2U],
            cell_x,
            cell_z,
            terrain::HeightTileTriangle::v00_v01_v11,
            std::array<math::Float3, 3>{v00, v01, v11},
            expected_normal);
        require_triangle_geometry(
            result.value()[cell_index * 2U + 1U],
            cell_x,
            cell_z,
            terrain::HeightTileTriangle::v00_v11_v10,
            std::array<math::Float3, 3>{v00, v11, v10},
            expected_normal);
    }
}

TEST_CASE(
    "terrain triangle bounds queries include touching boundaries",
    "[terrain][height-tile][query][triangle-bounds]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();

    SECTION("interior point")
    {
        const auto result =
            surface.lod0_triangles_overlapping_bounds(
                terrain::Bounds3{
                    .minimum = {0.25F, 0.0F, 0.50F},
                    .maximum = {0.25F, 0.0F, 0.50F},
                });
        REQUIRE(result);
        REQUIRE(result.value().size() == 2U);
        REQUIRE(result.value()[0].cell_x == 0U);
        REQUIRE(result.value()[1].cell_x == 0U);
    }

    SECTION("shared cell edge")
    {
        const auto result =
            surface.lod0_triangles_overlapping_bounds(
                terrain::Bounds3{
                    .minimum = {1.0F, 0.0F, 0.50F},
                    .maximum = {1.0F, 0.0F, 0.50F},
                });
        REQUIRE(result);
        REQUIRE(result.value().size() == 4U);
        REQUIRE(result.value()[0].cell_x == 0U);
        REQUIRE(result.value()[1].cell_x == 0U);
        REQUIRE(result.value()[2].cell_x == 1U);
        REQUIRE(result.value()[3].cell_x == 1U);
    }

    SECTION("maximum tile edge")
    {
        const auto result =
            surface.lod0_triangles_overlapping_bounds(
                terrain::Bounds3{
                    .minimum = {2.0F, 0.0F, 0.50F},
                    .maximum = {2.0F, 0.0F, 0.50F},
                });
        REQUIRE(result);
        REQUIRE(result.value().size() == 2U);
        REQUIRE(result.value()[0].cell_x == 1U);
        REQUIRE(result.value()[1].cell_x == 1U);
    }
}

TEST_CASE(
    "terrain triangle bounds queries filter a multi-cell tile",
    "[terrain][height-tile][query][triangle-bounds]")
{
    using namespace shark;

    auto surface_result = terrain::HeightTileSurface::create(
        make_tile(
            4U,
            4U,
            1.0F,
            {},
            std::vector<float>(16U, 0.0F)));
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    const auto result =
        surface.lod0_triangles_overlapping_bounds(
            terrain::Bounds3{
                .minimum = {0.25F, 0.0F, 1.25F},
                .maximum = {1.75F, 0.0F, 1.75F},
            });
    REQUIRE(result);
    REQUIRE(result.value().size() == 4U);

    constexpr std::array expected_cells{
        std::pair{0U, 1U},
        std::pair{0U, 1U},
        std::pair{1U, 1U},
        std::pair{1U, 1U},
    };
    constexpr std::array expected_triangles{
        terrain::HeightTileTriangle::v00_v01_v11,
        terrain::HeightTileTriangle::v00_v11_v10,
        terrain::HeightTileTriangle::v00_v01_v11,
        terrain::HeightTileTriangle::v00_v11_v10,
    };
    for (std::size_t triangle = 0U;
         triangle < result.value().size();
         ++triangle) {
        CAPTURE(triangle);
        REQUIRE(
            result.value()[triangle].cell_x ==
            expected_cells[triangle].first);
        REQUIRE(
            result.value()[triangle].cell_z ==
            expected_cells[triangle].second);
        REQUIRE(
            result.value()[triangle].triangle ==
            expected_triangles[triangle]);
    }
}

TEST_CASE(
    "terrain triangle bounds queries return empty for valid misses",
    "[terrain][height-tile][query][triangle-bounds]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    constexpr std::array misses{
        terrain::Bounds3{
            .minimum = {-2.0F, -1.0F, 0.25F},
            .maximum = {-1.0F, 1.0F, 0.75F},
        },
        terrain::Bounds3{
            .minimum = {3.0F, -1.0F, 0.25F},
            .maximum = {4.0F, 1.0F, 0.75F},
        },
        terrain::Bounds3{
            .minimum = {0.25F, -1.0F, 2.0F},
            .maximum = {0.75F, 1.0F, 3.0F},
        },
        terrain::Bounds3{
            .minimum = {0.25F, 0.01F, 0.25F},
            .maximum = {0.75F, 1.0F, 0.75F},
        },
        terrain::Bounds3{
            .minimum = {0.25F, -1.0F, 0.25F},
            .maximum = {0.75F, -0.01F, 0.75F},
        },
    };
    for (std::size_t miss = 0U; miss < misses.size(); ++miss) {
        CAPTURE(miss);
        const auto result =
            surface.lod0_triangles_overlapping_bounds(misses[miss]);
        REQUIRE(result);
        REQUIRE(result.value().empty());
    }
}

TEST_CASE(
    "terrain triangle bounds queries reject malformed bounds",
    "[terrain][height-tile][query][triangle-bounds][validation]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_flat_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    constexpr auto nan =
        std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity =
        std::numeric_limits<float>::infinity();
    constexpr std::array invalid_bounds{
        terrain::Bounds3{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}},
        terrain::Bounds3{{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F}},
        terrain::Bounds3{{0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}},
        terrain::Bounds3{{nan, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        terrain::Bounds3{{0.0F, nan, 0.0F}, {1.0F, 1.0F, 1.0F}},
        terrain::Bounds3{{0.0F, 0.0F, nan}, {1.0F, 1.0F, 1.0F}},
        terrain::Bounds3{{0.0F, 0.0F, 0.0F}, {infinity, 1.0F, 1.0F}},
        terrain::Bounds3{{0.0F, 0.0F, 0.0F}, {1.0F, infinity, 1.0F}},
        terrain::Bounds3{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, infinity}},
    };
    for (std::size_t invalid = 0U;
         invalid < invalid_bounds.size();
         ++invalid) {
        CAPTURE(invalid);
        require_invalid_triangle_bounds(
            surface,
            invalid_bounds[invalid]);
    }
}

TEST_CASE(
    "terrain triangle bounds query results are exactly repeatable",
    "[terrain][height-tile][query][triangle-bounds][determinism]")
{
    using namespace shark;

    auto surface_result =
        terrain::HeightTileSurface::create(make_ramp_tile());
    REQUIRE(surface_result);
    const auto surface = std::move(surface_result).value();
    const auto tile_before = surface.tile();
    const auto bounds_before = surface.bounds();
    const terrain::Bounds3 query_bounds{
        .minimum = {0.25F, 5.0F, 0.25F},
        .maximum = {1.75F, 15.0F, 1.75F},
    };
    const auto baseline =
        surface.lod0_triangles_overlapping_bounds(query_bounds);
    REQUIRE(baseline);
    REQUIRE_FALSE(baseline.value().empty());

    constexpr std::size_t repeat_count = 64U;
    for (std::size_t repeat = 0U;
         repeat < repeat_count;
         ++repeat) {
        CAPTURE(repeat);
        const auto repeated =
            surface.lod0_triangles_overlapping_bounds(query_bounds);
        REQUIRE(repeated);
        REQUIRE(repeated.value() == baseline.value());
    }
    REQUIRE(surface.tile() == tile_before);
    REQUIRE(surface.bounds() == bounds_before);
}
