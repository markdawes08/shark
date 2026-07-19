#include <shark/world/environment_lab_scenario.hpp>

#include <shark/core/error.hpp>

#include <cmath>
#include <string>
#include <utility>

namespace shark::world {
namespace {

inline constexpr terrain::LakeBasinShape environment_lab_lake_basin{
    .footprint = {
        .center = {-128.0F, -128.0F},
        .semi_axis_x = 56.0F,
        .semi_axis_z = 48.0F,
        .x_warp_square_offset = 1'152.0F,
        .x_warp_divisor = 512.0F,
        .z_warp_square_offset = 1'568.0F,
        .z_warp_divisor = 1'024.0F,
    },
    .future_waterline_y = -4.0F,
    .core_depth = 6.5F,
    .rim_height = 1.5F,
    .rise_end_radius = 23.0 / 20.0,
    .rim_end_radius = 27.0 / 20.0,
    .blend_end_radius = 2.0,
};
inline constexpr terrain::HorizontalPoint environment_lab_lake_core{
    -124.0F,
    -128.0F,
};
inline constexpr terrain::HorizontalPoint environment_lab_spawn{
    -128.0F,
    -20.0F,
};
inline constexpr terrain::HorizontalPoint environment_lab_ballistic_body{
    -128.0F,
    -44.0F,
};
inline constexpr float environment_lab_spawn_eye_height = 2.0F;
inline constexpr float environment_lab_ballistic_body_height = 12.0F;
inline constexpr float environment_lab_spawn_pitch = -0.1F;
inline constexpr float environment_lab_far_plane = 1'500.0F;
inline constexpr float minimum_core_depth = 6.0F;
inline constexpr float minimum_spawn_waterline_clearance = 2.0F;

[[nodiscard]] core::Error scenario_error(std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        core::ErrorCode::invalid_state,
        std::move(message),
    };
}

} // namespace

core::Result<EnvironmentLabScenario>
make_environment_lab_scenario()
{
    auto terrain_result = terrain::shape_closed_lake_basin(
        terrain::make_large_capacity_height_tile(),
        environment_lab_lake_basin);
    if (!terrain_result) {
        return core::Result<EnvironmentLabScenario>::failure(
            std::move(terrain_result).error());
    }
    auto shaped_terrain = std::move(terrain_result).value();

    auto surface_result = terrain::HeightTileSurface::create(
        shaped_terrain);
    if (!surface_result) {
        return core::Result<EnvironmentLabScenario>::failure(
            std::move(surface_result).error());
    }
    const auto& surface = surface_result.value();
    const auto core_height = surface.sample_lod0_height(
        environment_lab_lake_core.x,
        environment_lab_lake_core.z);
    const auto spawn_height = surface.sample_lod0_height(
        environment_lab_spawn.x,
        environment_lab_spawn.z);
    const auto ballistic_body_ground_height =
        surface.sample_lod0_height(
            environment_lab_ballistic_body.x,
            environment_lab_ballistic_body.z);
    if (!core_height ||
        !spawn_height ||
        !ballistic_body_ground_height) {
        return core::Result<EnvironmentLabScenario>::failure(
            scenario_error(
                "Environment Lab core, spawn, or ballistic body lies "
                "outside the terrain"));
    }
    if (*core_height >
        environment_lab_lake_basin.future_waterline_y -
            minimum_core_depth) {
        return core::Result<EnvironmentLabScenario>::failure(
            scenario_error(
                "Environment Lab lake core does not meet its depth gate"));
    }
    if (*spawn_height <
        environment_lab_lake_basin.future_waterline_y +
            minimum_spawn_waterline_clearance) {
        return core::Result<EnvironmentLabScenario>::failure(
            scenario_error(
                "Environment Lab spawn is not dry above the waterline"));
    }
    const auto core_field =
        terrain::lake_basin_normalized_radius_squared(
            environment_lab_lake_basin.footprint,
            environment_lab_lake_core);
    const auto spawn_field =
        terrain::lake_basin_normalized_radius_squared(
            environment_lab_lake_basin.footprint,
            environment_lab_spawn);
    const auto ballistic_body_field =
        terrain::lake_basin_normalized_radius_squared(
            environment_lab_lake_basin.footprint,
            environment_lab_ballistic_body);
    if (!std::isfinite(core_field) ||
        !std::isfinite(spawn_field) ||
        !std::isfinite(ballistic_body_field) ||
        core_field > 1.0 ||
        spawn_field <= 1.0 ||
        ballistic_body_field <= 1.0) {
        return core::Result<EnvironmentLabScenario>::failure(
            scenario_error(
                "Environment Lab core/spawn/body footprint ownership "
                "is invalid"));
    }

    const math::Float3 core_position{
        environment_lab_lake_core.x,
        *core_height,
        environment_lab_lake_core.z,
    };
    const math::Float3 spawn_ground{
        environment_lab_spawn.x,
        *spawn_height,
        environment_lab_spawn.z,
    };
    const math::Float3 ballistic_body_spawn{
        environment_lab_ballistic_body.x,
        *ballistic_body_ground_height +
            environment_lab_ballistic_body_height,
        environment_lab_ballistic_body.z,
    };
    Camera spawn_camera;
    spawn_camera.transform.position = {
        spawn_ground.x,
        spawn_ground.y + environment_lab_spawn_eye_height,
        spawn_ground.z,
    };
    spawn_camera.transform.pitch_radians =
        environment_lab_spawn_pitch;
    spawn_camera.lens.far_plane = environment_lab_far_plane;

    return core::Result<EnvironmentLabScenario>::success(
        EnvironmentLabScenario{
            .terrain = std::move(shaped_terrain),
            .lake_basin = environment_lab_lake_basin,
            .lake_core_position = core_position,
            .spawn_ground_position = spawn_ground,
            .ballistic_body_spawn_position =
                ballistic_body_spawn,
            .spawn_camera = spawn_camera,
        });
}

} // namespace shark::world
