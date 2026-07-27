#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/water/gameplay_water.hpp>

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
inline constexpr float default_player_walk_speed = 4.0F;
inline constexpr float default_player_run_speed = 7.0F;
inline constexpr float default_player_ground_acceleration = 24.0F;
inline constexpr float default_player_ground_braking_deceleration = 32.0F;
inline constexpr float
    default_player_facing_turn_speed_radians_per_second = 10.0F;
inline constexpr float default_player_maximum_probe_spacing = 0.25F;
inline constexpr float maximum_player_horizontal_speed = 32.0F;
inline constexpr float maximum_player_ground_acceleration = 256.0F;
inline constexpr float
    maximum_player_facing_turn_speed_radians_per_second = 32.0F;
inline constexpr float default_player_jump_launch_speed = 6.5F;
inline constexpr float maximum_player_jump_launch_speed = 32.0F;
inline constexpr float
    default_player_air_control_acceleration = 12.0F;
inline constexpr float
    maximum_player_air_control_acceleration = 256.0F;
inline constexpr float default_player_wading_enter_depth = 0.25F;
inline constexpr float default_player_wading_exit_depth = 0.125F;
inline constexpr float
    default_player_wading_depth_for_minimum_speed = 1.5F;
inline constexpr float
    default_player_wading_minimum_speed_multiplier = 0.5F;
inline constexpr float maximum_player_wading_depth = 32.0F;
inline constexpr float minimum_player_probe_spacing = 0.01F;
inline constexpr float maximum_player_probe_spacing = 1.0F;
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

struct PlayerGroundLocomotionSettings final {
    float walk_speed{default_player_walk_speed};
    float run_speed{default_player_run_speed};
    float acceleration{default_player_ground_acceleration};
    float braking_deceleration{
        default_player_ground_braking_deceleration};
    float facing_turn_speed_radians_per_second{
        default_player_facing_turn_speed_radians_per_second};
    float maximum_probe_spacing{
        default_player_maximum_probe_spacing};

    [[nodiscard]] friend bool operator==(
        const PlayerGroundLocomotionSettings&,
        const PlayerGroundLocomotionSettings&) noexcept = default;
};

struct PlayerAirLocomotionSettings final {
    float jump_launch_speed{default_player_jump_launch_speed};
    float control_acceleration{
        default_player_air_control_acceleration};

    [[nodiscard]] friend bool operator==(
        const PlayerAirLocomotionSettings&,
        const PlayerAirLocomotionSettings&) noexcept = default;
};

struct PlayerWadingSettings final {
    float enter_depth{default_player_wading_enter_depth};
    float exit_depth{default_player_wading_exit_depth};
    float depth_for_minimum_speed{
        default_player_wading_depth_for_minimum_speed};
    float minimum_speed_multiplier{
        default_player_wading_minimum_speed_multiplier};

    [[nodiscard]] friend bool operator==(
        const PlayerWadingSettings&,
        const PlayerWadingSettings&) noexcept = default;
};

// Character-owned, device-neutral horizontal frame supplied once per fixed
// tick. At camera yaw zero, right is +X and forward is -Z.
struct PlayerMovementFrame final {
    math::Float3 right{1.0F, 0.0F, 0.0F};
    math::Float3 forward{0.0F, 0.0F, -1.0F};

    [[nodiscard]] friend bool operator==(
        const PlayerMovementFrame&,
        const PlayerMovementFrame&) noexcept = default;
};

enum class PlayerGroundPhase : std::uint8_t {
    grounded = 1,
    rising,
    falling,
    landing,
    steep_contact,
};

enum class PlayerWaterPhase : std::uint8_t {
    dry = 1,
    wading,
};

// This is the water state consumed at the authoritative tick-start position,
// not a query of the post-movement position. Dry state always carries
// canonical positive-zero depth.
struct PlayerWaterState final {
    PlayerWaterPhase phase{PlayerWaterPhase::dry};
    float depth{};

    [[nodiscard]] friend bool operator==(
        const PlayerWaterState&,
        const PlayerWaterState&) noexcept = default;
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
    PlayerGroundLocomotionSettings ground_locomotion;
    PlayerAirLocomotionSettings air_locomotion;
    PlayerWadingSettings wading;

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
// pulses supplied by the platform-facing sampler. Character consumes the
// directional holds, run, jump, and reset; the camera consumes look deltas.
// Primary action policy remains deferred.
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
    PlayerWaterState water;
    math::Float3 horizontal_velocity{};
    PlayerActionCommand consumed_command;
    PlayerMovementFrame consumed_movement_frame;
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
static_assert(
    std::is_standard_layout_v<PlayerGroundLocomotionSettings>);
static_assert(
    std::is_trivially_copyable_v<PlayerGroundLocomotionSettings>);
static_assert(std::is_standard_layout_v<PlayerAirLocomotionSettings>);
static_assert(
    std::is_trivially_copyable_v<PlayerAirLocomotionSettings>);
static_assert(std::is_standard_layout_v<PlayerWadingSettings>);
static_assert(std::is_trivially_copyable_v<PlayerWadingSettings>);
static_assert(std::is_standard_layout_v<PlayerMovementFrame>);
static_assert(std::is_trivially_copyable_v<PlayerMovementFrame>);
static_assert(std::is_standard_layout_v<PlayerVerticalState>);
static_assert(std::is_trivially_copyable_v<PlayerVerticalState>);
static_assert(std::is_standard_layout_v<PlayerWaterState>);
static_assert(std::is_trivially_copyable_v<PlayerWaterState>);
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
    const PlayerGroundLocomotionSettings& settings) noexcept;

[[nodiscard]] bool is_valid(
    const PlayerAirLocomotionSettings& settings) noexcept;

[[nodiscard]] bool is_valid(
    const PlayerWadingSettings& settings) noexcept;

[[nodiscard]] bool is_valid(
    const PlayerMovementFrame& frame) noexcept;

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

// fixed_tick must be exactly current.fixed_tick + 1. Grounded and airborne
// movement use the supplied frame plus bounded canonical terrain probes.
// Airborne collision is a deterministic sampled center path, not exact
// continuous capsule collision. gameplay_water must be the one WQ result
// sampled at the authoritative tick-start X/Z; a crossing is therefore
// reclassified on the following emitted tick. The operation validates source
// terrain/water consistency and its complete candidate before committing.
[[nodiscard]] core::Result<void> advance_player_capsule(
    PlayerCapsuleSimulation& simulation,
    PlayerActionCommand command,
    PlayerMovementFrame movement_frame,
    const terrain::HeightTileSurface& terrain_surface,
    const water::GameplayWaterQuery& gameplay_water,
    float fixed_delta_seconds,
    std::uint64_t fixed_tick);

// Collapses pose, vertical, water, and horizontal presentation history after
// a time-baseline discontinuity. Tick, consumed inputs, generation, config,
// and current authority remain unchanged.
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
