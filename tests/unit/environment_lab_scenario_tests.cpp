#include "cube_scene_data.hpp"
#include "environment_scene_data.hpp"

#include <catch2/catch_test_macros.hpp>

#include <shark/physics/sphere_body_collision.hpp>
#include <shark/renderer/renderer.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/terrain/lake_basin.hpp>
#include <shark/world/camera.hpp>
#include <shark/world/environment_lab_scenario.hpp>

#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

namespace {

[[nodiscard]] std::size_t sample_index(
    const std::uint32_t x,
    const std::uint32_t z,
    const std::uint32_t columns) noexcept
{
    return static_cast<std::size_t>(z) * columns + x;
}

[[nodiscard]] std::uint64_t height_checksum(
    const std::vector<float>& heights) noexcept
{
    constexpr std::uint64_t fnv_offset_basis =
        0xCBF2'9CE4'8422'2325ULL;
    constexpr std::uint64_t fnv_prime = 0x0000'0100'0000'01B3ULL;
    auto result = fnv_offset_basis;
    for (const auto height : heights) {
        const auto bits = std::bit_cast<std::uint32_t>(height);
        for (std::uint32_t shift = 0; shift < 32U; shift += 8U) {
            result ^= (bits >> shift) & 0xFFU;
            result *= fnv_prime;
        }
    }
    return result;
}

[[nodiscard]] std::vector<shark::terrain::HorizontalPoint>
sample_shoreline(
    const shark::terrain::LakeBasinFootprint& footprint)
{
    constexpr std::size_t direction_count = 1'440;
    constexpr std::size_t bisection_steps = 48;
    constexpr std::size_t radial_validation_steps = 256;
    std::vector<shark::terrain::HorizontalPoint> result;
    result.reserve(direction_count);
    auto all_outer_brackets_are_outside = true;
    auto all_sampled_rays_cross_once = true;
    auto maximum_boundary_error = 0.0;
    REQUIRE(
        shark::terrain::lake_basin_normalized_radius_squared(
            footprint,
            footprint.center) <=
        1.0);
    for (std::size_t index = 0;
         index < direction_count;
         ++index) {
        const auto angle =
            shark::math::two_pi *
            static_cast<float>(index) /
            static_cast<float>(direction_count);
        const auto direction_x = std::cos(angle);
        const auto direction_z = std::sin(angle);
        double inner = 0.0;
        double outer = 160.0;
        const shark::terrain::HorizontalPoint outer_point{
            footprint.center.x +
                direction_x * static_cast<float>(outer),
            footprint.center.z +
                direction_z * static_cast<float>(outer),
        };
        all_outer_brackets_are_outside =
            all_outer_brackets_are_outside &&
            shark::terrain::lake_basin_normalized_radius_squared(
                footprint,
                outer_point) >
                1.0;
        auto observed_outside = false;
        for (std::size_t step = 1;
             step <= radial_validation_steps;
             ++step) {
            const auto distance =
                outer * static_cast<double>(step) /
                static_cast<double>(radial_validation_steps);
            const shark::terrain::HorizontalPoint point{
                footprint.center.x +
                    direction_x *
                        static_cast<float>(distance),
                footprint.center.z +
                    direction_z *
                        static_cast<float>(distance),
            };
            const auto inside =
                shark::terrain::
                    lake_basin_normalized_radius_squared(
                        footprint,
                        point) <=
                1.0;
            all_sampled_rays_cross_once =
                all_sampled_rays_cross_once &&
                !(observed_outside && inside);
            observed_outside = observed_outside || !inside;
        }
        for (std::size_t step = 0;
             step < bisection_steps;
             ++step) {
            const auto distance = (inner + outer) * 0.5;
            const shark::terrain::HorizontalPoint point{
                footprint.center.x +
                    direction_x *
                        static_cast<float>(distance),
                footprint.center.z +
                    direction_z *
                        static_cast<float>(distance),
            };
            const auto field =
                shark::terrain::
                    lake_basin_normalized_radius_squared(
                        footprint,
                        point);
            if (field <= 1.0) {
                inner = distance;
            }
            else {
                outer = distance;
            }
        }
        const shark::terrain::HorizontalPoint boundary{
            footprint.center.x +
                direction_x * static_cast<float>(inner),
            footprint.center.z +
                direction_z * static_cast<float>(inner),
        };
        maximum_boundary_error = std::max(
            maximum_boundary_error,
            std::abs(
                shark::terrain::
                    lake_basin_normalized_radius_squared(
                        footprint,
                        boundary) -
                1.0));
        result.push_back(boundary);
    }
    REQUIRE(all_outer_brackets_are_outside);
    REQUIRE(all_sampled_rays_cross_once);
    REQUIRE(maximum_boundary_error <= 0.00001);
    return result;
}

[[nodiscard]] float point_segment_distance(
    const shark::terrain::HorizontalPoint point,
    const shark::terrain::HorizontalPoint first,
    const shark::terrain::HorizontalPoint second) noexcept
{
    const auto segment_x = second.x - first.x;
    const auto segment_z = second.z - first.z;
    const auto length_squared =
        segment_x * segment_x + segment_z * segment_z;
    const auto amount = std::clamp(
        ((point.x - first.x) * segment_x +
         (point.z - first.z) * segment_z) /
            length_squared,
        0.0F,
        1.0F);
    return std::hypot(
        point.x - (first.x + segment_x * amount),
        point.z - (first.z + segment_z * amount));
}

[[nodiscard]] double maximum_lod0_slope_degrees(
    const shark::terrain::HeightTileMesh& mesh)
{
    constexpr double radians_to_degrees =
        57.2957795130823208768;
    auto result = 0.0;
    for (std::size_t index = 0;
         index < mesh.indices.size();
         index += 3U) {
        const auto& first =
            mesh.positions.at(mesh.indices[index]);
        const auto& second =
            mesh.positions.at(mesh.indices[index + 1U]);
        const auto& third =
            mesh.positions.at(mesh.indices[index + 2U]);
        const shark::math::Float3 first_edge{
            second.x - first.x,
            second.y - first.y,
            second.z - first.z,
        };
        const shark::math::Float3 second_edge{
            third.x - first.x,
            third.y - first.y,
            third.z - first.z,
        };
        const shark::math::Float3 normal{
            first_edge.y * second_edge.z -
                first_edge.z * second_edge.y,
            first_edge.z * second_edge.x -
                first_edge.x * second_edge.z,
            first_edge.x * second_edge.y -
                first_edge.y * second_edge.x,
        };
        const auto horizontal = std::hypot(
            static_cast<double>(normal.x),
            static_cast<double>(normal.z));
        const auto slope = std::atan2(
            horizontal,
            std::abs(static_cast<double>(normal.y))) *
            radians_to_degrees;
        result = std::max(result, slope);
    }
    return result;
}

} // namespace

TEST_CASE(
    "Environment Lab publishes a dry spawn and complete future lake",
    "[world][scenario][environment-lab][lake-basin]")
{
    using namespace shark;

    const auto first_result =
        world::make_environment_lab_scenario();
    const auto second_result =
        world::make_environment_lab_scenario();
    REQUIRE(first_result);
    REQUIRE(second_result);
    const auto& first = first_result.value();
    const auto& second = second_result.value();
    REQUIRE(first.terrain == second.terrain);
    REQUIRE(height_checksum(first.terrain.height_offsets) ==
        world::environment_lab_terrain_height_checksum);
    REQUIRE(first.lake_basin == second.lake_basin);
    REQUIRE(first.lake_core_position ==
        second.lake_core_position);
    REQUIRE(first.spawn_ground_position ==
        second.spawn_ground_position);
    REQUIRE(first.sphere_body_spawn_positions ==
        second.sphere_body_spawn_positions);
    REQUIRE(first.sphere_body_initial_velocities ==
        second.sphere_body_initial_velocities);
    REQUIRE(first.sphere_body_radius ==
        second.sphere_body_radius);
    REQUIRE(first.sphere_restitution ==
        second.sphere_restitution);
    REQUIRE(first.spawn_camera.transform.position ==
        second.spawn_camera.transform.position);
    const auto coarse =
        terrain::build_boundary_preserving_coarse_chunk_layout(
            first.terrain,
            terrain::large_capacity_tile_chunk_cell_columns,
            terrain::large_capacity_tile_chunk_cell_rows);
    REQUIRE(coarse);
    REQUIRE(coarse.value().maximum_geometric_error ==
        world::environment_lab_coarse_maximum_geometric_error);
    auto maximum_x_step = 0.0F;
    auto maximum_z_step = 0.0F;
    for (std::uint32_t z = 0;
         z < first.terrain.sample_rows;
         ++z) {
        for (std::uint32_t x = 0;
             x < first.terrain.sample_columns;
             ++x) {
            const auto index = sample_index(
                x,
                z,
                first.terrain.sample_columns);
            if (x + 1U < first.terrain.sample_columns) {
                maximum_x_step = std::max(
                    maximum_x_step,
                    std::abs(
                        first.terrain.height_offsets[index + 1U] -
                        first.terrain.height_offsets[index]));
            }
            if (z + 1U < first.terrain.sample_rows) {
                maximum_z_step = std::max(
                    maximum_z_step,
                    std::abs(
                        first.terrain.height_offsets[
                            index +
                            first.terrain.sample_columns] -
                        first.terrain.height_offsets[index]));
            }
        }
    }
    REQUIRE(maximum_x_step ==
        world::environment_lab_maximum_x_adjacent_height_step);
    REQUIRE(maximum_z_step ==
        world::environment_lab_maximum_z_adjacent_height_step);
    const auto mesh = terrain::build_lod0_mesh(first.terrain);
    REQUIRE(mesh);
    const auto maximum_slope =
        maximum_lod0_slope_degrees(mesh.value());
    REQUIRE(maximum_slope ==
        Catch::Approx(
            world::environment_lab_maximum_lod0_slope_degrees)
            .margin(0.000001));
    REQUIRE(maximum_slope <= 30.0);

    REQUIRE(first.lake_basin.footprint.center ==
        terrain::HorizontalPoint{-128.0F, -128.0F});
    REQUIRE(first.lake_basin.footprint.semi_axis_x == 56.0F);
    REQUIRE(first.lake_basin.footprint.semi_axis_z == 48.0F);
    REQUIRE(
        first.lake_basin.footprint.x_warp_square_offset ==
        1'152.0F);
    REQUIRE(first.lake_basin.footprint.x_warp_divisor == 512.0F);
    REQUIRE(
        first.lake_basin.footprint.z_warp_square_offset ==
        1'568.0F);
    REQUIRE(first.lake_basin.footprint.z_warp_divisor ==
        1'024.0F);
    REQUIRE(first.lake_basin.future_waterline_y == -4.0F);
    REQUIRE(first.lake_basin.core_depth == 6.5F);
    REQUIRE(first.lake_basin.rim_height == 1.5F);
    REQUIRE(first.lake_basin.rise_end_radius == 23.0 / 20.0);
    REQUIRE(first.lake_basin.rim_end_radius == 27.0 / 20.0);
    REQUIRE(first.lake_basin.blend_end_radius == 2.0);
    REQUIRE(first.lake_core_position ==
        math::Float3{-124.0F, -10.47265625F, -128.0F});
    REQUIRE(first.spawn_ground_position ==
        math::Float3{-128.0F, 1.34375F, -20.0F});
    STATIC_REQUIRE(world::environment_lab_sphere_body_count == 4);
    STATIC_REQUIRE(
        world::environment_lab_sphere_body_count ==
        physics::sphere_body_capacity);
    STATIC_REQUIRE(
        world::environment_lab_sphere_body_count ==
        renderer::maximum_material_sphere_count);
    REQUIRE(first.sphere_body_spawn_positions[0].x == -128.0F);
    REQUIRE(first.sphere_body_spawn_positions[0].z == -44.0F);
    REQUIRE(first.sphere_body_spawn_positions[1].x == -136.0F);
    REQUIRE(first.sphere_body_spawn_positions[1].z == -56.0F);
    REQUIRE(first.sphere_body_spawn_positions[2].x == -124.0F);
    REQUIRE(first.sphere_body_spawn_positions[2].z == -56.0F);
    REQUIRE(first.sphere_body_spawn_positions[3].x == -108.0F);
    REQUIRE(first.sphere_body_spawn_positions[3].z == -44.0F);
    REQUIRE(first.sphere_body_spawn_positions[1].y ==
        first.sphere_body_spawn_positions[2].y);
    REQUIRE(first.sphere_body_initial_velocities[0] ==
        math::Float3{});
    REQUIRE(first.sphere_body_initial_velocities[1] ==
        math::Float3{5.0F, 0.0F, 0.0F});
    REQUIRE(first.sphere_body_initial_velocities[2] ==
        math::Float3{-3.0F, 0.0F, 0.0F});
    REQUIRE(first.sphere_body_initial_velocities[3] ==
        math::Float3{});
    REQUIRE(first.sphere_body_radius ==
        world::environment_lab_sphere_body_radius);
    REQUIRE(first.sphere_body_radius ==
        renderer::d3d12::detail::material_sphere_radius);
    REQUIRE(first.sphere_restitution ==
        world::environment_lab_sphere_restitution);
    REQUIRE(first.sphere_restitution ==
        physics::SphereBodyCollisionSettings{}.restitution);
    REQUIRE(first.spawn_camera.transform.position ==
        math::Float3{-128.0F, 3.34375F, -20.0F});
    REQUIRE(first.spawn_camera.transform.yaw_radians == 0.0F);
    REQUIRE(first.spawn_camera.transform.pitch_radians == -0.1F);
    REQUIRE(first.spawn_camera.lens.far_plane == 1'500.0F);

    const auto shoreline =
        sample_shoreline(first.lake_basin.footprint);
    REQUIRE(shoreline.size() == 1'440);
    auto minimum_x = std::numeric_limits<float>::max();
    auto maximum_x = std::numeric_limits<float>::lowest();
    auto minimum_z = std::numeric_limits<float>::max();
    auto maximum_z = std::numeric_limits<float>::lowest();
    auto spawn_distance = std::numeric_limits<float>::max();
    const terrain::HorizontalPoint spawn_xz{
        first.spawn_ground_position.x,
        first.spawn_ground_position.z,
    };
    for (std::size_t index = 0;
         index < shoreline.size();
         ++index) {
        const auto point = shoreline[index];
        minimum_x = std::min(minimum_x, point.x);
        maximum_x = std::max(maximum_x, point.x);
        minimum_z = std::min(minimum_z, point.z);
        maximum_z = std::max(maximum_z, point.z);
        spawn_distance = std::min(
            spawn_distance,
            point_segment_distance(
                spawn_xz,
                point,
                shoreline[(index + 1U) % shoreline.size()]));
    }
    const auto span_x = maximum_x - minimum_x;
    const auto span_z = maximum_z - minimum_z;
    REQUIRE(span_x >= 80.0F);
    REQUIRE(span_x <= 120.0F);
    REQUIRE(span_z >= 80.0F);
    REQUIRE(span_z <= 120.0F);
    REQUIRE(spawn_distance >= 20.0F);
    REQUIRE(span_x ==
        Catch::Approx(111.998421F).margin(0.01F));
    REQUIRE(span_z ==
        Catch::Approx(95.998672F).margin(0.01F));
    REQUIRE(spawn_distance ==
        Catch::Approx(58.496138F).margin(0.02F));

    REQUIRE(first.lake_core_position.y <=
        first.lake_basin.future_waterline_y - 6.0F);
    REQUIRE(first.spawn_ground_position.y >=
        first.lake_basin.future_waterline_y + 2.0F);
    REQUIRE(first.spawn_camera.transform.position.y ==
        first.spawn_ground_position.y + 2.0F);
    const auto surface =
        terrain::HeightTileSurface::create(first.terrain);
    REQUIRE(surface);
    std::array<
        float,
        world::environment_lab_sphere_body_count>
        sphere_body_ground_heights{};
    for (std::size_t body_index = 0;
         body_index < world::environment_lab_sphere_body_count;
         ++body_index) {
        const auto body_ground =
            surface.value().sample_lod0_height(
                first.sphere_body_spawn_positions[body_index].x,
                first.sphere_body_spawn_positions[body_index].z);
        REQUIRE(body_ground);
        sphere_body_ground_heights[body_index] =
            *body_ground;
    }
    REQUIRE(first.sphere_body_spawn_positions[0].y ==
        sphere_body_ground_heights[0] + 12.0F);
    REQUIRE(first.sphere_body_spawn_positions[1].y >=
        sphere_body_ground_heights[1] + 20.0F);
    REQUIRE(first.sphere_body_spawn_positions[2].y >=
        sphere_body_ground_heights[2] + 20.0F);
    REQUIRE(first.sphere_body_spawn_positions[3].y ==
        sphere_body_ground_heights[3] + 14.0F);
    const auto spawn_sample =
        surface.value().sample_lod0_surface(
            first.spawn_ground_position.x,
            first.spawn_ground_position.z);
    REQUIRE(spawn_sample);
    REQUIRE(spawn_sample->position ==
        first.spawn_ground_position);
    const terrain::Ray3 spawn_ray{
        {
            first.spawn_ground_position.x,
            first.spawn_ground_position.y + 100.0F,
            first.spawn_ground_position.z,
        },
        {0.0F, -1.0F, 0.0F},
    };
    const auto spawn_hit =
        surface.value().raycast_lod0(spawn_ray, 200.0F);
    REQUIRE(spawn_hit);
    REQUIRE(spawn_hit.value());
    REQUIRE(spawn_hit.value()->position ==
        first.spawn_ground_position);
    REQUIRE(
        terrain::lake_basin_normalized_radius_squared(
            first.lake_basin.footprint,
            {
                first.lake_core_position.x,
                first.lake_core_position.z,
            }) <=
        1.0);
    REQUIRE(
        terrain::lake_basin_normalized_radius_squared(
            first.lake_basin.footprint,
            spawn_xz) >
        1.0);
    for (const auto body_position :
         first.sphere_body_spawn_positions) {
        REQUIRE(
            terrain::lake_basin_normalized_radius_squared(
                first.lake_basin.footprint,
                {
                    body_position.x,
                    body_position.z,
                }) >
            1.0);
    }

    const auto camera_basis =
        world::camera_basis(first.spawn_camera.transform);
    const math::Float3 toward_lake{
        first.lake_basin.footprint.center.x -
            first.spawn_camera.transform.position.x,
        first.lake_basin.future_waterline_y -
            first.spawn_camera.transform.position.y,
        first.lake_basin.footprint.center.z -
            first.spawn_camera.transform.position.z,
    };
    const auto forward_dot =
        camera_basis.forward.x * toward_lake.x +
        camera_basis.forward.y * toward_lake.y +
        camera_basis.forward.z * toward_lake.z;
    REQUIRE(forward_dot > 100.0F);
}

TEST_CASE(
    "Environment Lab spill rim closes over canonical triangle edges",
    "[world][scenario][environment-lab][lake-basin][flood]")
{
    using namespace shark;

    const auto scenario_result =
        world::make_environment_lab_scenario();
    REQUIRE(scenario_result);
    const auto& scenario = scenario_result.value();
    const auto& tile = scenario.terrain;
    const auto spill_height =
        scenario.lake_basin.future_waterline_y + 1.0F;
    const auto core_x = static_cast<std::uint32_t>(
        std::lround(
            (scenario.lake_core_position.x - tile.origin.x) /
            tile.sample_spacing));
    const auto core_z = static_cast<std::uint32_t>(
        std::lround(
            (scenario.lake_core_position.z - tile.origin.z) /
            tile.sample_spacing));

    std::vector<bool> visited(tile.height_offsets.size(), false);
    std::deque<std::array<std::uint32_t, 2>> pending;
    pending.push_back({core_x, core_z});
    visited[sample_index(
        core_x,
        core_z,
        tile.sample_columns)] = true;
    constexpr std::array<std::array<int, 2>, 6> neighbors{{
        {{-1, 0}},
        {{1, 0}},
        {{0, -1}},
        {{0, 1}},
        {{-1, -1}},
        {{1, 1}},
    }};
    auto reached_tile_edge = false;
    std::size_t visited_count = 0;
    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop_front();
        ++visited_count;
        reached_tile_edge =
            reached_tile_edge ||
            current[0] == 0U ||
            current[1] == 0U ||
            current[0] + 1U == tile.sample_columns ||
            current[1] + 1U == tile.sample_rows;
        for (const auto offset : neighbors) {
            const auto next_x =
                static_cast<std::int64_t>(current[0]) +
                offset[0];
            const auto next_z =
                static_cast<std::int64_t>(current[1]) +
                offset[1];
            if (next_x < 0 ||
                next_z < 0 ||
                next_x >= tile.sample_columns ||
                next_z >= tile.sample_rows) {
                continue;
            }
            const auto index = sample_index(
                static_cast<std::uint32_t>(next_x),
                static_cast<std::uint32_t>(next_z),
                tile.sample_columns);
            if (visited[index]) {
                continue;
            }
            const auto world_height =
                tile.origin.y + tile.height_offsets[index];
            if (world_height < spill_height) {
                visited[index] = true;
                pending.push_back({
                    static_cast<std::uint32_t>(next_x),
                    static_cast<std::uint32_t>(next_z),
                });
            }
        }
    }
    REQUIRE(visited_count > 500);
    REQUIRE_FALSE(reached_tile_edge);
    std::size_t footprint_samples = 0;
    std::size_t visited_footprint_samples = 0;
    for (std::uint32_t z = 0; z < tile.sample_rows; ++z) {
        const auto world_z =
            tile.origin.z +
            static_cast<float>(z) * tile.sample_spacing;
        for (std::uint32_t x = 0; x < tile.sample_columns; ++x) {
            const auto world_x =
                tile.origin.x +
                static_cast<float>(x) * tile.sample_spacing;
            if (terrain::lake_basin_normalized_radius_squared(
                    scenario.lake_basin.footprint,
                    {world_x, world_z}) > 1.0) {
                continue;
            }
            ++footprint_samples;
            visited_footprint_samples +=
                static_cast<std::size_t>(
                    visited[sample_index(
                        x,
                        z,
                        tile.sample_columns)]);
        }
    }
    REQUIRE(footprint_samples == 530);
    REQUIRE(visited_footprint_samples == footprint_samples);
}

TEST_CASE(
    "Environment Lab validation props remain outside dry and unburied",
    "[world][scenario][environment-lab][props]")
{
    using namespace shark;

    const auto scenario_result =
        world::make_environment_lab_scenario();
    REQUIRE(scenario_result);
    const auto& scenario = scenario_result.value();
    const auto surface_result =
        terrain::HeightTileSurface::create(scenario.terrain);
    REQUIRE(surface_result);
    const auto& surface = surface_result.value();
    namespace scene = renderer::d3d12::detail;
    auto cube_minimum_x = std::numeric_limits<float>::max();
    auto cube_maximum_x = std::numeric_limits<float>::lowest();
    auto cube_minimum_z = std::numeric_limits<float>::max();
    auto cube_maximum_z = std::numeric_limits<float>::lowest();
    auto cube_bottom = std::numeric_limits<float>::max();
    for (const auto& vertex : scene::cube_vertices) {
        cube_minimum_x =
            std::min(cube_minimum_x, vertex.position[0]);
        cube_maximum_x =
            std::max(cube_maximum_x, vertex.position[0]);
        cube_bottom =
            std::min(cube_bottom, vertex.position[1]);
        cube_minimum_z =
            std::min(cube_minimum_z, vertex.position[2]);
        cube_maximum_z =
            std::max(cube_maximum_z, vertex.position[2]);
    }
    const auto sphere_mesh = scene::make_material_sphere_mesh();
    const auto sphere_bottom =
        scene::material_sphere_center.y -
        scene::material_sphere_radius;
    REQUIRE(cube_bottom >
        scenario.lake_basin.future_waterline_y);
    REQUIRE(sphere_bottom >
        scenario.lake_basin.future_waterline_y);

    constexpr std::size_t cube_axis_samples = 17;
    for (std::size_t x_index = 0;
         x_index < cube_axis_samples;
         ++x_index) {
        const auto x_amount =
            static_cast<float>(x_index) /
            static_cast<float>(cube_axis_samples - 1U);
        const auto x = std::lerp(
            cube_minimum_x,
            cube_maximum_x,
            x_amount);
        for (std::size_t z_index = 0;
             z_index < cube_axis_samples;
             ++z_index) {
            const auto z_amount =
                static_cast<float>(z_index) /
                static_cast<float>(cube_axis_samples - 1U);
            const auto z = std::lerp(
                cube_minimum_z,
                cube_maximum_z,
                z_amount);
            REQUIRE(
                terrain::lake_basin_normalized_radius_squared(
                    scenario.lake_basin.footprint,
                    {x, z}) >
                scenario.lake_basin.blend_end_radius *
                    scenario.lake_basin.blend_end_radius);
            const auto ground =
                surface.sample_lod0_height(x, z);
            REQUIRE(ground);
            REQUIRE(*ground < cube_bottom);
        }
    }
    REQUIRE(sphere_mesh.vertices.size() ==
        scene::material_sphere_vertex_count);
    for (const auto& vertex : sphere_mesh.vertices) {
        const auto ground = surface.sample_lod0_height(
            vertex.position.x,
            vertex.position.z);
        REQUIRE(ground);
        REQUIRE(*ground < vertex.position.y);
    }
    constexpr std::size_t sphere_radial_samples = 16;
    constexpr std::size_t sphere_angular_samples = 64;
    for (std::size_t radial = 0;
         radial <= sphere_radial_samples;
         ++radial) {
        const auto radius =
            scene::material_sphere_radius *
            static_cast<float>(radial) /
            static_cast<float>(sphere_radial_samples);
        for (std::size_t angular = 0;
             angular < sphere_angular_samples;
             ++angular) {
            const auto angle =
                math::two_pi *
                static_cast<float>(angular) /
                static_cast<float>(sphere_angular_samples);
            const auto x =
                scene::material_sphere_center.x +
                radius * std::cos(angle);
            const auto z =
                scene::material_sphere_center.z +
                radius * std::sin(angle);
            REQUIRE(
                terrain::lake_basin_normalized_radius_squared(
                    scenario.lake_basin.footprint,
                    {x, z}) >
                scenario.lake_basin.blend_end_radius *
                    scenario.lake_basin.blend_end_radius);
            const auto ground =
                surface.sample_lod0_height(x, z);
            REQUIRE(ground);
            REQUIRE(*ground < sphere_bottom);
        }
    }
}
