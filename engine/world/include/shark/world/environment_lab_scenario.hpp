#pragma once

#include <shark/core/result.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/terrain/lake_basin.hpp>
#include <shark/world/camera.hpp>

#include <cstdint>

namespace shark::world {

inline constexpr std::uint64_t
    environment_lab_terrain_height_checksum =
        0x4890'DE3E'1AA0'63A9ULL;
inline constexpr double
    environment_lab_coarse_maximum_geometric_error =
        0.603515625;
inline constexpr float
    environment_lab_maximum_x_adjacent_height_step =
        1.16015625F;
inline constexpr float
    environment_lab_maximum_z_adjacent_height_step =
        1.33203125F;
inline constexpr double
    environment_lab_maximum_lod0_slope_degrees =
        18.6815981793;
inline constexpr float
    environment_lab_water_render_half_extent_x = 64.0F;
inline constexpr float
    environment_lab_water_render_half_extent_z = 56.0F;

struct EnvironmentLabScenario final {
    terrain::HeightTile terrain;
    terrain::LakeBasinShape lake_basin;
    math::Float3 lake_core_position;
    math::Float3 spawn_ground_position;
    Camera spawn_camera;
};

// Composes T-008 from the untouched T-007 rolling-height oracle plus one
// deterministic, terrain-owned basin post-process. The returned lake metadata
// is authoritative for W-001; this factory creates no water or GPU state.
[[nodiscard]] core::Result<EnvironmentLabScenario>
    make_environment_lab_scenario();

} // namespace shark::world
