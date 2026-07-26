#include <shark/character/player_capsule.hpp>

#include <shark/core/error.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::character {
namespace {

inline constexpr double terrain_contact_tolerance = 0.00001;

struct SpawnClassification final {
    float center_position_y{};
    PlayerVerticalState vertical;
};

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

[[nodiscard]] bool positive_zero(const float value) noexcept
{
    return value == 0.0F && !std::signbit(value);
}

[[nodiscard]] bool canonical_zero_vector(
    const math::Float3 value) noexcept
{
    return positive_zero(value.x) &&
        positive_zero(value.y) &&
        positive_zero(value.z);
}

[[nodiscard]] bool representable_float(
    const double value) noexcept
{
    return std::isfinite(value) &&
        std::abs(value) <=
            static_cast<double>(
                std::numeric_limits<float>::max());
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

[[nodiscard]] float wrap_yaw(const double yaw) noexcept
{
    auto wrapped = std::remainder(
        yaw,
        static_cast<double>(math::two_pi));
    if (wrapped >= static_cast<double>(math::pi)) {
        wrapped -= static_cast<double>(math::two_pi);
    }
    auto result = static_cast<float>(wrapped);
    if (result >= math::pi) {
        result -= math::two_pi;
    }
    return canonical_zero(result);
}

[[nodiscard]] bool valid_surface_normal(
    const math::Float3 normal) noexcept
{
    const auto length_squared =
        static_cast<double>(normal.x) * normal.x +
        static_cast<double>(normal.y) * normal.y +
        static_cast<double>(normal.z) * normal.z;
    return math::is_finite(normal) &&
        normal.y > 0.0F &&
        std::isfinite(length_squared) &&
        std::abs(length_squared - 1.0) <= 0.00001;
}

[[nodiscard]] bool valid_vertical_state(
    const PlayerVerticalState& vertical,
    const PlayerGroundingSettings& settings) noexcept
{
    if (!std::isfinite(vertical.velocity_y) ||
        !math::is_finite(vertical.support_normal)) {
        return false;
    }

    switch (vertical.phase) {
    case PlayerGroundPhase::grounded:
    case PlayerGroundPhase::landing:
        return positive_zero(vertical.velocity_y) &&
            valid_surface_normal(vertical.support_normal) &&
            vertical.support_normal.y >=
                settings.minimum_walkable_normal_y;
    case PlayerGroundPhase::steep_contact:
        return positive_zero(vertical.velocity_y) &&
            valid_surface_normal(vertical.support_normal) &&
            vertical.support_normal.y <
                settings.minimum_walkable_normal_y;
    case PlayerGroundPhase::falling:
        return vertical.velocity_y <= 0.0F &&
            canonical_zero_vector(vertical.support_normal);
    default:
        return false;
    }
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
        canonical_yaw(config.spawn_facing_yaw_radians) &&
        is_valid(config.grounding);
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

[[nodiscard]] PlayerVerticalState supported_vertical_state(
    const PlayerTerrainSupport& support,
    const bool landing) noexcept
{
    return {
        .velocity_y = 0.0F,
        .phase = support.walkable
            ? (landing
                ? PlayerGroundPhase::landing
                : PlayerGroundPhase::grounded)
            : PlayerGroundPhase::steep_contact,
        .support_normal = support.surface.normal,
    };
}

[[nodiscard]] PlayerVerticalState falling_vertical_state(
    const float velocity_y = 0.0F) noexcept
{
    return {
        .velocity_y = canonical_zero(velocity_y),
        .phase = PlayerGroundPhase::falling,
        .support_normal = {},
    };
}

[[nodiscard]] core::Result<SpawnClassification>
classify_spawn(
    const PlayerCapsuleConfig& config,
    const terrain::HeightTileSurface& terrain_surface,
    const core::ErrorCode below_support_code)
{
    auto support_result = query_player_terrain_support(
        config.shape,
        config.grounding,
        terrain_surface,
        config.spawn_center_position.x,
        config.spawn_center_position.z);
    if (!support_result) {
        return core::Result<SpawnClassification>::failure(
            support_result.error());
    }
    if (!support_result.value()) {
        return core::Result<SpawnClassification>::success({
            .center_position_y =
                config.spawn_center_position.y,
            .vertical = falling_vertical_state(),
        });
    }

    const auto& support = *support_result.value();
    const auto separation =
        static_cast<double>(config.spawn_center_position.y) -
        static_cast<double>(support.center_position_y);
    if (separation < -terrain_contact_tolerance) {
        return core::Result<SpawnClassification>::failure(
            character_error(
                below_support_code,
                "Player spawn center begins below canonical terrain "
                "support"));
    }
    if (separation <=
        static_cast<double>(config.grounding.snap_distance)) {
        return core::Result<SpawnClassification>::success({
            .center_position_y = support.center_position_y,
            .vertical = supported_vertical_state(
                support,
                false),
        });
    }
    return core::Result<SpawnClassification>::success({
        .center_position_y =
            config.spawn_center_position.y,
        .vertical = falling_vertical_state(),
    });
}

[[nodiscard]] core::Result<void> validate_source_support(
    const PlayerCapsuleSimulation& simulation,
    const std::optional<PlayerTerrainSupport>& support)
{
    const auto& state = simulation.current.state;
    const auto& vertical = simulation.current.vertical;
    if (!support) {
        if (vertical.phase != PlayerGroundPhase::falling) {
            return core::Result<void>::failure(character_error(
                core::ErrorCode::invalid_state,
                "Supported player state has no canonical terrain "
                "sample"));
        }
        return core::Result<void>::success();
    }

    const auto separation =
        static_cast<double>(state.center_position.y) -
        static_cast<double>(support->center_position_y);
    if (separation < -terrain_contact_tolerance) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::invalid_state,
            "Player center begins below canonical terrain support"));
    }

    if (vertical.phase == PlayerGroundPhase::falling) {
        return core::Result<void>::success();
    }

    const auto expected_walkable =
        vertical.phase == PlayerGroundPhase::grounded ||
        vertical.phase == PlayerGroundPhase::landing;
    if (support->walkable != expected_walkable ||
        state.center_position.y != support->center_position_y ||
        vertical.support_normal != support->surface.normal) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::invalid_state,
            "Player support snapshot disagrees with canonical "
            "terrain"));
    }
    return core::Result<void>::success();
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

bool is_valid(
    const PlayerGroundingSettings& settings) noexcept
{
    return std::isfinite(settings.gravity_magnitude) &&
        settings.gravity_magnitude > 0.0F &&
        settings.gravity_magnitude <=
            maximum_player_gravity_magnitude &&
        std::isfinite(settings.minimum_walkable_normal_y) &&
        settings.minimum_walkable_normal_y > 0.0F &&
        settings.minimum_walkable_normal_y <= 1.0F &&
        std::isfinite(settings.snap_distance) &&
        settings.snap_distance >= 0.0F &&
        settings.snap_distance <=
            maximum_player_ground_snap_distance;
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
        !valid_vertical_state(
            simulation.previous.vertical,
            simulation.config.grounding) ||
        !valid_vertical_state(
            simulation.current.vertical,
            simulation.config.grounding) ||
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
            simulation.previous.vertical ==
                simulation.current.vertical &&
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

core::Result<std::optional<PlayerTerrainSupport>>
query_player_terrain_support(
    const PlayerCapsuleShape& shape,
    const PlayerGroundingSettings& settings,
    const terrain::HeightTileSurface& terrain_surface,
    const float world_x,
    const float world_z)
{
    if (!is_valid(shape) ||
        !is_valid(settings) ||
        !std::isfinite(world_x) ||
        !std::isfinite(world_z)) {
        return core::Result<
            std::optional<PlayerTerrainSupport>>::failure(
                character_error(
                    core::ErrorCode::invalid_argument,
                    "Player terrain support requires valid shape, "
                    "grounding settings, and finite X/Z"));
    }

    const auto surface =
        terrain_surface.sample_lod0_surface(world_x, world_z);
    if (!surface) {
        return core::Result<
            std::optional<PlayerTerrainSupport>>::success(
                std::nullopt);
    }
    if (!math::is_finite(surface->position) ||
        surface->position.x != world_x ||
        surface->position.z != world_z ||
        !valid_surface_normal(surface->normal)) {
        return core::Result<
            std::optional<PlayerTerrainSupport>>::failure(
                character_error(
                    core::ErrorCode::invalid_state,
                    "Canonical terrain returned an invalid player "
                    "support sample"));
    }

    const auto center_position_y =
        static_cast<double>(surface->position.y) +
        static_cast<double>(shape.vertical_half_segment) +
        static_cast<double>(shape.radius) /
            static_cast<double>(surface->normal.y);
    if (!representable_float(center_position_y)) {
        return core::Result<
            std::optional<PlayerTerrainSupport>>::failure(
                character_error(
                    core::ErrorCode::unavailable,
                    "Player terrain support exceeded finite float "
                    "range"));
    }

    return core::Result<
        std::optional<PlayerTerrainSupport>>::success(
            PlayerTerrainSupport{
                .surface = *surface,
                .center_position_y =
                    canonical_zero(
                        static_cast<float>(center_position_y)),
                .walkable =
                    surface->normal.y >=
                    settings.minimum_walkable_normal_y,
            });
}

core::Result<PlayerCapsuleSimulation>
create_player_capsule(
    PlayerCapsuleConfig config,
    const terrain::HeightTileSurface& terrain_surface)
{
    if (!is_valid(config.shape) ||
        !ordered_bounds(config.center_bounds) ||
        !math::is_finite(config.spawn_center_position) ||
        !std::isfinite(config.spawn_facing_yaw_radians) ||
        !is_valid(config.grounding)) {
        return core::Result<PlayerCapsuleSimulation>::failure(
            character_error(
                core::ErrorCode::invalid_argument,
                "Player capsule requires finite bounded shape, "
                "center bounds, spawn pose, and grounding settings"));
    }

    config.center_bounds.minimum =
        canonical_zero(config.center_bounds.minimum);
    config.center_bounds.maximum =
        canonical_zero(config.center_bounds.maximum);
    config.spawn_center_position =
        canonical_zero(config.spawn_center_position);
    config.spawn_facing_yaw_radians =
        wrap_yaw(
            static_cast<double>(
                config.spawn_facing_yaw_radians));
    config.grounding.snap_distance =
        canonical_zero(config.grounding.snap_distance);
    if (!valid_config(config)) {
        return core::Result<PlayerCapsuleSimulation>::failure(
            character_error(
                core::ErrorCode::invalid_argument,
                "Player-capsule spawn center must lie inside its "
                "inclusive finite center bounds"));
    }

    auto spawn_result = classify_spawn(
        config,
        terrain_surface,
        core::ErrorCode::invalid_argument);
    if (!spawn_result) {
        return core::Result<PlayerCapsuleSimulation>::failure(
            spawn_result.error());
    }
    config.spawn_center_position.y =
        spawn_result.value().center_position_y;
    if (!valid_config(config)) {
        return core::Result<PlayerCapsuleSimulation>::failure(
            character_error(
                core::ErrorCode::invalid_argument,
                "Canonical player spawn support lies outside center "
                "bounds"));
    }

    const PlayerCapsuleSnapshot initial{
        .state = spawn_state(config),
        .vertical = spawn_result.value().vertical,
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
    const terrain::HeightTileSurface& terrain_surface,
    const float fixed_delta_seconds,
    const std::uint64_t fixed_tick)
{
    if (!is_valid(simulation) ||
        !is_valid(command) ||
        !std::isfinite(fixed_delta_seconds) ||
        fixed_delta_seconds <= 0.0F ||
        fixed_delta_seconds >
            maximum_player_fixed_delta_seconds) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::invalid_argument,
            "Player-capsule advance requires valid state, command, "
            "and fixed delta in (0, 0.25] seconds"));
    }
    if (simulation.current.fixed_tick ==
        std::numeric_limits<std::uint64_t>::max()) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::unavailable,
            "Player-capsule fixed-tick counter would overflow"));
    }
    if (fixed_tick != simulation.current.fixed_tick + 1U) {
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

    auto support_result = query_player_terrain_support(
        simulation.config.shape,
        simulation.config.grounding,
        terrain_surface,
        simulation.current.state.center_position.x,
        simulation.current.state.center_position.z);
    if (!support_result) {
        return core::Result<void>::failure(
            support_result.error());
    }
    auto source_result = validate_source_support(
        simulation,
        support_result.value());
    if (!source_result) {
        return core::Result<void>::failure(
            source_result.error());
    }

    command.look_yaw_delta_radians =
        canonical_zero(command.look_yaw_delta_radians);
    command.look_pitch_delta_radians =
        canonical_zero(command.look_pitch_delta_radians);

    auto candidate = simulation;
    if (command.reset_pressed) {
        auto spawn_result = classify_spawn(
            simulation.config,
            terrain_surface,
            core::ErrorCode::invalid_state);
        if (!spawn_result) {
            return core::Result<void>::failure(
                spawn_result.error());
        }
        const auto generation =
            simulation.current.reset_generation + 1U;
        auto spawn = spawn_state(simulation.config);
        spawn.center_position.y =
            spawn_result.value().center_position_y;
        candidate.previous = PlayerCapsuleSnapshot{
            .state = spawn,
            .vertical = spawn_result.value().vertical,
            .fixed_tick = fixed_tick - 1U,
            .reset_generation = generation,
        };
        candidate.current = PlayerCapsuleSnapshot{
            .state = spawn,
            .vertical = spawn_result.value().vertical,
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

        if (simulation.current.vertical.phase !=
            PlayerGroundPhase::falling) {
            const auto& support = *support_result.value();
            candidate.current.state.center_position.y =
                support.center_position_y;
            candidate.current.vertical =
                supported_vertical_state(
                    support,
                    false);
        }
        else {
            const auto velocity_y =
                static_cast<double>(
                    simulation.current.vertical.velocity_y) -
                static_cast<double>(
                    simulation.config.grounding.gravity_magnitude) *
                    static_cast<double>(fixed_delta_seconds);
            const auto position_y =
                static_cast<double>(
                    simulation.current.state.center_position.y) +
                velocity_y *
                    static_cast<double>(fixed_delta_seconds);
            if (!representable_float(velocity_y) ||
                !representable_float(position_y)) {
                return core::Result<void>::failure(
                    character_error(
                        core::ErrorCode::unavailable,
                        "Player vertical integration exceeded finite "
                        "float range"));
            }

            if (support_result.value() &&
                position_y <=
                    static_cast<double>(
                        support_result.value()->
                            center_position_y) +
                    static_cast<double>(
                        simulation.config.grounding.snap_distance)) {
                const auto& support =
                    *support_result.value();
                candidate.current.state.center_position.y =
                    support.center_position_y;
                candidate.current.vertical =
                    supported_vertical_state(
                        support,
                        true);
            }
            else {
                candidate.current.state.center_position.y =
                    canonical_zero(
                        static_cast<float>(position_y));
                candidate.current.vertical =
                    falling_vertical_state(
                        static_cast<float>(velocity_y));
            }
        }
    }

    if (!is_valid(candidate)) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::unavailable,
            "Player-capsule advance could not publish valid bounded "
            "snapshots"));
    }
    simulation = candidate;
    return core::Result<void>::success();
}

core::Result<void>
collapse_player_capsule_interpolation(
    PlayerCapsuleSimulation& simulation)
{
    if (!is_valid(simulation)) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::invalid_argument,
            "Player-capsule interpolation collapse requires valid "
            "authoritative state"));
    }

    auto candidate = simulation;
    candidate.previous.state = candidate.current.state;
    candidate.previous.vertical =
        candidate.current.vertical;
    if (!is_valid(candidate)) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::unavailable,
            "Player-capsule could not collapse its interpolation "
            "history"));
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
        .facing_yaw_radians = wrap_yaw(interpolated_yaw),
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
