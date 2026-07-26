#include <shark/character/player_capsule.hpp>

#include <shark/core/error.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::character {
namespace {

[[nodiscard]] core::Error character_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] float canonical_zero(const float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] math::Float3 canonical_zero(
    const math::Float3 value) noexcept
{
    return {
        canonical_zero(value.x),
        canonical_zero(value.y),
        canonical_zero(value.z),
    };
}

[[nodiscard]] bool ordered_bounds(
    const PlayerCapsuleCenterBounds& bounds) noexcept
{
    return math::is_finite(bounds.minimum) &&
        math::is_finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool contains(
    const PlayerCapsuleCenterBounds& bounds,
    const math::Float3 value) noexcept
{
    return math::is_finite(value) &&
        value.x >= bounds.minimum.x &&
        value.x <= bounds.maximum.x &&
        value.y >= bounds.minimum.y &&
        value.y <= bounds.maximum.y &&
        value.z >= bounds.minimum.z &&
        value.z <= bounds.maximum.z;
}

[[nodiscard]] bool canonical_yaw(const float yaw) noexcept
{
    return std::isfinite(yaw) &&
        yaw >= -math::pi &&
        yaw < math::pi &&
        (yaw != 0.0F || !std::signbit(yaw));
}

[[nodiscard]] float wrap_yaw(const float yaw) noexcept
{
    auto wrapped = static_cast<float>(std::remainder(
        static_cast<double>(yaw),
        static_cast<double>(math::two_pi)));
    if (wrapped >= math::pi) {
        wrapped -= math::two_pi;
    }
    return canonical_zero(wrapped);
}

[[nodiscard]] bool valid_state(
    const PlayerCapsuleState& state,
    const PlayerCapsuleCenterBounds& bounds) noexcept
{
    return contains(bounds, state.center_position) &&
        canonical_yaw(state.facing_yaw_radians);
}

[[nodiscard]] bool neutral_command(
    const PlayerActionCommand& command) noexcept
{
    return command == PlayerActionCommand{};
}

[[nodiscard]] bool valid_config(
    const PlayerCapsuleConfig& config) noexcept
{
    return is_valid(config.shape) &&
        ordered_bounds(config.center_bounds) &&
        contains(
            config.center_bounds,
            config.spawn_center_position) &&
        canonical_yaw(config.spawn_facing_yaw_radians);
}

[[nodiscard]] PlayerCapsuleState spawn_state(
    const PlayerCapsuleConfig& config) noexcept
{
    return {
        .center_position = config.spawn_center_position,
        .facing_yaw_radians =
            config.spawn_facing_yaw_radians,
    };
}

[[nodiscard]] bool representable_float(
    const double value) noexcept
{
    return std::isfinite(value) &&
        std::abs(value) <=
            static_cast<double>(
                std::numeric_limits<float>::max());
}

[[nodiscard]] core::Result<float> interpolate_component(
    const float previous,
    const float current,
    const double alpha)
{
    const auto value =
        static_cast<double>(previous) +
        (static_cast<double>(current) - previous) * alpha;
    if (!representable_float(value)) {
        return core::Result<float>::failure(character_error(
            core::ErrorCode::unavailable,
            "Player-capsule interpolation exceeded finite float range"));
    }
    return core::Result<float>::success(
        canonical_zero(static_cast<float>(value)));
}

} // namespace

bool is_valid(const PlayerCapsuleShape& shape) noexcept
{
    if (!std::isfinite(shape.radius) ||
        shape.radius <= 0.0F ||
        shape.radius > maximum_player_capsule_radius ||
        !std::isfinite(shape.vertical_half_segment) ||
        shape.vertical_half_segment <= 0.0F ||
        shape.vertical_half_segment >
            maximum_player_capsule_vertical_half_segment) {
        return false;
    }

    const auto vertical_extent =
        static_cast<double>(shape.radius) +
        static_cast<double>(shape.vertical_half_segment);
    return representable_float(vertical_extent) &&
        static_cast<float>(vertical_extent) > 0.0F;
}

bool is_valid(const PlayerActionCommand& command) noexcept
{
    return std::isfinite(command.look_yaw_delta_radians) &&
        std::isfinite(command.look_pitch_delta_radians) &&
        std::abs(command.look_yaw_delta_radians) <=
            maximum_player_look_delta_radians &&
        std::abs(command.look_pitch_delta_radians) <=
            maximum_player_look_delta_radians;
}

bool is_valid(
    const PlayerCapsuleSimulation& simulation) noexcept
{
    if (!valid_config(simulation.config) ||
        !valid_state(
            simulation.previous.state,
            simulation.config.center_bounds) ||
        !valid_state(
            simulation.current.state,
            simulation.config.center_bounds) ||
        !is_valid(simulation.previous.consumed_command) ||
        !is_valid(simulation.current.consumed_command) ||
        simulation.previous.reset_generation !=
            simulation.current.reset_generation ||
        simulation.current.reset_generation >
            simulation.current.fixed_tick) {
        return false;
    }

    if (simulation.current.fixed_tick == 0U) {
        return simulation.previous.fixed_tick == 0U &&
            simulation.previous.reset_generation == 0U &&
            simulation.previous.state ==
                spawn_state(simulation.config) &&
            simulation.current.state ==
                spawn_state(simulation.config) &&
            neutral_command(
                simulation.previous.consumed_command) &&
            neutral_command(
                simulation.current.consumed_command);
    }

    return simulation.previous.fixed_tick !=
            std::numeric_limits<std::uint64_t>::max() &&
        simulation.previous.fixed_tick + 1U ==
            simulation.current.fixed_tick;
}

core::Result<PlayerCapsuleSimulation>
create_player_capsule(PlayerCapsuleConfig config)
{
    if (!is_valid(config.shape) ||
        !ordered_bounds(config.center_bounds) ||
        !math::is_finite(config.spawn_center_position) ||
        !std::isfinite(config.spawn_facing_yaw_radians)) {
        return core::Result<PlayerCapsuleSimulation>::failure(
            character_error(
                core::ErrorCode::invalid_argument,
                "Player capsule requires a finite positive shape, "
                "finite ordered center bounds, and finite spawn pose"));
    }

    config.shape.vertical_half_segment =
        canonical_zero(config.shape.vertical_half_segment);
    config.center_bounds.minimum =
        canonical_zero(config.center_bounds.minimum);
    config.center_bounds.maximum =
        canonical_zero(config.center_bounds.maximum);
    config.spawn_center_position =
        canonical_zero(config.spawn_center_position);
    config.spawn_facing_yaw_radians =
        wrap_yaw(config.spawn_facing_yaw_radians);

    if (!valid_config(config)) {
        return core::Result<PlayerCapsuleSimulation>::failure(
            character_error(
                core::ErrorCode::invalid_argument,
                "Player-capsule spawn center must lie inside its "
                "inclusive finite center bounds"));
    }

    const PlayerCapsuleSnapshot initial{
        .state = spawn_state(config),
    };
    PlayerCapsuleSimulation simulation{
        .config = config,
        .previous = initial,
        .current = initial,
    };
    if (!is_valid(simulation)) {
        return core::Result<PlayerCapsuleSimulation>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player-capsule creation could not publish canonical "
                "initial snapshots"));
    }
    return core::Result<PlayerCapsuleSimulation>::success(
        simulation);
}

core::Result<void> advance_player_capsule(
    PlayerCapsuleSimulation& simulation,
    PlayerActionCommand command,
    const std::uint64_t fixed_tick)
{
    if (!is_valid(simulation) || !is_valid(command)) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::invalid_argument,
            "Player-capsule advance requires valid authoritative state "
            "and a bounded finite action command"));
    }
    if (simulation.current.fixed_tick ==
        std::numeric_limits<std::uint64_t>::max()) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::unavailable,
            "Player-capsule fixed-tick counter would overflow"));
    }
    const auto expected_tick =
        simulation.current.fixed_tick + 1U;
    if (fixed_tick != expected_tick) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::invalid_argument,
            "Player-capsule fixed ticks must be consecutive"));
    }
    if (command.reset_pressed &&
        simulation.current.reset_generation ==
            std::numeric_limits<std::uint64_t>::max()) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::unavailable,
            "Player-capsule reset generation would overflow"));
    }

    command.look_yaw_delta_radians =
        canonical_zero(command.look_yaw_delta_radians);
    command.look_pitch_delta_radians =
        canonical_zero(command.look_pitch_delta_radians);

    auto candidate = simulation;
    if (command.reset_pressed) {
        const auto generation =
            simulation.current.reset_generation + 1U;
        const auto spawn = spawn_state(simulation.config);
        candidate.previous = PlayerCapsuleSnapshot{
            .state = spawn,
            .fixed_tick = fixed_tick - 1U,
            .reset_generation = generation,
        };
        candidate.current = PlayerCapsuleSnapshot{
            .state = spawn,
            .consumed_command = command,
            .fixed_tick = fixed_tick,
            .reset_generation = generation,
        };
    }
    else {
        candidate.previous = simulation.current;
        candidate.current = simulation.current;
        candidate.current.consumed_command = command;
        candidate.current.fixed_tick = fixed_tick;
    }

    if (!is_valid(candidate)) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::unavailable,
            "Player-capsule advance could not publish valid ordered "
            "snapshots"));
    }
    simulation = candidate;
    return core::Result<void>::success();
}

core::Result<PlayerCapsuleState>
interpolate_player_capsule(
    const PlayerCapsuleSimulation& simulation,
    const float alpha)
{
    if (!is_valid(simulation) ||
        !std::isfinite(alpha) ||
        alpha < 0.0F ||
        alpha > 1.0F) {
        return core::Result<PlayerCapsuleState>::failure(
            character_error(
                core::ErrorCode::invalid_argument,
                "Player-capsule interpolation requires valid ordered "
                "snapshots and alpha in [0, 1]"));
    }
    if (alpha == 0.0F) {
        return core::Result<PlayerCapsuleState>::success(
            simulation.previous.state);
    }
    if (alpha == 1.0F) {
        return core::Result<PlayerCapsuleState>::success(
            simulation.current.state);
    }

    const auto& previous =
        simulation.previous.state;
    const auto& current =
        simulation.current.state;
    std::array<core::Result<float>, 3> position{
        interpolate_component(
            previous.center_position.x,
            current.center_position.x,
            alpha),
        interpolate_component(
            previous.center_position.y,
            current.center_position.y,
            alpha),
        interpolate_component(
            previous.center_position.z,
            current.center_position.z,
            alpha),
    };
    for (const auto& component : position) {
        if (!component) {
            return core::Result<PlayerCapsuleState>::failure(
                component.error());
        }
    }

    const auto yaw_delta = std::remainder(
        static_cast<double>(current.facing_yaw_radians) -
            previous.facing_yaw_radians,
        static_cast<double>(math::two_pi));
    const auto interpolated_yaw =
        static_cast<double>(previous.facing_yaw_radians) +
        yaw_delta * static_cast<double>(alpha);
    if (!representable_float(interpolated_yaw)) {
        return core::Result<PlayerCapsuleState>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player-capsule yaw interpolation exceeded finite "
                "float range"));
    }

    const PlayerCapsuleState result{
        .center_position = {
            position[0].value(),
            position[1].value(),
            position[2].value(),
        },
        .facing_yaw_radians = wrap_yaw(
            static_cast<float>(interpolated_yaw)),
    };
    if (!valid_state(result, simulation.config.center_bounds)) {
        return core::Result<PlayerCapsuleState>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player-capsule interpolation left its finite center "
                "bounds"));
    }
    return core::Result<PlayerCapsuleState>::success(result);
}

} // namespace shark::character
