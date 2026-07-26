#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>

#include <cstdint>
#include <type_traits>

namespace shark::character {

inline constexpr float default_player_capsule_radius = 0.5F;
inline constexpr float
    default_player_capsule_vertical_half_segment = 0.5F;
inline constexpr float maximum_player_capsule_radius = 4.0F;
inline constexpr float
    maximum_player_capsule_vertical_half_segment = 4.0F;
inline constexpr float maximum_player_look_delta_radians = math::pi;

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
// pulses supplied by the platform-facing sampler. CHR-001 records every
// command but only reset_pressed changes authoritative pose.
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
    const PlayerActionCommand& command) noexcept;

[[nodiscard]] bool is_valid(
    const PlayerCapsuleSimulation& simulation) noexcept;

// Canonicalizes signed zero and wraps the finite spawn yaw to [-pi, pi).
// Both initial snapshots publish tick/generation zero and the exact same
// spawn pose.
[[nodiscard]] core::Result<PlayerCapsuleSimulation>
create_player_capsule(PlayerCapsuleConfig config);

// fixed_tick must be exactly current.fixed_tick + 1. The operation validates
// all source state and its candidate before committing. A reset collapses both
// snapshot poses to spawn, publishes ticks N-1/N in one new reset generation,
// and therefore cannot interpolate through a teleport.
[[nodiscard]] core::Result<void> advance_player_capsule(
    PlayerCapsuleSimulation& simulation,
    PlayerActionCommand command,
    std::uint64_t fixed_tick);

// Produces a presentation-only state without changing either authoritative
// snapshot. Position is linear; yaw follows the shortest wrapped arc.
[[nodiscard]] core::Result<PlayerCapsuleState>
interpolate_player_capsule(
    const PlayerCapsuleSimulation& simulation,
    float alpha);

} // namespace shark::character
