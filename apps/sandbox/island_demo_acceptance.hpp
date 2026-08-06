#pragma once

#include "player_avatar_frame.hpp"

#include <shark/character/player_capsule.hpp>
#include <shark/core/result.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/world/island_demo_scenario.hpp>
#include <shark/world/third_person_camera.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace shark::sandbox {

inline constexpr std::size_t island_demo_avatar_phase_count = 6U;

struct IslandDemoJourneyWitness final {
    std::uint64_t completed_fixed_tick{};
    std::uint64_t transcript_checksum{};
    character::PlayerCapsuleSnapshot final_player;
    world::ThirdPersonOrbitSnapshot final_camera_orbit;
    std::uint32_t jump_launch_count{};
    std::uint32_t falling_transition_count{};
    std::uint32_t landing_count{};
    std::uint32_t dry_to_wading_count{};
    std::uint32_t wading_to_swimming_count{};
    std::uint32_t swimming_to_wading_count{};
    std::uint32_t wading_to_dry_count{};
    float maximum_water_depth{};
    float minimum_terrain_clearance{};
    float maximum_tick_displacement{};
    bool observed_camera_orbit{};
    bool observed_idle_avatar{};
    bool observed_walk_avatar{};
    bool observed_run_avatar{};
    bool observed_jump_avatar{};
    bool observed_wade_avatar{};
    bool observed_swim_avatar{};
    bool returned_to_dry_land{};

    [[nodiscard]] friend bool operator==(
        const IslandDemoJourneyWitness&,
        const IslandDemoJourneyWitness&) noexcept = default;
};

struct IslandDemoAcceptanceReport final {
    IslandDemoJourneyWitness journey;
    std::array<PlayerAvatarFrame, island_demo_avatar_phase_count>
        avatar_phase_checkpoints{};
    std::array<std::uint64_t, island_demo_avatar_phase_count>
        avatar_phase_checkpoint_ticks{};
    std::uint64_t render_frame_count{};
    std::uint64_t zero_step_render_frame_count{};
    std::uint64_t multi_step_render_frame_count{};
    std::uint64_t camera_frame_count{};
    std::uint64_t avatar_frame_count{};
};

// Replays the complete Island Demo 0.1 journey through the same fixed-clock,
// camera, gameplay-water, character, and presentation composition used by the
// sandbox. render_rate_hz changes only the render partition; the returned
// journey witness must remain exact. This is an acceptance diagnostic, not an
// autoplay mode and not gameplay authority.
[[nodiscard]] core::Result<IslandDemoAcceptanceReport>
run_island_demo_acceptance(
    const world::IslandDemoScenario& scenario,
    const terrain::HeightTileSurface& terrain_surface,
    std::uint32_t render_rate_hz);

} // namespace shark::sandbox
