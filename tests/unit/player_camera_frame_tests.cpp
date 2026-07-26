#include "player_camera_frame.hpp"

#include <shark/character/player_capsule.hpp>
#include <shark/simulation/fixed_step_clock.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/world/island_demo_scenario.hpp>
#include <shark/world/third_person_camera.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

struct IslandCameraFixture final {
    shark::world::IslandDemoScenario scenario;
    shark::terrain::HeightTileSurface surface;
};

[[nodiscard]] IslandCameraFixture make_island_fixture()
{
    auto scenario_result =
        shark::world::make_island_demo_scenario();
    REQUIRE(scenario_result);
    auto scenario = std::move(scenario_result).value();
    auto surface_result =
        shark::terrain::HeightTileSurface::create(
            scenario.terrain);
    REQUIRE(surface_result);
    return {
        .scenario = std::move(scenario),
        .surface = std::move(surface_result).value(),
    };
}

[[nodiscard]] shark::character::PlayerCapsuleSimulation
make_player(
    const shark::world::IslandDemoScenario& scenario,
    const shark::terrain::HeightTileSurface& surface)
{
    auto result = shark::character::create_player_capsule(
        scenario.player_capsule,
        surface);
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::world::ThirdPersonCameraRig
make_camera_rig(
    const shark::world::IslandDemoScenario& scenario)
{
    auto result =
        shark::world::create_third_person_camera_rig(
            scenario.player_camera);
    REQUIRE(result);
    return std::move(result).value();
}

void advance_camera_and_player(
    shark::world::ThirdPersonCameraRig& camera_rig,
    shark::character::PlayerCapsuleSimulation& player,
    const shark::character::PlayerActionCommand command,
    const shark::world::ThirdPersonOrbitDelta camera_delta,
    const shark::terrain::HeightTileSurface& surface,
    const float fixed_delta_seconds,
    const std::uint64_t fixed_tick)
{
    using namespace shark;

    REQUIRE(world::advance_third_person_camera_rig(
        camera_rig,
        camera_delta,
        fixed_tick));
    const auto camera_basis = world::horizontal_camera_basis(
        camera_rig.current.state.yaw_radians);
    REQUIRE(camera_basis);
    REQUIRE(character::advance_player_capsule(
        player,
        command,
        character::PlayerMovementFrame{
            .right = camera_basis.value().right,
            .forward = camera_basis.value().forward,
        },
        surface,
        fixed_delta_seconds,
        fixed_tick));
}

[[nodiscard]] shark::sandbox::PlayerCameraFrame
run_partition(
    const std::uint32_t render_rate_hz,
    const IslandCameraFixture& fixture)
{
    using namespace shark;

    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();
    auto player = make_player(
        fixture.scenario,
        fixture.surface);
    auto camera_rig = make_camera_rig(fixture.scenario);

    auto previous_timestamp = std::chrono::nanoseconds{0};
    for (std::uint64_t frame = 1U;
         frame <= render_rate_hz;
         ++frame) {
        const auto timestamp = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                frame * 1'000'000'000ULL / render_rate_hz)};
        const auto frame_result =
            clock.advance(timestamp - previous_timestamp);
        REQUIRE(frame_result);
        previous_timestamp = timestamp;

        const auto first_fixed_tick =
            clock.total_step_count() -
            frame_result.value().step_count + 1U;
        for (std::uint32_t step = 0U;
             step < frame_result.value().step_count;
             ++step) {
            const auto fixed_tick = first_fixed_tick + step;
            advance_camera_and_player(
                camera_rig,
                player,
                character::PlayerActionCommand{
                    .move_forward_held = true,
                },
                world::ThirdPersonOrbitDelta{
                    .yaw_radians =
                        fixed_tick % 5U == 0U
                        ? 0.03125F
                        : -0.0078125F,
                    .pitch_radians =
                        fixed_tick % 11U == 0U
                        ? 0.015625F
                        : -0.00390625F,
                    .boom_distance =
                        fixed_tick % 13U == 0U
                        ? -0.125F
                        : 0.015625F,
                },
                fixture.surface,
                clock.fixed_delta_seconds(),
                fixed_tick);
        }
    }

    REQUIRE(previous_timestamp == std::chrono::seconds{1});
    REQUIRE(clock.total_step_count() == 60U);
    auto composed = sandbox::build_player_camera_frame(
        player,
        camera_rig,
        clock.interpolation_alpha(),
        fixture.scenario.player_camera_lens,
        fixture.surface);
    REQUIRE(composed);
    return std::move(composed).value();
}

} // namespace

TEST_CASE(
    "player camera frame composes synchronized Island Demo snapshots",
    "[sandbox][player-camera-frame][composition]")
{
    using namespace shark;

    const auto fixture = make_island_fixture();
    auto player = make_player(
        fixture.scenario,
        fixture.surface);
    auto camera_rig = make_camera_rig(fixture.scenario);

    advance_camera_and_player(
        camera_rig,
        player,
        character::PlayerActionCommand{
            .move_forward_held = true,
        },
        world::ThirdPersonOrbitDelta{
            .yaw_radians = 0.2F,
            .pitch_radians = 0.1F,
            .boom_distance = -2.0F,
        },
        fixture.surface,
        1.0F / 60.0F,
        1U);
    REQUIRE(player.current.state.center_position.x >
        player.previous.state.center_position.x);
    REQUIRE(player.current.state.center_position.z <
        player.previous.state.center_position.z);
    REQUIRE(player.current.horizontal_velocity != math::Float3{});

    constexpr auto alpha = 0.25F;
    const auto expected_player =
        character::interpolate_player_capsule(player, alpha);
    const auto expected_orbit =
        world::interpolate_third_person_camera_rig(
            camera_rig,
            alpha);
    REQUIRE(expected_player);
    REQUIRE(expected_orbit);
    const auto expected_placement =
        world::build_third_person_camera(
            camera_rig.config,
            expected_orbit.value(),
            expected_player.value().center_position,
            fixture.scenario.player_camera_lens,
            fixture.surface);
    REQUIRE(expected_placement);

    const auto player_before = player;
    const auto rig_before = camera_rig;
    const auto lens_before =
        fixture.scenario.player_camera_lens;
    const auto terrain_before = fixture.surface.tile();
    const auto frame = sandbox::build_player_camera_frame(
        player,
        camera_rig,
        alpha,
        fixture.scenario.player_camera_lens,
        fixture.surface);
    REQUIRE(frame);
    REQUIRE(frame.value().interpolated_player ==
        expected_player.value());
    REQUIRE(frame.value().interpolated_orbit ==
        expected_orbit.value());
    REQUIRE(frame.value().camera_placement ==
        expected_placement.value());
    REQUIRE(frame.value().camera_placement.target_position ==
        math::Float3{
            expected_player.value().center_position.x,
            expected_player.value().center_position.y +
                camera_rig.config.target_height_offset,
            expected_player.value().center_position.z,
        });
    REQUIRE(frame.value().camera_placement.camera.lens
            .vertical_fov_radians ==
        lens_before.vertical_fov_radians);
    REQUIRE(frame.value().camera_placement.camera.lens
            .near_plane ==
        lens_before.near_plane);
    REQUIRE(frame.value().camera_placement.camera.lens
            .far_plane ==
        lens_before.far_plane);
    REQUIRE(player == player_before);
    REQUIRE(camera_rig == rig_before);
    REQUIRE(fixture.scenario.player_camera_lens
            .vertical_fov_radians ==
        lens_before.vertical_fov_radians);
    REQUIRE(fixture.scenario.player_camera_lens.near_plane ==
        lens_before.near_plane);
    REQUIRE(fixture.scenario.player_camera_lens.far_plane ==
        lens_before.far_plane);
    REQUIRE(fixture.surface.tile() == terrain_before);
}

TEST_CASE(
    "player camera frame follows interpolated vertical falling motion",
    "[sandbox][player-camera-frame][character][falling]")
{
    using namespace shark;

    const auto fixture = make_island_fixture();
    auto airborne_config = fixture.scenario.player_capsule;
    airborne_config.spawn_center_position.y += 2.0F;
    auto player_result = character::create_player_capsule(
        airborne_config,
        fixture.surface);
    REQUIRE(player_result);
    auto player = std::move(player_result).value();
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::falling);

    auto camera_rig = make_camera_rig(fixture.scenario);
    advance_camera_and_player(
        camera_rig,
        player,
        {},
        {},
        fixture.surface,
        1.0F / 60.0F,
        1U);
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::falling);
    REQUIRE(player.current.vertical.velocity_y < 0.0F);
    REQUIRE(player.current.state.center_position.y <
        player.previous.state.center_position.y);

    constexpr auto alpha = 0.5F;
    const auto expected_player =
        character::interpolate_player_capsule(
            player,
            alpha);
    REQUIRE(expected_player);
    const auto frame = sandbox::build_player_camera_frame(
        player,
        camera_rig,
        alpha,
        fixture.scenario.player_camera_lens,
        fixture.surface);
    REQUIRE(frame);
    REQUIRE(frame.value().interpolated_player ==
        expected_player.value());
    REQUIRE(frame.value().camera_placement.target_position ==
        math::Float3{
            expected_player.value().center_position.x,
            expected_player.value().center_position.y +
                camera_rig.config.target_height_offset,
            expected_player.value().center_position.z,
        });
    REQUIRE(
        frame.value().interpolated_player.center_position.y >
        player.current.state.center_position.y);
    REQUIRE(
        frame.value().interpolated_player.center_position.y <
        player.previous.state.center_position.y);
}

TEST_CASE(
    "player camera frame applies canonical terrain obstruction",
    "[sandbox][player-camera-frame][terrain]")
{
    using namespace shark;

    const auto fixture = make_island_fixture();
    auto player = make_player(
        fixture.scenario,
        fixture.surface);
    auto camera_rig = make_camera_rig(fixture.scenario);
    advance_camera_and_player(
        camera_rig,
        player,
        {},
        world::ThirdPersonOrbitDelta{
            .pitch_radians =
                camera_rig.config.maximum_pitch_radians -
                camera_rig.current.state.pitch_radians,
        },
        fixture.surface,
        1.0F / 60.0F,
        1U);

    const auto frame = sandbox::build_player_camera_frame(
        player,
        camera_rig,
        1.0F,
        fixture.scenario.player_camera_lens,
        fixture.surface);
    REQUIRE(frame);
    REQUIRE(frame.value().camera_placement.terrain_obstructed);
    REQUIRE(
        frame.value().camera_placement.applied_boom_distance <
        frame.value().camera_placement.desired_boom_distance);
    REQUIRE(frame.value().camera_placement.applied_boom_distance >=
        0.0F);

    const auto clearance_check =
        fixture.surface.closest_lod0_point_to_segment(
            terrain::Segment3{
                .first_endpoint =
                    frame.value().camera_placement.target_position,
                .second_endpoint = frame.value()
                    .camera_placement.camera.transform.position,
            },
            camera_rig.config.obstruction_clearance);
    REQUIRE(clearance_check);
    REQUIRE_FALSE(clearance_check.value());
}

TEST_CASE(
    "player camera frame rejects invalid alpha without mutation",
    "[sandbox][player-camera-frame][validation][immutability]")
{
    using namespace shark;

    const auto fixture = make_island_fixture();
    const auto player = make_player(
        fixture.scenario,
        fixture.surface);
    const auto camera_rig = make_camera_rig(fixture.scenario);
    const auto player_before = player;
    const auto rig_before = camera_rig;

    REQUIRE_FALSE(sandbox::build_player_camera_frame(
        player,
        camera_rig,
        -0.001F,
        fixture.scenario.player_camera_lens,
        fixture.surface));
    REQUIRE_FALSE(sandbox::build_player_camera_frame(
        player,
        camera_rig,
        1.001F,
        fixture.scenario.player_camera_lens,
        fixture.surface));
    REQUIRE_FALSE(sandbox::build_player_camera_frame(
        player,
        camera_rig,
        std::numeric_limits<float>::quiet_NaN(),
        fixture.scenario.player_camera_lens,
        fixture.surface));
    REQUIRE(player == player_before);
    REQUIRE(camera_rig == rig_before);
}

TEST_CASE(
    "player camera final frame is exact across render partitions",
    "[sandbox][player-camera-frame][fixed-step][invariance]")
{
    const auto fixture = make_island_fixture();
    constexpr std::array render_rates{
        30U,
        60U,
        120U,
        144U,
    };

    const auto baseline =
        run_partition(render_rates.front(), fixture);
    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        REQUIRE(run_partition(render_rate, fixture) ==
            baseline);
    }
}
