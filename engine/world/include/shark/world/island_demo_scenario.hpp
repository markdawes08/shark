#pragma once

#include <shark/character/player_capsule.hpp>
#include <shark/core/result.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/terrain/island.hpp>
#include <shark/water/gameplay_water.hpp>
#include <shark/world/third_person_camera.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace shark::world {

inline constexpr std::size_t island_demo_route_point_count = 8;
inline constexpr std::size_t island_demo_shore_sample_count = 4;
inline constexpr std::size_t island_demo_sphere_body_count = 4;
inline constexpr float island_demo_sphere_body_radius = 1.0F;
inline constexpr float island_demo_sphere_body_mass = 1.0F;
inline constexpr float island_demo_sphere_restitution = 0.75F;
inline constexpr float island_demo_water_render_half_extent_x =
    2'048.0F;
inline constexpr float island_demo_water_render_half_extent_z =
    2'048.0F;
inline constexpr double
    island_demo_coarse_maximum_geometric_error = 0.501953125;
inline constexpr std::uint64_t
    island_demo_terrain_height_checksum =
        0x53DD'2821'AE9A'CDEAULL;

struct IslandDemoWaterSurface final {
    water::CalmWaterBody gameplay_body;
    float depth_proxy{};
    float render_half_extent_x{};
    float render_half_extent_z{};

    [[nodiscard]] friend bool operator==(
        const IslandDemoWaterSurface&,
        const IslandDemoWaterSurface&) = default;
};

struct IslandDemoScenario final {
    terrain::HeightTile terrain;
    terrain::IslandShape island;
    IslandDemoWaterSurface water;
    std::uint64_t terrain_height_checksum{};
    math::Float3 spawn_ground_position;
    character::PlayerCapsuleConfig player_capsule;
    std::array<
        math::Float3,
        island_demo_route_point_count>
        traversal_loop{};
    std::array<
        math::Float3,
        island_demo_shore_sample_count>
        shore_entry_samples{};
    std::array<
        math::Float3,
        island_demo_sphere_body_count>
        sphere_body_spawn_positions{};
    std::array<
        math::Float3,
        island_demo_sphere_body_count>
        sphere_body_initial_velocities{};
    std::array<
        math::Quaternion,
        island_demo_sphere_body_count>
        sphere_body_initial_orientations{};
    std::array<
        math::Float3,
        island_demo_sphere_body_count>
        sphere_body_initial_angular_velocities{};
    std::array<
        math::Float3,
        island_demo_sphere_body_count>
        sphere_body_torques{};
    float sphere_body_radius{};
    float sphere_body_mass{};
    float sphere_restitution{};
    ThirdPersonCameraConfig player_camera;
    PerspectiveLens player_camera_lens;
};

// Builds the default playable-island fixture without changing the retained
// Environment Lab, physics, or fluid regression scenarios.
[[nodiscard]] core::Result<IslandDemoScenario>
    make_island_demo_scenario();

} // namespace shark::world
