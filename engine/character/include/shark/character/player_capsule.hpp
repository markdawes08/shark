#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>
#include <shark/terrain/height_tile.hpp>

#include <cstdint>
#include <optional>
#include <type_traits>

namespace shark::character {

inline constexpr float default_player_capsule_radius = 0.5F;
inline constexpr float
    default_player_capsule_vertical_half_segment = 0.5F;
inline constexpr float maximum_player_capsule_radius = 4.0F;
inline constexpr float
    maximum_player_capsule_vertical_half_segment = 4.0F;
inline constexpr float maximum_player_look_delta_radians = math::pi;
inline constexpr float default_player_gravity_magnitude = 9.81F;
inline constexpr float maximum_player_gravity_magnitude = 100.0F;
inline constexpr float default_player_minimum_walkable_normal_y =
    0.70710678118654752440F;
inline constexpr float default_player_ground_snap_distance = 0.05F;
inline constexpr float maximum_player_ground_snap_distance = 1.0F;
inline constexpr float maximum_player_fixed_delta_seconds = 0.25F;

struct PlayerGroundingSettings final {
    float gravity_magnitude{default_player_gravity_magnitude};
    float minimum_walkable_normal_y{
        default_player_minimum_walkable_normal_y};
    float snap_distance{default_player_ground_snap_distance};

    [[nodiscard]] friend bool operator==(
        const PlayerGroundingSettings&,
        const PlayerGroundingSettings&) noexcept = default;
};

enum class PlayerGroundPhase : std::uint8_t {
    grounded = 1,
    falling,
    landing,
    steep_contact,
};

struct PlayerVerticalState final {
    float velocity_y{};
    PlayerGroundPhase phase{PlayerGroundPhase::falling};
    math::Float3 support_normal{};

    [[nodiscard]] friend bool operator==(
        const PlayerVerticalState&,
        const PlayerVerticalState&) noexcept = default;
};

// Character capsules are always aligned to world +Y. This is deliberately
// narrower than the arbitrary-orientation collider owned by Physics.
struct PlayerCapsuleShape final {
    float radius{default_player_capsule_radius};
    float vertical_half_segment{
        default_player_capsule_vertical_half_segment};

    [[nodiscard]] friend bool operator==(
        const PlayerCapsuleShape&,
        const PlayerCapsuleShape&) noexcept = default;
};

struct PlayerCapsuleCenterBounds final {
    math::Float3 minimum{};
    math::Float3 maximum{};

    [[nodiscard]] friend bool operator==(
        const PlayerCapsuleCenterBounds&,
        const PlayerCapsuleCenterBounds&) noexcept = default;
};

struct PlayerCapsuleConfig final {
    PlayerCapsuleShape shape;
    PlayerCapsuleCenterBounds center_bounds;
    math::Float3 spawn_center_position{};
    float spawn_facing_yaw_radians{};
    PlayerGroundingSettings grounding;

    [[nodiscard]] friend bool operator==(
        const PlayerCapsuleConfig&,
        const PlayerCapsuleConfig&) noexcept = default;
};

struct PlayerCapsuleState final {
    math::Float3 center_position{};
    float facing_yaw_radians{};

    [[nodiscard]] friend bool operator==(
        const PlayerCapsuleState&,
        const PlayerCapsuleState&) noexcept = default;
};

// Device-neutral actions sampled once for an emitted fixed tick. Held values
// may repeat on consecutive ticks; the three *_pressed values are one-tick
// pulses supplied by the platform-facing sampler. CHR-002 records every
// command, always advances vertical grounding, and gives reset_pressed the
// only action-driven pose behavior until locomotion.
struct PlayerActionCommand final {
    bool move_forward_held{};
    bool move_backward_held{};
    bool move_left_held{};
    bool move_right_held{};
    bool run_held{};
    bool jump_pressed{};
    bool primary_action_pressed{};
    bool reset_pressed{};
    float look_yaw_delta_radians{};
    float look_pitch_delta_radians{};

    [[nodiscard]] friend bool operator==(
        const PlayerActionCommand&,
        const PlayerActionCommand&) noexcept = default;
};

struct PlayerCapsuleSnapshot final {
    PlayerCapsuleState state;
    PlayerVerticalState vertical;
    PlayerActionCommand consumed_command;
    std::uint64_t fixed_tick{};
    std::uint64_t reset_generation{};

    [[nodiscard]] friend bool operator==(
        const PlayerCapsuleSnapshot&,
        const PlayerCapsuleSnapshot&) noexcept = default;
};

// Exactly one bounded player is represented. There is intentionally no
// entity ID, collection, registry, or hidden allocation.
struct PlayerCapsuleSimulation final {
    PlayerCapsuleConfig config;
    PlayerCapsuleSnapshot previous;
    PlayerCapsuleSnapshot current;

    [[nodiscard]] friend bool operator==(
        const PlayerCapsuleSimulation&,
        const PlayerCapsuleSimulation&) noexcept = default;
};

static_assert(std::is_standard_layout_v<PlayerCapsuleShape>);
static_assert(std::is_trivially_copyable_v<PlayerCapsuleShape>);
static_assert(std::is_standard_layout_v<PlayerGroundingSettings>);
static_assert(std::is_trivially_copyable_v<PlayerGroundingSettings>);
static_assert(std::is_standard_layout_v<PlayerVerticalState>);
static_assert(std::is_trivially_copyable_v<PlayerVerticalState>);
static_assert(std::is_standard_layout_v<PlayerCapsuleCenterBounds>);
static_assert(std::is_trivially_copyable_v<PlayerCapsuleCenterBounds>);
static_assert(std::is_standard_layout_v<PlayerCapsuleConfig>);
static_assert(std::is_trivially_copyable_v<PlayerCapsuleConfig>);
static_assert(std::is_standard_layout_v<PlayerCapsuleState>);
static_assert(std::is_trivially_copyable_v<PlayerCapsuleState>);
static_assert(std::is_standard_layout_v<PlayerActionCommand>);
static_assert(std::is_trivially_copyable_v<PlayerActionCommand>);
static_assert(std::is_standard_layout_v<PlayerCapsuleSnapshot>);
static_assert(std::is_trivially_copyable_v<PlayerCapsuleSnapshot>);
static_assert(std::is_standard_layout_v<PlayerCapsuleSimulation>);
static_assert(std::is_trivially_copyable_v<PlayerCapsuleSimulation>);

[[nodiscard]] bool is_valid(
    const PlayerCapsuleShape& shape) noexcept;

[[nodiscard]] bool is_valid(
    const PlayerGroundingSettings& settings) noexcept;

[[nodiscard]] bool is_valid(
    const PlayerActionCommand& command) noexcept;

[[nodiscard]] bool is_valid(
    const PlayerCapsuleSimulation& simulation) noexcept;

struct PlayerTerrainSupport final {
    terrain::HeightTileSurfaceSample surface;
    float center_position_y{};
    bool walkable{};

    [[nodiscard]] friend bool operator==(
        const PlayerTerrainSupport&,
        const PlayerTerrainSupport&) noexcept = default;
};

static_assert(std::is_standard_layout_v<PlayerTerrainSupport>);
static_assert(std::is_trivially_copyable_v<PlayerTerrainSupport>);

// Samples only the exact canonical LOD0 face at X/Z. The upright capsule's
// supported center is surface.y + half-segment + radius / normal.y.
[[nodiscard]] core::Result<std::optional<PlayerTerrainSupport>>
query_player_terrain_support(
    const PlayerCapsuleShape& shape,
    const PlayerGroundingSettings& settings,
    const terrain::HeightTileSurface& terrain_surface,
    float world_x,
    float world_z);

// Canonicalizes signed zero and wraps finite spawn yaw to [-pi, pi). Spawn is
// classified against canonical terrain; nearby support is snapped exactly.
[[nodiscard]] core::Result<PlayerCapsuleSimulation>
create_player_capsule(
    PlayerCapsuleConfig config,
    const terrain::HeightTileSurface& terrain_surface);

// fixed_tick must be exactly current.fixed_tick + 1. The operation validates
// source terrain consistency and its complete candidate before committing.
[[nodiscard]] core::Result<void> advance_player_capsule(
    PlayerCapsuleSimulation& simulation,
    PlayerActionCommand command,
    const terrain::HeightTileSurface& terrain_surface,
    float fixed_delta_seconds,
    std::uint64_t fixed_tick);

// Collapses only presentation history after a time-baseline discontinuity.
// Tick, command, generation, config, and current authority remain unchanged.
[[nodiscard]] core::Result<void>
collapse_player_capsule_interpolation(
    PlayerCapsuleSimulation& simulation);

// Produces a presentation-only state without changing either authoritative
// snapshot. Position is linear; yaw follows the shortest wrapped arc.
[[nodiscard]] core::Result<PlayerCapsuleState>
interpolate_player_capsule(
    const PlayerCapsuleSimulation& simulation,
    float alpha);

} // namespace shark::character
