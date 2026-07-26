#include <shark/world/island_demo_scenario.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace shark::world {
namespace {

inline constexpr terrain::IslandShape island_demo_shape{
    .footprint = {
        .center_x = 0.0F,
        .center_z = 0.0F,
        .semi_axis_x = 210.0F,
        .semi_axis_z = 170.0F,
        .x_warp_square_offset = 0.0F,
        .x_warp_divisor = 4'096.0F,
        .z_warp_square_offset = 0.0F,
        .z_warp_divisor = 4'096.0F,
    },
    .waterline_y = -4.0F,
    .shoreline_land_clearance = 0.75F,
    .interior_land_height = 8.0F,
    .natural_relief_scale = 0.25F,
    .shoreline_water_depth = 0.25F,
    .deep_water_depth = 9.0F,
    .deep_water_end_radius_squared = 2.25,
};
inline constexpr water::CalmWaterBody island_demo_gameplay_water{
    .footprint = island_demo_shape.footprint,
    .support_side =
        water::CalmWaterSupportSide::outside_warped_footprint,
    .surface_height = island_demo_shape.waterline_y,
    .shoreline_depth_tolerance =
        water::default_shoreline_depth_tolerance,
    .flow_velocity = water::HorizontalFlow{},
};
inline constexpr std::array<
    std::array<float, 2>,
    island_demo_route_point_count>
    island_demo_route{{
        {0.0F, 112.0F},
        {72.0F, 64.0F},
        {96.0F, 0.0F},
        {68.0F, -64.0F},
        {0.0F, -88.0F},
        {-72.0F, -60.0F},
        {-96.0F, 0.0F},
        {-68.0F, 68.0F},
    }};
inline constexpr std::array<
    std::array<float, 2>,
    island_demo_shore_sample_count>
    island_demo_shore_transect{{
        {0.0F, 160.0F},
        {0.0F, 176.0F},
        {0.0F, 192.0F},
        {0.0F, 224.0F},
    }};
inline constexpr std::array<
    std::array<float, 2>,
    island_demo_sphere_body_count>
    island_demo_sphere_bodies{{
        {0.0F, 88.0F},
        {-12.0F, 60.0F},
        {0.0F, 60.0F},
        {20.0F, 88.0F},
    }};
inline constexpr std::array<
    math::Float3,
    island_demo_sphere_body_count>
    island_demo_sphere_initial_velocities{{
        {},
        {5.0F, 0.0F, 0.0F},
        {-3.0F, 0.0F, 0.0F},
        {},
    }};
inline constexpr std::array<
    math::Quaternion,
    island_demo_sphere_body_count>
    island_demo_sphere_initial_orientations{};
inline constexpr std::array<
    math::Float3,
    island_demo_sphere_body_count>
    island_demo_sphere_initial_angular_velocities{};
inline constexpr std::array<
    math::Float3,
    island_demo_sphere_body_count>
    island_demo_sphere_torques{{
        {},
        {},
        {},
        {0.0F, 0.0F, 0.2F},
    }};
inline constexpr character::PlayerCapsuleShape
    island_demo_player_capsule_shape{
        .radius = 0.5F,
        .vertical_half_segment = 0.5F,
    };
inline constexpr float island_demo_player_minimum_center_y = -32.0F;
inline constexpr float island_demo_player_maximum_center_y = 64.0F;
inline constexpr ThirdPersonCameraConfig island_demo_player_camera{
    .target_height_offset =
        default_third_person_target_height_offset,
    .minimum_pitch_radians =
        default_third_person_minimum_pitch,
    .maximum_pitch_radians =
        default_third_person_maximum_pitch,
    .initial_yaw_radians = 0.0F,
    .initial_pitch_radians =
        default_third_person_initial_pitch,
    .minimum_boom_distance =
        default_third_person_minimum_boom_distance,
    .maximum_boom_distance =
        default_third_person_maximum_boom_distance,
    .initial_boom_distance =
        default_third_person_initial_boom_distance,
    .obstruction_clearance =
        default_third_person_obstruction_clearance,
};
inline constexpr float island_demo_primary_body_height = 12.0F;
inline constexpr float island_demo_pair_body_height = 20.0F;
inline constexpr float island_demo_isolated_body_height = 14.0F;
inline constexpr float island_demo_far_plane = 1'500.0F;
inline constexpr float minimum_spawn_clearance = 2.0F;
inline constexpr float minimum_route_clearance = 0.5F;
inline constexpr float route_sample_spacing = 2.0F;
inline constexpr float minimum_surface_normal_y = 0.8660254F;

[[nodiscard]] core::Error scenario_error(std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        core::ErrorCode::invalid_state,
        std::move(message),
    };
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

[[nodiscard]] core::Result<math::Float3> sample_ground(
    const terrain::HeightTileSurface& surface,
    const float x,
    const float z,
    const char* label)
{
    const auto sample = surface.sample_lod0_surface(x, z);
    if (!sample.has_value()) {
        return core::Result<math::Float3>::failure(
            scenario_error(
                std::string{"Island Demo "} + label +
                " lies outside canonical terrain"));
    }
    return core::Result<math::Float3>::success(sample->position);
}

[[nodiscard]] core::Result<void> validate_route(
    const terrain::HeightTileSurface& surface)
{
    for (std::size_t start_index = 0;
         start_index < island_demo_route.size();
         ++start_index) {
        const auto& start = island_demo_route[start_index];
        const auto& end = island_demo_route[
            (start_index + 1U) % island_demo_route.size()];
        const auto delta_x = end[0] - start[0];
        const auto delta_z = end[1] - start[1];
        const auto length = std::sqrt(
            delta_x * delta_x + delta_z * delta_z);
        const auto segment_count = std::max(
            1U,
            static_cast<unsigned int>(
                std::ceil(length / route_sample_spacing)));
        for (unsigned int segment = 0;
             segment <= segment_count;
             ++segment) {
            const auto amount =
                static_cast<float>(segment) /
                static_cast<float>(segment_count);
            const auto sample = surface.sample_lod0_surface(
                start[0] + delta_x * amount,
                start[1] + delta_z * amount);
            if (!sample.has_value() ||
                sample->position.y <
                    island_demo_shape.waterline_y +
                        minimum_route_clearance ||
                sample->normal.y < minimum_surface_normal_y) {
                return core::Result<void>::failure(
                    scenario_error(
                        "Island Demo traversal loop is not continuously "
                        "dry and walkable"));
            }
        }
    }
    return core::Result<void>::success();
}

} // namespace

core::Result<IslandDemoScenario> make_island_demo_scenario()
{
    auto terrain_result = terrain::shape_playable_island(
        terrain::make_large_capacity_height_tile(),
        island_demo_shape);
    if (!terrain_result) {
        return core::Result<IslandDemoScenario>::failure(
            std::move(terrain_result).error());
    }
    auto shaped_terrain = std::move(terrain_result).value();
    const auto shaped_height_checksum =
        height_checksum(shaped_terrain.height_offsets);
    if (shaped_height_checksum !=
        island_demo_terrain_height_checksum) {
        return core::Result<IslandDemoScenario>::failure(
            scenario_error(
                "Island Demo terrain checksum diverged from its "
                "deterministic fixture"));
    }

    auto surface_result = terrain::HeightTileSurface::create(
        shaped_terrain);
    if (!surface_result) {
        return core::Result<IslandDemoScenario>::failure(
            std::move(surface_result).error());
    }
    const auto& surface = surface_result.value();

    auto spawn_result = sample_ground(
        surface,
        island_demo_route[0][0],
        island_demo_route[0][1],
        "spawn");
    if (!spawn_result) {
        return core::Result<IslandDemoScenario>::failure(
            std::move(spawn_result).error());
    }
    const auto spawn_ground = spawn_result.value();
    if (spawn_ground.y <
        island_demo_shape.waterline_y +
            minimum_spawn_clearance) {
        return core::Result<IslandDemoScenario>::failure(
            scenario_error(
                "Island Demo spawn is not dry above the waterline"));
    }

    const auto player_vertical_extent =
        island_demo_player_capsule_shape.radius +
        island_demo_player_capsule_shape.vertical_half_segment;
    const character::PlayerCapsuleConfig player_capsule{
        .shape = island_demo_player_capsule_shape,
        .center_bounds = {
            .minimum = {
                surface.bounds().minimum.x +
                    island_demo_player_capsule_shape.radius,
                island_demo_player_minimum_center_y,
                surface.bounds().minimum.z +
                    island_demo_player_capsule_shape.radius,
            },
            .maximum = {
                surface.bounds().maximum.x -
                    island_demo_player_capsule_shape.radius,
                island_demo_player_maximum_center_y,
                surface.bounds().maximum.z -
                    island_demo_player_capsule_shape.radius,
            },
        },
        .spawn_center_position = {
            spawn_ground.x,
            spawn_ground.y + player_vertical_extent,
            spawn_ground.z,
        },
        .spawn_facing_yaw_radians = 0.0F,
    };
    const auto player_result =
        character::create_player_capsule(player_capsule);
    if (!player_result) {
        return core::Result<IslandDemoScenario>::failure(
            player_result.error());
    }
    const auto spawn_water_result =
        water::query_gameplay_water(
            island_demo_gameplay_water,
            surface,
            spawn_ground.x,
            spawn_ground.z);
    if (!spawn_water_result ||
        spawn_water_result.value().disposition !=
            water::GameplayWaterDisposition::no_water ||
        player_result.value().current.state.center_position.y -
                player_vertical_extent !=
            spawn_ground.y) {
        return core::Result<IslandDemoScenario>::failure(
            scenario_error(
                "Island Demo player capsule is not resting at its "
                "canonical dry spawn"));
    }

    PerspectiveLens player_camera_lens;
    player_camera_lens.far_plane = island_demo_far_plane;
    const auto camera_rig_result =
        create_third_person_camera_rig(island_demo_player_camera);
    if (!camera_rig_result) {
        return core::Result<IslandDemoScenario>::failure(
            camera_rig_result.error());
    }
    const auto camera_placement_result =
        build_third_person_camera(
            island_demo_player_camera,
            camera_rig_result.value().current.state,
            player_result.value().current.state.center_position,
            player_camera_lens,
            surface);
    if (!camera_placement_result ||
        camera_placement_result.value().terrain_obstructed ||
        camera_placement_result.value().desired_boom_distance !=
            island_demo_player_camera.initial_boom_distance ||
        camera_placement_result.value().applied_boom_distance !=
            island_demo_player_camera.initial_boom_distance) {
        return core::Result<IslandDemoScenario>::failure(
            scenario_error(
                "Island Demo player camera does not have an "
                "unobstructed canonical-terrain spawn view"));
    }

    auto route_result = validate_route(surface);
    if (!route_result) {
        return core::Result<IslandDemoScenario>::failure(
            std::move(route_result).error());
    }

    std::array<
        math::Float3,
        island_demo_route_point_count>
        route_positions{};
    for (std::size_t index = 0;
         index < island_demo_route.size();
         ++index) {
        auto point_result = sample_ground(
            surface,
            island_demo_route[index][0],
            island_demo_route[index][1],
            "route point");
        if (!point_result) {
            return core::Result<IslandDemoScenario>::failure(
                std::move(point_result).error());
        }
        route_positions[index] = point_result.value();
    }

    std::array<
        math::Float3,
        island_demo_shore_sample_count>
        shore_positions{};
    for (std::size_t index = 0;
         index < island_demo_shore_transect.size();
         ++index) {
        auto point_result = sample_ground(
            surface,
            island_demo_shore_transect[index][0],
            island_demo_shore_transect[index][1],
            "shore sample");
        if (!point_result) {
            return core::Result<IslandDemoScenario>::failure(
                std::move(point_result).error());
        }
        shore_positions[index] = point_result.value();
    }
    const auto dry_height = shore_positions[0].y;
    const auto shallow_depth =
        island_demo_shape.waterline_y - shore_positions[1].y;
    const auto transition_depth =
        island_demo_shape.waterline_y - shore_positions[2].y;
    const auto swim_depth =
        island_demo_shape.waterline_y - shore_positions[3].y;
    if (dry_height <= island_demo_shape.waterline_y ||
        shallow_depth < 0.25F ||
        shallow_depth > 0.75F ||
        transition_depth < 1.0F ||
        transition_depth > 2.0F ||
        swim_depth < 3.0F) {
        return core::Result<IslandDemoScenario>::failure(
            scenario_error(
                "Island Demo shore transect does not progress from dry "
                "land through shallow and swimmable depths"));
    }

    for (std::size_t index = 0;
         index < shore_positions.size();
         ++index) {
        const auto query_result = water::query_gameplay_water(
            island_demo_gameplay_water,
            surface,
            shore_positions[index].x,
            shore_positions[index].z);
        if (!query_result) {
            return core::Result<IslandDemoScenario>::failure(
                query_result.error());
        }
        const auto expected_disposition =
            index == 0U
                ? water::GameplayWaterDisposition::no_water
                : water::GameplayWaterDisposition::water;
        if (query_result.value().disposition !=
                expected_disposition ||
            (index == 0U &&
             query_result.value().depth != 0.0F) ||
            (index > 0U &&
             query_result.value().depth !=
                 island_demo_shape.waterline_y -
                     shore_positions[index].y)) {
            return core::Result<IslandDemoScenario>::failure(
                scenario_error(
                    "Island Demo gameplay-water query disagrees with "
                    "the authored shore transect"));
        }
    }

    std::array<
        float,
        island_demo_sphere_body_count>
        sphere_ground_heights{};
    for (std::size_t index = 0;
         index < island_demo_sphere_bodies.size();
         ++index) {
        auto point_result = sample_ground(
            surface,
            island_demo_sphere_bodies[index][0],
            island_demo_sphere_bodies[index][1],
            "diagnostic sphere");
        if (!point_result) {
            return core::Result<IslandDemoScenario>::failure(
                std::move(point_result).error());
        }
        sphere_ground_heights[index] = point_result.value().y;
        if (sphere_ground_heights[index] <=
            island_demo_shape.waterline_y) {
            return core::Result<IslandDemoScenario>::failure(
                scenario_error(
                    "An Island Demo diagnostic sphere is not on dry land"));
        }
    }
    const auto pair_spawn_y =
        std::max(
            sphere_ground_heights[1],
            sphere_ground_heights[2]) +
        island_demo_pair_body_height;
    const std::array<
        math::Float3,
        island_demo_sphere_body_count>
        sphere_body_spawns{{
            {
                island_demo_sphere_bodies[0][0],
                sphere_ground_heights[0] +
                    island_demo_primary_body_height,
                island_demo_sphere_bodies[0][1],
            },
            {
                island_demo_sphere_bodies[1][0],
                pair_spawn_y,
                island_demo_sphere_bodies[1][1],
            },
            {
                island_demo_sphere_bodies[2][0],
                pair_spawn_y,
                island_demo_sphere_bodies[2][1],
            },
            {
                island_demo_sphere_bodies[3][0],
                sphere_ground_heights[3] +
                    island_demo_isolated_body_height,
                island_demo_sphere_bodies[3][1],
            },
        }};

    return core::Result<IslandDemoScenario>::success(
        IslandDemoScenario{
            .terrain = std::move(shaped_terrain),
            .island = island_demo_shape,
            .water = {
                .gameplay_body = island_demo_gameplay_water,
                .depth_proxy = island_demo_shape.deep_water_depth,
                .render_half_extent_x =
                    island_demo_water_render_half_extent_x,
                .render_half_extent_z =
                    island_demo_water_render_half_extent_z,
            },
            .terrain_height_checksum = shaped_height_checksum,
            .spawn_ground_position = spawn_ground,
            .player_capsule = player_capsule,
            .traversal_loop = route_positions,
            .shore_entry_samples = shore_positions,
            .sphere_body_spawn_positions =
                sphere_body_spawns,
            .sphere_body_initial_velocities =
                island_demo_sphere_initial_velocities,
            .sphere_body_initial_orientations =
                island_demo_sphere_initial_orientations,
            .sphere_body_initial_angular_velocities =
                island_demo_sphere_initial_angular_velocities,
            .sphere_body_torques =
                island_demo_sphere_torques,
            .sphere_body_radius =
                island_demo_sphere_body_radius,
            .sphere_body_mass =
                island_demo_sphere_body_mass,
            .sphere_restitution =
                island_demo_sphere_restitution,
            .player_camera = island_demo_player_camera,
            .player_camera_lens = player_camera_lens,
        });
}

} // namespace shark::world
