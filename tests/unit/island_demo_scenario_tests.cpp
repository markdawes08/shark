#include <shark/character/player_capsule.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/terrain/island.hpp>
#include <shark/water/gameplay_water.hpp>
#include <shark/world/camera.hpp>
#include <shark/world/island_demo_scenario.hpp>
#include <shark/world/third_person_camera.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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

} // namespace

TEST_CASE(
    "Island Demo scenario is a repeatable bounded launch fixture",
    "[world][scenario][island-demo][determinism][contract]")
{
    using namespace shark;

    const auto first_result = world::make_island_demo_scenario();
    const auto second_result = world::make_island_demo_scenario();
    REQUIRE(first_result);
    REQUIRE(second_result);
    const auto& first = first_result.value();
    const auto& second = second_result.value();

    REQUIRE(first.terrain == second.terrain);
    REQUIRE(first.island == second.island);
    REQUIRE(first.water == second.water);
    REQUIRE(first.terrain_height_checksum ==
        world::island_demo_terrain_height_checksum);
    REQUIRE(first.terrain_height_checksum ==
        second.terrain_height_checksum);
    REQUIRE(first.spawn_ground_position ==
        second.spawn_ground_position);
    REQUIRE(first.player_capsule == second.player_capsule);
    REQUIRE(first.player_camera == second.player_camera);
    REQUIRE(first.player_camera_lens.vertical_fov_radians ==
        second.player_camera_lens.vertical_fov_radians);
    REQUIRE(first.player_camera_lens.near_plane ==
        second.player_camera_lens.near_plane);
    REQUIRE(first.player_camera_lens.far_plane ==
        second.player_camera_lens.far_plane);
    REQUIRE(first.traversal_loop == second.traversal_loop);
    REQUIRE(first.shore_entry_samples ==
        second.shore_entry_samples);
    REQUIRE(first.sphere_body_spawn_positions ==
        second.sphere_body_spawn_positions);
    REQUIRE(first.sphere_body_initial_velocities ==
        second.sphere_body_initial_velocities);
    REQUIRE(first.sphere_body_torques ==
        second.sphere_body_torques);

    REQUIRE(first.terrain.sample_columns ==
        terrain::large_capacity_tile_sample_columns);
    REQUIRE(first.terrain.sample_rows ==
        terrain::large_capacity_tile_sample_rows);
    REQUIRE(first.terrain.sample_spacing ==
        terrain::large_capacity_tile_sample_spacing);
    REQUIRE(first.terrain.origin ==
        terrain::large_capacity_tile_origin);
    REQUIRE(first.water.gameplay_body.footprint ==
        first.island.footprint);
    REQUIRE(first.water.gameplay_body.support_side ==
        water::CalmWaterSupportSide::outside_warped_footprint);
    REQUIRE(first.water.gameplay_body.surface_height ==
        first.island.waterline_y);
    REQUIRE(first.water.gameplay_body.shoreline_depth_tolerance ==
        water::default_shoreline_depth_tolerance);
    REQUIRE(first.water.gameplay_body.flow_velocity ==
        water::HorizontalFlow{});
    REQUIRE(first.water.depth_proxy ==
        first.island.deep_water_depth);
    REQUIRE(first.water.render_half_extent_x ==
        world::island_demo_water_render_half_extent_x);
    REQUIRE(first.water.render_half_extent_z ==
        world::island_demo_water_render_half_extent_z);

    const auto surface_result =
        terrain::HeightTileSurface::create(first.terrain);
    REQUIRE(surface_result);
    const auto& surface = surface_result.value();
    const auto spawn_sample = surface.sample_lod0_surface(
        first.spawn_ground_position.x,
        first.spawn_ground_position.z);
    REQUIRE(spawn_sample);
    REQUIRE(spawn_sample->position ==
        first.spawn_ground_position);
    REQUIRE(first.spawn_ground_position.y >=
        first.water.gameplay_body.surface_height + 2.0F);
    REQUIRE(first.player_capsule.shape ==
        character::PlayerCapsuleShape{
            .radius = 0.5F,
            .vertical_half_segment = 0.5F,
        });
    const auto player_result =
        character::create_player_capsule(first.player_capsule);
    REQUIRE(player_result);
    const auto& player = player_result.value();
    REQUIRE(player.previous == player.current);
    REQUIRE(player.current.state.center_position ==
        math::Float3{
            first.spawn_ground_position.x,
            first.spawn_ground_position.y + 1.0F,
            first.spawn_ground_position.z,
        });
    REQUIRE(
        player.current.state.center_position.y -
            first.player_capsule.shape.radius -
            first.player_capsule.shape.vertical_half_segment ==
        first.spawn_ground_position.y);
    REQUIRE(first.player_capsule.center_bounds.minimum.x ==
        surface.bounds().minimum.x + 0.5F);
    REQUIRE(first.player_capsule.center_bounds.minimum.z ==
        surface.bounds().minimum.z + 0.5F);
    REQUIRE(first.player_capsule.center_bounds.maximum.x ==
        surface.bounds().maximum.x - 0.5F);
    REQUIRE(first.player_capsule.center_bounds.maximum.z ==
        surface.bounds().maximum.z - 0.5F);
    const auto spawn_water = water::query_gameplay_water(
        first.water.gameplay_body,
        surface,
        player.current.state.center_position.x,
        player.current.state.center_position.z);
    REQUIRE(spawn_water);
    REQUIRE(spawn_water.value().disposition ==
        water::GameplayWaterDisposition::no_water);
    REQUIRE(first.player_camera ==
        world::ThirdPersonCameraConfig{});
    REQUIRE(first.player_camera_lens.vertical_fov_radians ==
        math::pi / 3.0F);
    REQUIRE(first.player_camera_lens.near_plane == 0.1F);
    REQUIRE(first.player_camera_lens.far_plane == 1'500.0F);
    const auto camera_rig_result =
        world::create_third_person_camera_rig(
            first.player_camera);
    REQUIRE(camera_rig_result);
    const auto camera_placement_result =
        world::build_third_person_camera(
            first.player_camera,
            camera_rig_result.value().current.state,
            player.current.state.center_position,
            first.player_camera_lens,
            surface);
    REQUIRE(camera_placement_result);
    const auto& camera_placement =
        camera_placement_result.value();
    REQUIRE_FALSE(camera_placement.terrain_obstructed);
    REQUIRE(camera_placement.desired_boom_distance ==
        first.player_camera.initial_boom_distance);
    REQUIRE(camera_placement.applied_boom_distance ==
        first.player_camera.initial_boom_distance);
    REQUIRE(camera_placement.target_position ==
        math::Float3{
            player.current.state.center_position.x,
            player.current.state.center_position.y +
                first.player_camera.target_height_offset,
            player.current.state.center_position.z,
        });
    REQUIRE(camera_placement.camera.lens.vertical_fov_radians ==
        first.player_camera_lens.vertical_fov_radians);
    REQUIRE(camera_placement.camera.lens.near_plane ==
        first.player_camera_lens.near_plane);
    REQUIRE(camera_placement.camera.lens.far_plane ==
        first.player_camera_lens.far_plane);

    const auto coarse_result =
        terrain::build_boundary_preserving_coarse_chunk_layout(
            first.terrain,
            terrain::large_capacity_tile_chunk_cell_columns,
            terrain::large_capacity_tile_chunk_cell_rows);
    REQUIRE(coarse_result);
    REQUIRE(coarse_result.value().chunks.size() ==
        terrain::large_capacity_tile_chunk_count);
    REQUIRE(coarse_result.value().indices.size() ==
        terrain::large_capacity_tile_coarse_index_count);
    REQUIRE(
        coarse_result.value().maximum_geometric_error ==
        world::island_demo_coarse_maximum_geometric_error);
}

TEST_CASE(
    "Island Demo has one closed landmass and a walkable loop",
    "[world][scenario][island-demo][topology][route]")
{
    using namespace shark;

    const auto scenario_result =
        world::make_island_demo_scenario();
    REQUIRE(scenario_result);
    const auto& scenario = scenario_result.value();
    const auto surface_result =
        terrain::HeightTileSurface::create(scenario.terrain);
    REQUIRE(surface_result);
    const auto& surface = surface_result.value();

    const auto& tile = scenario.terrain;
    std::vector<std::uint8_t> dry(
        tile.height_offsets.size(),
        0U);
    bool dry_perimeter = false;
    float minimum_perimeter_depth =
        std::numeric_limits<float>::max();
    for (std::uint32_t z = 0; z < tile.sample_rows; ++z) {
        for (std::uint32_t x = 0; x < tile.sample_columns; ++x) {
            const auto index = sample_index(
                x,
                z,
                tile.sample_columns);
            const auto world_height =
                tile.origin.y + tile.height_offsets[index];
            if (world_height >
                scenario.water.gameplay_body.surface_height) {
                dry[index] = 1U;
                dry_perimeter =
                    dry_perimeter ||
                    x == 0U ||
                    z == 0U ||
                    x + 1U == tile.sample_columns ||
                    z + 1U == tile.sample_rows;
            }
            if (x == 0U ||
                z == 0U ||
                x + 1U == tile.sample_columns ||
                z + 1U == tile.sample_rows) {
                minimum_perimeter_depth = std::min(
                    minimum_perimeter_depth,
                    scenario.water.gameplay_body.surface_height -
                        world_height);
            }
        }
    }
    REQUIRE_FALSE(dry_perimeter);
    REQUIRE(minimum_perimeter_depth >= 7.0F);

    std::vector<std::uint8_t> visited(dry.size(), 0U);
    constexpr std::array<std::array<int, 2>, 4> neighbors{{
        {-1, 0},
        {1, 0},
        {0, -1},
        {0, 1},
    }};
    std::size_t dry_components = 0;
    for (std::uint32_t start_z = 0;
         start_z < tile.sample_rows;
         ++start_z) {
        for (std::uint32_t start_x = 0;
             start_x < tile.sample_columns;
             ++start_x) {
            const auto start = sample_index(
                start_x,
                start_z,
                tile.sample_columns);
            if (dry[start] == 0U || visited[start] != 0U) {
                continue;
            }
            ++dry_components;
            std::deque<std::array<std::uint32_t, 2>> pending;
            pending.push_back({start_x, start_z});
            visited[start] = 1U;
            while (!pending.empty()) {
                const auto point = pending.front();
                pending.pop_front();
                for (const auto direction : neighbors) {
                    const auto next_x =
                        static_cast<std::int64_t>(point[0]) +
                        direction[0];
                    const auto next_z =
                        static_cast<std::int64_t>(point[1]) +
                        direction[1];
                    if (next_x < 0 ||
                        next_z < 0 ||
                        next_x >= tile.sample_columns ||
                        next_z >= tile.sample_rows) {
                        continue;
                    }
                    const auto x =
                        static_cast<std::uint32_t>(next_x);
                    const auto z =
                        static_cast<std::uint32_t>(next_z);
                    const auto index = sample_index(
                        x,
                        z,
                        tile.sample_columns);
                    if (dry[index] == 0U ||
                        visited[index] != 0U) {
                        continue;
                    }
                    visited[index] = 1U;
                    pending.push_back({x, z});
                }
            }
        }
    }
    REQUIRE(dry_components == 1U);

    double route_length = 0.0;
    bool route_is_walkable = true;
    for (std::size_t start_index = 0;
         start_index < scenario.traversal_loop.size();
         ++start_index) {
        const auto& start =
            scenario.traversal_loop[start_index];
        const auto& end = scenario.traversal_loop[
            (start_index + 1U) %
            scenario.traversal_loop.size()];
        const auto delta_x = end.x - start.x;
        const auto delta_z = end.z - start.z;
        const auto length = std::hypot(
            static_cast<double>(delta_x),
            static_cast<double>(delta_z));
        route_length += length;
        const auto segment_count = std::max(
            1U,
            static_cast<unsigned int>(std::ceil(length / 2.0)));
        for (unsigned int segment = 0;
             segment <= segment_count;
             ++segment) {
            const auto amount =
                static_cast<float>(segment) /
                static_cast<float>(segment_count);
            const auto sample = surface.sample_lod0_surface(
                start.x + delta_x * amount,
                start.z + delta_z * amount);
            route_is_walkable =
                route_is_walkable &&
                sample.has_value() &&
                sample->position.y >=
                    scenario.water.gameplay_body.surface_height +
                        0.5F &&
                sample->normal.y >= 0.8660254F;
        }
    }
    REQUIRE(route_length >= 500.0);
    REQUIRE(route_is_walkable);
}

TEST_CASE(
    "Island Demo shoreline progresses through gameplay depth bands",
    "[world][scenario][island-demo][shore][water]")
{
    using namespace shark;

    const auto scenario_result =
        world::make_island_demo_scenario();
    REQUIRE(scenario_result);
    const auto& scenario = scenario_result.value();
    const auto& samples = scenario.shore_entry_samples;
    const auto depth = [&scenario](const math::Float3 point) {
        return scenario.water.gameplay_body.surface_height -
            point.y;
    };

    REQUIRE(depth(samples[0]) ==
        Catch::Approx(-0.98828125F));
    REQUIRE(depth(samples[1]) ==
        Catch::Approx(0.33984375F));
    REQUIRE(depth(samples[2]) ==
        Catch::Approx(1.359375F));
    REQUIRE(depth(samples[3]) ==
        Catch::Approx(5.734375F));
    REQUIRE(samples[0].z < samples[1].z);
    REQUIRE(samples[1].z < samples[2].z);
    REQUIRE(samples[2].z < samples[3].z);

    for (const auto sphere :
         scenario.sphere_body_spawn_positions) {
        REQUIRE(
            terrain::island_normalized_radius_squared(
                scenario.island.footprint,
                sphere.x,
                sphere.z) < 1.0);
    }

    const auto surface_result =
        terrain::HeightTileSurface::create(scenario.terrain);
    REQUIRE(surface_result);
    const auto camera_rig_result =
        world::create_third_person_camera_rig(
            scenario.player_camera);
    REQUIRE(camera_rig_result);
    const auto camera_placement_result =
        world::build_third_person_camera(
            scenario.player_camera,
            camera_rig_result.value().current.state,
            scenario.player_capsule.spawn_center_position,
            scenario.player_camera_lens,
            surface_result.value());
    REQUIRE(camera_placement_result);
    REQUIRE_FALSE(
        camera_placement_result.value().terrain_obstructed);
    const auto& camera_placement =
        camera_placement_result.value();
    const auto camera_basis =
        world::camera_basis(camera_placement.camera.transform);
    const math::Float3 toward_center{
        scenario.island.footprint.center_x -
            camera_placement.camera.transform.position.x,
        scenario.water.gameplay_body.surface_height -
            camera_placement.camera.transform.position.y,
        scenario.island.footprint.center_z -
            camera_placement.camera.transform.position.z,
    };
    const auto forward_dot =
        camera_basis.forward.x * toward_center.x +
        camera_basis.forward.y * toward_center.y +
        camera_basis.forward.z * toward_center.z;
    REQUIRE(forward_dot > 100.0F);
}
