#include "island_demo_acceptance.hpp"

#include <shark/terrain/height_tile.hpp>
#include <shark/world/island_demo_scenario.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

TEST_CASE(
    "Island Demo completes one exact playable journey across render partitions",
    "[sandbox][island-demo][acceptance][determinism]")
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

    const auto baseline_result =
        sandbox::run_island_demo_acceptance(
            scenario,
            surface,
            30U);
    REQUIRE(baseline_result);
    const auto& baseline = baseline_result.value();
    REQUIRE(baseline.render_frame_count > 0U);
    REQUIRE(baseline.multi_step_render_frame_count > 0U);
    REQUIRE(baseline.camera_frame_count ==
        baseline.render_frame_count);
    REQUIRE(baseline.avatar_frame_count ==
        baseline.render_frame_count);
    REQUIRE(baseline.journey.completed_fixed_tick == 3'060U);
    REQUIRE(baseline.journey.transcript_checksum ==
        14'647'426'038'331'086'164ULL);
    REQUIRE(baseline.journey.jump_launch_count == 1U);
    REQUIRE(baseline.journey.falling_transition_count == 1U);
    REQUIRE(baseline.journey.landing_count == 1U);
    REQUIRE(baseline.journey.dry_to_wading_count == 1U);
    REQUIRE(baseline.journey.wading_to_swimming_count == 1U);
    REQUIRE(baseline.journey.swimming_to_wading_count == 1U);
    REQUIRE(baseline.journey.wading_to_dry_count == 1U);
    REQUIRE(baseline.journey.maximum_water_depth >= 5.0F);
    REQUIRE(baseline.journey.minimum_terrain_clearance >=
        -0.0001F);
    REQUIRE(baseline.journey.maximum_tick_displacement <= 0.5F);
    REQUIRE(baseline.journey.observed_camera_orbit);
    REQUIRE(baseline.journey.observed_idle_avatar);
    REQUIRE(baseline.journey.observed_walk_avatar);
    REQUIRE(baseline.journey.observed_run_avatar);
    REQUIRE(baseline.journey.observed_jump_avatar);
    REQUIRE(baseline.journey.observed_wade_avatar);
    REQUIRE(baseline.journey.observed_swim_avatar);
    REQUIRE(baseline.journey.returned_to_dry_land);
    REQUIRE(baseline.journey.final_player.reset_generation == 0U);
    REQUIRE(baseline.journey.final_player.water.phase ==
        character::PlayerWaterPhase::dry);
    REQUIRE(baseline.journey.final_player.vertical.phase ==
        character::PlayerGroundPhase::grounded);
    REQUIRE(baseline.journey.final_player.horizontal_velocity ==
        math::Float3{});
    REQUIRE(baseline.journey.final_camera_orbit.fixed_tick ==
        baseline.journey.completed_fixed_tick);
    std::uint64_t previous_checkpoint_tick = 0U;
    for (std::size_t phase_index = 0U;
         phase_index < sandbox::island_demo_avatar_phase_count;
         ++phase_index) {
        const auto expected_phase = static_cast<
            sandbox::PlayerAvatarPresentationPhase>(
                phase_index + 1U);
        REQUIRE(
            baseline.avatar_phase_checkpoints[phase_index]
                .current_phase == expected_phase);
        REQUIRE(
            baseline.avatar_phase_checkpoint_ticks[phase_index] >
            previous_checkpoint_tick);
        previous_checkpoint_tick =
            baseline.avatar_phase_checkpoint_ticks[phase_index];
    }

    for (const auto render_rate :
         std::array<std::uint32_t, 3>{60U, 120U, 144U}) {
        CAPTURE(render_rate);
        const auto result = sandbox::run_island_demo_acceptance(
            scenario,
            surface,
            render_rate);
        REQUIRE(result);
        REQUIRE(result.value().journey == baseline.journey);
        REQUIRE(result.value().avatar_phase_checkpoints ==
            baseline.avatar_phase_checkpoints);
        REQUIRE(result.value().avatar_phase_checkpoint_ticks ==
            baseline.avatar_phase_checkpoint_ticks);
        REQUIRE(result.value().camera_frame_count ==
            result.value().render_frame_count);
        REQUIRE(result.value().avatar_frame_count ==
            result.value().render_frame_count);
        if (render_rate > 60U) {
            REQUIRE(
                result.value().zero_step_render_frame_count > 0U);
        }
    }
}

TEST_CASE(
    "Island Demo acceptance rejects an invalid render partition",
    "[sandbox][island-demo][acceptance][validation]")
{
    using namespace shark;

    const auto scenario_result =
        world::make_island_demo_scenario();
    REQUIRE(scenario_result);
    const auto surface_result =
        terrain::HeightTileSurface::create(
            scenario_result.value().terrain);
    REQUIRE(surface_result);
    REQUIRE_FALSE(sandbox::run_island_demo_acceptance(
        scenario_result.value(),
        surface_result.value(),
        3U));
    const auto minimum_supported =
        sandbox::run_island_demo_acceptance(
            scenario_result.value(),
            surface_result.value(),
            4U);
    REQUIRE(minimum_supported);
    REQUIRE(minimum_supported.value().journey.completed_fixed_tick ==
        3'060U);
    REQUIRE(minimum_supported.value().journey.transcript_checksum ==
        14'647'426'038'331'086'164ULL);
}
