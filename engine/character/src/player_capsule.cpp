#include <shark/character/player_capsule.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::character {
namespace {

inline constexpr double terrain_contact_tolerance = 0.00001;
inline constexpr std::uint32_t maximum_airborne_probe_count = 4'096U;

struct SpawnClassification final {
    float center_position_y{};
    PlayerVerticalState vertical;
};

struct HorizontalIntent final {
    math::Float3 direction{};
    float target_speed{};
    bool active{};
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

[[nodiscard]] bool canonical_float(const float value) noexcept
{
    return std::isfinite(value) &&
        (value != 0.0F || !std::signbit(value));
}

[[nodiscard]] bool canonical_zero_vector(
    const math::Float3 value) noexcept
{
    return positive_zero(value.x) &&
        positive_zero(value.y) &&
        positive_zero(value.z);
}

[[nodiscard]] bool canonical_vector(
    const math::Float3 value) noexcept
{
    return math::is_finite(value) &&
        (value.x != 0.0F || !std::signbit(value.x)) &&
        (value.y != 0.0F || !std::signbit(value.y)) &&
        (value.z != 0.0F || !std::signbit(value.z));
}

[[nodiscard]] double horizontal_length_squared(
    const math::Float3 value) noexcept
{
    return static_cast<double>(value.x) * value.x +
        static_cast<double>(value.z) * value.z;
}

[[nodiscard]] double horizontal_dot(
    const math::Float3 left,
    const math::Float3 right) noexcept
{
    return static_cast<double>(left.x) * right.x +
        static_cast<double>(left.z) * right.z;
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

[[nodiscard]] bool contains_horizontal(
    const PlayerCapsuleCenterBounds& bounds,
    const float x,
    const float z) noexcept
{
    return std::isfinite(x) &&
        std::isfinite(z) &&
        x >= bounds.minimum.x &&
        x <= bounds.maximum.x &&
        z >= bounds.minimum.z &&
        z <= bounds.maximum.z;
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
    case PlayerGroundPhase::rising:
        return vertical.velocity_y > 0.0F &&
            canonical_zero_vector(vertical.support_normal);
    case PlayerGroundPhase::surface_swimming:
        return positive_zero(vertical.velocity_y) &&
            canonical_zero_vector(vertical.support_normal);
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

[[nodiscard]] bool valid_water_state(
    const PlayerWaterState& water_state,
    const PlayerVerticalState& vertical,
    const PlayerCapsuleState& state,
    const PlayerWadingSettings& wading,
    const PlayerSurfaceSwimmingSettings&
        surface_swimming) noexcept
{
    switch (water_state.phase) {
    case PlayerWaterPhase::dry:
        return positive_zero(water_state.depth) &&
            positive_zero(water_state.surface_height);
    case PlayerWaterPhase::wading:
        return std::isfinite(water_state.depth) &&
            water_state.depth > wading.exit_depth &&
            positive_zero(water_state.surface_height) &&
            (vertical.phase == PlayerGroundPhase::grounded ||
             vertical.phase == PlayerGroundPhase::landing);
    case PlayerWaterPhase::surface_swimming:
        return std::isfinite(water_state.depth) &&
            water_state.depth > surface_swimming.exit_depth &&
            canonical_float(water_state.surface_height) &&
            vertical.phase ==
                PlayerGroundPhase::surface_swimming &&
            static_cast<double>(state.center_position.y) >=
                static_cast<double>(
                    water_state.surface_height) -
                    static_cast<double>(
                        surface_swimming.surface_center_depth);
    default:
        return false;
    }
}

[[nodiscard]] bool valid_horizontal_velocity(
    const math::Float3 velocity,
    const PlayerGroundPhase phase,
    const PlayerGroundLocomotionSettings& settings) noexcept
{
    if (!canonical_vector(velocity) ||
        !positive_zero(velocity.y)) {
        return false;
    }
    const auto speed_squared =
        horizontal_length_squared(velocity);
    const auto maximum_speed =
        static_cast<double>(settings.run_speed);
    if (!std::isfinite(speed_squared) ||
        speed_squared >
            maximum_speed * maximum_speed + 0.00001) {
        return false;
    }
    if (phase == PlayerGroundPhase::steep_contact) {
        return canonical_zero_vector(velocity);
    }
    return true;
}

[[nodiscard]] bool valid_state(
    const PlayerCapsuleState& state,
    const PlayerCapsuleCenterBounds& bounds) noexcept
{
    return contains(bounds, state.center_position) &&
        canonical_yaw(state.facing_yaw_radians);
}

[[nodiscard]] bool canonical_movement_frame(
    const PlayerMovementFrame& frame) noexcept
{
    return canonical_vector(frame.right) &&
        canonical_vector(frame.forward);
}

[[nodiscard]] bool neutral_command(
    const PlayerActionCommand& command) noexcept
{
    return command == PlayerActionCommand{};
}

[[nodiscard]] bool neutral_movement_frame(
    const PlayerMovementFrame& frame) noexcept
{
    return frame == PlayerMovementFrame{};
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
        is_valid(config.grounding) &&
        is_valid(config.ground_locomotion) &&
        is_valid(config.air_locomotion) &&
        is_valid(config.wading) &&
        is_valid(config.surface_swimming) &&
        config.wading.enter_depth <
            config.surface_swimming.exit_depth &&
        config.surface_swimming.exit_depth <
            config.surface_swimming.enter_depth &&
        config.surface_swimming.enter_depth >=
            config.wading.depth_for_minimum_speed &&
        config.surface_swimming.speed <=
            config.ground_locomotion.run_speed;
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

[[nodiscard]] PlayerVerticalState
surface_swimming_vertical_state() noexcept
{
    return {
        .velocity_y = 0.0F,
        .phase = PlayerGroundPhase::surface_swimming,
        .support_normal = {},
    };
}

[[nodiscard]] PlayerVerticalState airborne_vertical_state(
    const float velocity_y) noexcept
{
    return {
        .velocity_y = canonical_zero(velocity_y),
        .phase = velocity_y > 0.0F
            ? PlayerGroundPhase::rising
            : PlayerGroundPhase::falling,
        .support_normal = {},
    };
}

[[nodiscard]] PlayerWaterState classify_tick_start_water(
    const PlayerWaterState& current,
    const PlayerVerticalState& vertical,
    const water::GameplayWaterQuery& query,
    const PlayerWadingSettings& settings) noexcept
{
    const auto supported =
        vertical.phase == PlayerGroundPhase::grounded ||
        vertical.phase == PlayerGroundPhase::landing;
    if (!supported ||
        query.disposition !=
            water::GameplayWaterDisposition::water) {
        return {};
    }

    const auto wading = current.phase == PlayerWaterPhase::wading
        ? query.depth > settings.exit_depth
        : query.depth >= settings.enter_depth;
    return wading
        ? PlayerWaterState{
              .phase = PlayerWaterPhase::wading,
              .depth = query.depth,
          }
        : PlayerWaterState{};
}

[[nodiscard]] float wading_speed_multiplier(
    const PlayerWaterState& water_state,
    const PlayerWadingSettings& settings) noexcept
{
    if (water_state.phase != PlayerWaterPhase::wading ||
        water_state.depth <= settings.enter_depth) {
        return 1.0F;
    }
    if (water_state.depth >=
        settings.depth_for_minimum_speed) {
        return settings.minimum_speed_multiplier;
    }

    const auto amount =
        (static_cast<double>(water_state.depth) -
         static_cast<double>(settings.enter_depth)) /
        (static_cast<double>(
             settings.depth_for_minimum_speed) -
         static_cast<double>(settings.enter_depth));
    const auto multiplier =
        1.0 -
        (1.0 -
         static_cast<double>(
             settings.minimum_speed_multiplier)) *
            amount;
    return canonical_zero(static_cast<float>(multiplier));
}

[[nodiscard]] HorizontalIntent scale_intent_for_wading(
    HorizontalIntent intent,
    const PlayerWaterState& water_state,
    const PlayerWadingSettings& settings) noexcept
{
    if (intent.active) {
        intent.target_speed = canonical_zero(
            intent.target_speed *
            wading_speed_multiplier(water_state, settings));
    }
    return intent;
}

[[nodiscard]] core::Result<HorizontalIntent>
build_horizontal_intent(
    const PlayerActionCommand& command,
    const PlayerMovementFrame& movement_frame,
    const PlayerGroundLocomotionSettings& settings)
{
    const auto right_axis =
        static_cast<double>(command.move_right_held) -
        static_cast<double>(command.move_left_held);
    const auto forward_axis =
        static_cast<double>(command.move_forward_held) -
        static_cast<double>(command.move_backward_held);
    const auto x =
        static_cast<double>(movement_frame.right.x) * right_axis +
        static_cast<double>(movement_frame.forward.x) * forward_axis;
    const auto z =
        static_cast<double>(movement_frame.right.z) * right_axis +
        static_cast<double>(movement_frame.forward.z) * forward_axis;
    const auto length_squared = x * x + z * z;
    if (!std::isfinite(length_squared)) {
        return core::Result<HorizontalIntent>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player horizontal intent exceeded finite range"));
    }
    if (length_squared == 0.0) {
        return core::Result<HorizontalIntent>::success({});
    }

    const auto inverse_length =
        1.0 / std::sqrt(length_squared);
    const auto normalized_x = x * inverse_length;
    const auto normalized_z = z * inverse_length;
    if (!representable_float(normalized_x) ||
        !representable_float(normalized_z)) {
        return core::Result<HorizontalIntent>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player horizontal intent could not be normalized"));
    }
    return core::Result<HorizontalIntent>::success({
        .direction = canonical_zero({
            static_cast<float>(normalized_x),
            0.0F,
            static_cast<float>(normalized_z),
        }),
        .target_speed = command.run_held
            ? settings.run_speed
            : settings.walk_speed,
        .active = true,
    });
}

[[nodiscard]] core::Result<math::Float3>
move_horizontal_velocity_toward(
    const math::Float3 current,
    const HorizontalIntent& intent,
    const PlayerGroundLocomotionSettings& settings,
    const float fixed_delta_seconds)
{
    const auto target_x = intent.active
        ? static_cast<double>(intent.direction.x) *
            intent.target_speed
        : 0.0;
    const auto target_z = intent.active
        ? static_cast<double>(intent.direction.z) *
            intent.target_speed
        : 0.0;
    const auto delta_x =
        target_x - static_cast<double>(current.x);
    const auto delta_z =
        target_z - static_cast<double>(current.z);
    const auto delta_length =
        std::sqrt(delta_x * delta_x + delta_z * delta_z);
    if (!std::isfinite(delta_length)) {
        return core::Result<math::Float3>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player horizontal velocity target exceeded finite "
                "range"));
    }
    if (delta_length == 0.0) {
        return core::Result<math::Float3>::success(current);
    }

    const auto current_speed =
        std::sqrt(horizontal_length_squared(current));
    const auto reversing =
        intent.active &&
        horizontal_dot(current, intent.direction) < 0.0;
    const auto slowing =
        !intent.active ||
        reversing ||
        current_speed >
            static_cast<double>(intent.target_speed);
    const auto rate = slowing
        ? settings.braking_deceleration
        : settings.acceleration;
    const auto maximum_delta =
        static_cast<double>(rate) *
        static_cast<double>(fixed_delta_seconds);
    const auto scale = std::min(
        1.0,
        maximum_delta / delta_length);
    const auto next_x =
        static_cast<double>(current.x) + delta_x * scale;
    const auto next_z =
        static_cast<double>(current.z) + delta_z * scale;
    if (!representable_float(next_x) ||
        !representable_float(next_z)) {
        return core::Result<math::Float3>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player horizontal integration exceeded finite "
                "range"));
    }
    return core::Result<math::Float3>::success(
        canonical_zero({
            static_cast<float>(next_x),
            0.0F,
            static_cast<float>(next_z),
        }));
}

[[nodiscard]] core::Result<math::Float3>
move_air_horizontal_velocity_toward(
    const math::Float3 current,
    const HorizontalIntent& intent,
    const PlayerAirLocomotionSettings& settings,
    const float fixed_delta_seconds)
{
    if (!intent.active) {
        return core::Result<math::Float3>::success(current);
    }

    const auto target_x =
        static_cast<double>(intent.direction.x) *
        intent.target_speed;
    const auto target_z =
        static_cast<double>(intent.direction.z) *
        intent.target_speed;
    const auto delta_x =
        target_x - static_cast<double>(current.x);
    const auto delta_z =
        target_z - static_cast<double>(current.z);
    const auto delta_length =
        std::sqrt(delta_x * delta_x + delta_z * delta_z);
    if (!std::isfinite(delta_length)) {
        return core::Result<math::Float3>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player airborne velocity target exceeded finite "
                "range"));
    }
    if (delta_length == 0.0) {
        return core::Result<math::Float3>::success(current);
    }

    const auto maximum_delta =
        static_cast<double>(settings.control_acceleration) *
        static_cast<double>(fixed_delta_seconds);
    const auto scale = std::min(
        1.0,
        maximum_delta / delta_length);
    const auto next_x =
        static_cast<double>(current.x) + delta_x * scale;
    const auto next_z =
        static_cast<double>(current.z) + delta_z * scale;
    if (!representable_float(next_x) ||
        !representable_float(next_z)) {
        return core::Result<math::Float3>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player airborne horizontal integration exceeded "
                "finite range"));
    }
    return core::Result<math::Float3>::success(
        canonical_zero({
            static_cast<float>(next_x),
            0.0F,
            static_cast<float>(next_z),
        }));
}

[[nodiscard]] core::Result<float> turn_facing_toward_intent(
    const float current_yaw,
    const HorizontalIntent& intent,
    const PlayerGroundLocomotionSettings& settings,
    const float fixed_delta_seconds)
{
    if (!intent.active) {
        return core::Result<float>::success(current_yaw);
    }
    const auto desired_yaw = std::atan2(
        static_cast<double>(intent.direction.x),
        -static_cast<double>(intent.direction.z));
    const auto yaw_delta = std::remainder(
        desired_yaw - static_cast<double>(current_yaw),
        static_cast<double>(math::two_pi));
    const auto maximum_turn =
        static_cast<double>(
            settings.facing_turn_speed_radians_per_second) *
        static_cast<double>(fixed_delta_seconds);
    const auto applied_turn = std::clamp(
        yaw_delta,
        -maximum_turn,
        maximum_turn);
    const auto next_yaw =
        static_cast<double>(current_yaw) + applied_turn;
    if (!representable_float(next_yaw)) {
        return core::Result<float>::failure(character_error(
            core::ErrorCode::unavailable,
            "Player facing integration exceeded finite range"));
    }
    return core::Result<float>::success(wrap_yaw(next_yaw));
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

[[nodiscard]] core::Result<PlayerCapsuleSimulation>
build_spawn_discontinuity(
    const PlayerCapsuleSimulation& simulation,
    const PlayerActionCommand& command,
    const PlayerMovementFrame& movement_frame,
    const terrain::HeightTileSurface& terrain_surface,
    const std::uint64_t fixed_tick)
{
    if (simulation.current.reset_generation ==
        std::numeric_limits<std::uint64_t>::max()) {
        return core::Result<PlayerCapsuleSimulation>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player-capsule reset generation would overflow"));
    }
    auto spawn_result = classify_spawn(
        simulation.config,
        terrain_surface,
        core::ErrorCode::invalid_state);
    if (!spawn_result) {
        return core::Result<PlayerCapsuleSimulation>::failure(
            spawn_result.error());
    }

    const auto generation =
        simulation.current.reset_generation + 1U;
    auto spawn = spawn_state(simulation.config);
    spawn.center_position.y =
        spawn_result.value().center_position_y;
    auto candidate = simulation;
    candidate.previous = PlayerCapsuleSnapshot{
        .state = spawn,
        .vertical = spawn_result.value().vertical,
        .water = {},
        .horizontal_velocity = {},
        .consumed_movement_frame = {},
        .fixed_tick = fixed_tick - 1U,
        .reset_generation = generation,
    };
    candidate.current = PlayerCapsuleSnapshot{
        .state = spawn,
        .vertical = spawn_result.value().vertical,
        .water = {},
        .horizontal_velocity = {},
        .consumed_command = command,
        .consumed_movement_frame = movement_frame,
        .fixed_tick = fixed_tick,
        .reset_generation = generation,
    };
    return core::Result<PlayerCapsuleSimulation>::success(candidate);
}

[[nodiscard]] core::Result<void> validate_source_support(
    const PlayerCapsuleSimulation& simulation,
    const std::optional<PlayerTerrainSupport>& support)
{
    const auto& state = simulation.current.state;
    const auto& vertical = simulation.current.vertical;
    if (!support) {
        if (vertical.phase != PlayerGroundPhase::rising &&
            vertical.phase != PlayerGroundPhase::falling &&
            vertical.phase !=
                PlayerGroundPhase::surface_swimming) {
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

    if (vertical.phase == PlayerGroundPhase::rising ||
        vertical.phase == PlayerGroundPhase::falling ||
        vertical.phase ==
            PlayerGroundPhase::surface_swimming) {
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

[[nodiscard]] core::Result<void> validate_source_water(
    const water::GameplayWaterQuery& gameplay_water,
    const std::optional<PlayerTerrainSupport>& support)
{
    if (!support.has_value()) {
        if (gameplay_water.disposition ==
            water::GameplayWaterDisposition::out_of_terrain) {
            return core::Result<void>::success();
        }
        return core::Result<void>::failure(character_error(
            core::ErrorCode::invalid_state,
            "Player tick-start water query reports terrain where "
            "canonical terrain is absent"));
    }

    if (gameplay_water.disposition ==
            water::GameplayWaterDisposition::out_of_terrain ||
        gameplay_water.bed_height !=
            support->surface.position.y) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::invalid_state,
            "Player tick-start water query disagrees with canonical "
            "terrain support"));
    }
    return core::Result<void>::success();
}

struct GroundTraversal final {
    math::Float3 center_position{};
    PlayerTerrainSupport support;
    math::Float3 horizontal_velocity{};
    bool blocked{};
};

[[nodiscard]] core::Result<GroundTraversal>
traverse_walkable_ground(
    const PlayerCapsuleSimulation& simulation,
    const PlayerTerrainSupport& source_support,
    const math::Float3 requested_velocity,
    const terrain::HeightTileSurface& terrain_surface,
    const float fixed_delta_seconds)
{
    const auto displacement_x =
        static_cast<double>(requested_velocity.x) *
        static_cast<double>(fixed_delta_seconds);
    const auto displacement_z =
        static_cast<double>(requested_velocity.z) *
        static_cast<double>(fixed_delta_seconds);
    const auto displacement_length = std::sqrt(
        displacement_x * displacement_x +
        displacement_z * displacement_z);
    if (!std::isfinite(displacement_length) ||
        !representable_float(displacement_x) ||
        !representable_float(displacement_z)) {
        return core::Result<GroundTraversal>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player ground traversal exceeded finite range"));
    }

    GroundTraversal traversal{
        .center_position =
            simulation.current.state.center_position,
        .support = source_support,
        .horizontal_velocity = requested_velocity,
    };
    if (displacement_length == 0.0) {
        return core::Result<GroundTraversal>::success(traversal);
    }

    const auto probe_count_value = std::ceil(
        displacement_length /
        static_cast<double>(
            simulation.config.ground_locomotion
                .maximum_probe_spacing));
    if (!std::isfinite(probe_count_value) ||
        probe_count_value < 1.0 ||
        probe_count_value >
            static_cast<double>(
                std::numeric_limits<std::uint32_t>::max())) {
        return core::Result<GroundTraversal>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player ground traversal probe count exceeded its "
                "bounded range"));
    }
    const auto probe_count =
        static_cast<std::uint32_t>(probe_count_value);
    const auto source_x = static_cast<double>(
        simulation.current.state.center_position.x);
    const auto source_z = static_cast<double>(
        simulation.current.state.center_position.z);

    for (std::uint32_t probe_index = 1U;
         probe_index <= probe_count;
         ++probe_index) {
        const auto fraction =
            static_cast<double>(probe_index) /
            static_cast<double>(probe_count);
        const auto probe_x =
            source_x + displacement_x * fraction;
        const auto probe_z =
            source_z + displacement_z * fraction;
        if (!representable_float(probe_x) ||
            !representable_float(probe_z)) {
            return core::Result<GroundTraversal>::failure(
                character_error(
                    core::ErrorCode::unavailable,
                    "Player terrain probe exceeded finite world "
                    "coordinates"));
        }
        const auto canonical_x =
            canonical_zero(static_cast<float>(probe_x));
        const auto canonical_z =
            canonical_zero(static_cast<float>(probe_z));
        auto support_result = query_player_terrain_support(
            simulation.config.shape,
            simulation.config.grounding,
            terrain_surface,
            canonical_x,
            canonical_z);
        if (!support_result) {
            return core::Result<GroundTraversal>::failure(
                support_result.error());
        }
        if (!support_result.value() ||
            !support_result.value()->walkable) {
            traversal.blocked = true;
            break;
        }

        const math::Float3 probe_position{
            canonical_x,
            support_result.value()->center_position_y,
            canonical_z,
        };
        if (!contains(
                simulation.config.center_bounds,
                probe_position)) {
            traversal.blocked = true;
            break;
        }
        traversal.center_position = probe_position;
        traversal.support = *support_result.value();
    }

    if (traversal.blocked) {
        traversal.horizontal_velocity = {};
    }
    return core::Result<GroundTraversal>::success(traversal);
}

[[nodiscard]] HorizontalIntent scale_intent_for_surface_swimming(
    HorizontalIntent intent,
    const PlayerSurfaceSwimmingSettings& settings) noexcept
{
    if (intent.active) {
        intent.target_speed = settings.speed;
    }
    return intent;
}

[[nodiscard]] core::Result<float>
surface_swimming_baseline(
    const PlayerCapsuleSimulation& simulation,
    const water::GameplayWaterQuery& gameplay_water,
    const PlayerTerrainSupport& source_support)
{
    const auto baseline = std::max(
        static_cast<double>(gameplay_water.surface_height) -
            static_cast<double>(
                simulation.config.surface_swimming
                    .surface_center_depth),
        static_cast<double>(
            source_support.center_position_y));
    if (!representable_float(baseline)) {
        return core::Result<float>::failure(character_error(
            core::ErrorCode::unavailable,
            "Player surface-swimming baseline exceeded finite "
            "range"));
    }
    return core::Result<float>::success(
        canonical_zero(static_cast<float>(baseline)));
}

struct SurfaceSwimmingTraversal final {
    math::Float3 center_position{};
    math::Float3 horizontal_velocity{};
    bool blocked{};
};

[[nodiscard]] core::Result<SurfaceSwimmingTraversal>
traverse_surface_swimming(
    const PlayerCapsuleSimulation& simulation,
    const PlayerTerrainSupport& source_support,
    const water::GameplayWaterQuery& gameplay_water,
    const math::Float3 requested_velocity,
    const terrain::HeightTileSurface& terrain_surface,
    const float traversal_seconds)
{
    auto baseline_result = surface_swimming_baseline(
        simulation,
        gameplay_water,
        source_support);
    if (!baseline_result) {
        return core::Result<
            SurfaceSwimmingTraversal>::failure(
                baseline_result.error());
    }
    const auto baseline = baseline_result.value();
    SurfaceSwimmingTraversal traversal{
        .center_position = {
            simulation.current.state.center_position.x,
            baseline,
            simulation.current.state.center_position.z,
        },
        .horizontal_velocity = requested_velocity,
    };
    if (!contains(
            simulation.config.center_bounds,
            traversal.center_position)) {
        return core::Result<
            SurfaceSwimmingTraversal>::failure(
                character_error(
                    core::ErrorCode::unavailable,
                    "Player surface-swimming source lies outside "
                    "its center bounds"));
    }

    const auto displacement_x =
        static_cast<double>(requested_velocity.x) *
        static_cast<double>(traversal_seconds);
    const auto displacement_z =
        static_cast<double>(requested_velocity.z) *
        static_cast<double>(traversal_seconds);
    const auto displacement_length = std::sqrt(
        displacement_x * displacement_x +
        displacement_z * displacement_z);
    if (!std::isfinite(displacement_length) ||
        !representable_float(displacement_x) ||
        !representable_float(displacement_z)) {
        return core::Result<
            SurfaceSwimmingTraversal>::failure(
                character_error(
                    core::ErrorCode::unavailable,
                    "Player surface-swimming traversal exceeded "
                    "finite range"));
    }
    if (displacement_length == 0.0) {
        return core::Result<
            SurfaceSwimmingTraversal>::success(traversal);
    }

    const auto probe_count_value = std::ceil(
        displacement_length /
        static_cast<double>(
            simulation.config.ground_locomotion
                .maximum_probe_spacing));
    if (!std::isfinite(probe_count_value) ||
        probe_count_value < 1.0 ||
        probe_count_value >
            static_cast<double>(
                maximum_airborne_probe_count)) {
        return core::Result<
            SurfaceSwimmingTraversal>::failure(
                character_error(
                    core::ErrorCode::unavailable,
                    "Player surface-swimming probe count exceeded "
                    "its bounded range"));
    }
    const auto probe_count =
        static_cast<std::uint32_t>(probe_count_value);
    const auto source_x = static_cast<double>(
        simulation.current.state.center_position.x);
    const auto source_z = static_cast<double>(
        simulation.current.state.center_position.z);

    for (std::uint32_t probe_index = 1U;
         probe_index <= probe_count;
         ++probe_index) {
        const auto fraction =
            static_cast<double>(probe_index) /
            static_cast<double>(probe_count);
        const auto probe_x =
            source_x + displacement_x * fraction;
        const auto probe_z =
            source_z + displacement_z * fraction;
        if (!representable_float(probe_x) ||
            !representable_float(probe_z)) {
            return core::Result<
                SurfaceSwimmingTraversal>::failure(
                    character_error(
                        core::ErrorCode::unavailable,
                        "Player surface-swimming probe exceeded "
                        "finite world coordinates"));
        }
        const auto canonical_x =
            canonical_zero(static_cast<float>(probe_x));
        const auto canonical_z =
            canonical_zero(static_cast<float>(probe_z));
        if (!contains_horizontal(
                simulation.config.center_bounds,
                canonical_x,
                canonical_z)) {
            traversal.blocked = true;
            break;
        }

        auto support_result = query_player_terrain_support(
            simulation.config.shape,
            simulation.config.grounding,
            terrain_surface,
            canonical_x,
            canonical_z);
        if (!support_result) {
            return core::Result<
                SurfaceSwimmingTraversal>::failure(
                    support_result.error());
        }
        if (!support_result.value()) {
            traversal.blocked = true;
            break;
        }

        const auto& support = *support_result.value();
        if (!support.walkable &&
            static_cast<double>(support.center_position_y) >
                static_cast<double>(baseline) +
                    terrain_contact_tolerance) {
            traversal.blocked = true;
            break;
        }
        const math::Float3 probe_position{
            canonical_x,
            canonical_zero(
                std::max(
                    baseline,
                    support.walkable
                        ? support.center_position_y
                        : baseline)),
            canonical_z,
        };
        if (!contains(
                simulation.config.center_bounds,
                probe_position)) {
            traversal.blocked = true;
            break;
        }
        traversal.center_position = probe_position;
    }

    if (traversal.blocked) {
        traversal.horizontal_velocity = {};
    }
    return core::Result<
        SurfaceSwimmingTraversal>::success(traversal);
}

struct AirTraversal final {
    math::Float3 center_position{};
    PlayerVerticalState vertical;
    math::Float3 horizontal_velocity{};
    bool requires_recovery{};
};

// This is a deterministic sampled center trajectory. It deliberately does
// not claim continuous swept-capsule collision against terrain.
[[nodiscard]] core::Result<AirTraversal>
traverse_airborne(
    const PlayerCapsuleSimulation& simulation,
    const math::Float3 requested_horizontal_velocity,
    const float starting_velocity_y,
    const terrain::HeightTileSurface& terrain_surface,
    const float fixed_delta_seconds)
{
    const auto next_velocity_y =
        static_cast<double>(starting_velocity_y) -
        static_cast<double>(
            simulation.config.grounding.gravity_magnitude) *
            static_cast<double>(fixed_delta_seconds);
    const auto displacement_x =
        static_cast<double>(requested_horizontal_velocity.x) *
        static_cast<double>(fixed_delta_seconds);
    const auto displacement_y =
        next_velocity_y *
        static_cast<double>(fixed_delta_seconds);
    const auto displacement_z =
        static_cast<double>(requested_horizontal_velocity.z) *
        static_cast<double>(fixed_delta_seconds);
    const auto displacement_length = std::sqrt(
        displacement_x * displacement_x +
        displacement_y * displacement_y +
        displacement_z * displacement_z);
    const auto target_y =
        static_cast<double>(
            simulation.current.state.center_position.y) +
        displacement_y;
    if (!representable_float(next_velocity_y) ||
        !representable_float(displacement_x) ||
        !representable_float(displacement_y) ||
        !representable_float(displacement_z) ||
        !std::isfinite(displacement_length) ||
        !representable_float(target_y)) {
        return core::Result<AirTraversal>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player airborne integration exceeded finite "
                "range"));
    }
    if (target_y >
        static_cast<double>(
            simulation.config.center_bounds.maximum.y)) {
        return core::Result<AirTraversal>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player airborne integration exceeded its maximum "
                "center bound"));
    }

    const auto source =
        simulation.current.state.center_position;
    const auto minimum_y = static_cast<double>(
        simulation.config.center_bounds.minimum.y);
    const auto crosses_minimum_y = target_y < minimum_y;
    auto traversal_fraction = 1.0;
    if (crosses_minimum_y) {
        const auto vertical_distance =
            static_cast<double>(source.y) - target_y;
        if (!std::isfinite(vertical_distance) ||
            vertical_distance <= 0.0) {
            return core::Result<AirTraversal>::failure(
                character_error(
                    core::ErrorCode::unavailable,
                    "Player airborne lower-bound traversal "
                    "exceeded finite range"));
        }
        traversal_fraction = std::clamp(
            (static_cast<double>(source.y) - minimum_y) /
                vertical_distance,
            0.0,
            1.0);
    }
    const auto traversal_displacement_x =
        displacement_x * traversal_fraction;
    const auto traversal_displacement_y =
        displacement_y * traversal_fraction;
    const auto traversal_displacement_z =
        displacement_z * traversal_fraction;
    const auto traversal_displacement_length = std::sqrt(
        traversal_displacement_x * traversal_displacement_x +
        traversal_displacement_y * traversal_displacement_y +
        traversal_displacement_z * traversal_displacement_z);
    const auto probe_count_value = std::max(
        1.0,
        std::ceil(
            traversal_displacement_length /
            static_cast<double>(
                simulation.config.ground_locomotion
                    .maximum_probe_spacing)));
    if (!std::isfinite(probe_count_value) ||
        probe_count_value >
            static_cast<double>(
                maximum_airborne_probe_count)) {
        return core::Result<AirTraversal>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player airborne probe count exceeded its bounded "
                "range"));
    }
    const auto probe_count =
        static_cast<std::uint32_t>(probe_count_value);
    auto safe_x = source.x;
    auto safe_z = source.z;
    auto horizontal_blocked = false;
    const auto descending = next_velocity_y <= 0.0;
    auto published_horizontal_velocity =
        requested_horizontal_velocity;

    for (std::uint32_t probe_index = 0U;
         probe_index < probe_count;
         ++probe_index) {
        const auto fraction = traversal_fraction *
            static_cast<double>(probe_index + 1U) /
            static_cast<double>(probe_count);
        const auto proposed_x =
            static_cast<double>(source.x) +
            displacement_x * fraction;
        const auto proposed_y =
            static_cast<double>(source.y) +
            displacement_y * fraction;
        const auto proposed_z =
            static_cast<double>(source.z) +
            displacement_z * fraction;
        if (!representable_float(proposed_x) ||
            !representable_float(proposed_y) ||
            !representable_float(proposed_z)) {
            return core::Result<AirTraversal>::failure(
                character_error(
                    core::ErrorCode::unavailable,
                    "Player airborne probe exceeded finite world "
                    "coordinates"));
        }

        auto probe_x = safe_x;
        auto probe_z = safe_z;
        if (!horizontal_blocked) {
            const auto candidate_x =
                canonical_zero(static_cast<float>(proposed_x));
            const auto candidate_z =
                canonical_zero(static_cast<float>(proposed_z));
            if (contains_horizontal(
                    simulation.config.center_bounds,
                    candidate_x,
                    candidate_z)) {
                probe_x = candidate_x;
                probe_z = candidate_z;
            }
            else {
                horizontal_blocked = true;
                published_horizontal_velocity = {};
            }
        }

        auto support_result = query_player_terrain_support(
            simulation.config.shape,
            simulation.config.grounding,
            terrain_surface,
            probe_x,
            probe_z);
        if (!support_result) {
            return core::Result<AirTraversal>::failure(
                support_result.error());
        }
        auto support = support_result.value();

        if (support) {
            const auto& terrain_support = *support;
            const auto intrudes =
                static_cast<double>(
                    terrain_support.center_position_y) >
                proposed_y + terrain_contact_tolerance;
            const auto rises_above_source =
                static_cast<double>(
                    terrain_support.center_position_y) >
                static_cast<double>(source.y) +
                    static_cast<double>(
                        simulation.config.grounding.snap_distance);
            if (intrudes &&
                (!descending || rises_above_source) &&
                !horizontal_blocked &&
                (probe_x != safe_x || probe_z != safe_z)) {
                horizontal_blocked = true;
                published_horizontal_velocity = {};
                probe_x = safe_x;
                probe_z = safe_z;
                auto safe_support_result =
                    query_player_terrain_support(
                    simulation.config.shape,
                    simulation.config.grounding,
                    terrain_surface,
                    probe_x,
                    probe_z);
                if (!safe_support_result) {
                    return core::Result<AirTraversal>::failure(
                        safe_support_result.error());
                }
                support = safe_support_result.value();
            }
        }

        if (descending && support &&
            proposed_y <=
                static_cast<double>(
                    support->center_position_y) +
                    static_cast<double>(
                        simulation.config.grounding.snap_distance)) {
            const auto& terrain_support = *support;
            const math::Float3 contact_position{
                probe_x,
                terrain_support.center_position_y,
                probe_z,
            };
            if (!contains(
                    simulation.config.center_bounds,
                    contact_position)) {
                if (contact_position.y <
                    simulation.config.center_bounds.minimum.y) {
                    return core::Result<AirTraversal>::success({
                        .requires_recovery = true,
                    });
                }
                return core::Result<AirTraversal>::failure(
                    character_error(
                        core::ErrorCode::unavailable,
                        "Player airborne contact lies outside its "
                        "center bounds"));
            }
            return core::Result<AirTraversal>::success({
                .center_position = contact_position,
                .vertical = supported_vertical_state(
                    terrain_support,
                    true),
                .horizontal_velocity = terrain_support.walkable
                    ? published_horizontal_velocity
                    : math::Float3{},
            });
        }

        if (!horizontal_blocked) {
            safe_x = probe_x;
            safe_z = probe_z;
        }
    }

    if (crosses_minimum_y) {
        return core::Result<AirTraversal>::success({
            .requires_recovery = true,
        });
    }
    const math::Float3 target_position{
        safe_x,
        canonical_zero(static_cast<float>(target_y)),
        safe_z,
    };
    if (!contains(
            simulation.config.center_bounds,
            target_position)) {
        return core::Result<AirTraversal>::failure(
            character_error(
                core::ErrorCode::unavailable,
                "Player airborne target lies outside its center "
                "bounds"));
    }
    return core::Result<AirTraversal>::success({
        .center_position = target_position,
        .vertical = airborne_vertical_state(
            static_cast<float>(next_velocity_y)),
        .horizontal_velocity =
            published_horizontal_velocity,
    });
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

bool is_valid(
    const PlayerGroundLocomotionSettings& settings) noexcept
{
    return std::isfinite(settings.walk_speed) &&
        settings.walk_speed > 0.0F &&
        settings.walk_speed <= maximum_player_horizontal_speed &&
        std::isfinite(settings.run_speed) &&
        settings.run_speed >= settings.walk_speed &&
        settings.run_speed <= maximum_player_horizontal_speed &&
        std::isfinite(settings.acceleration) &&
        settings.acceleration > 0.0F &&
        settings.acceleration <=
            maximum_player_ground_acceleration &&
        std::isfinite(settings.braking_deceleration) &&
        settings.braking_deceleration > 0.0F &&
        settings.braking_deceleration <=
            maximum_player_ground_acceleration &&
        std::isfinite(
            settings.facing_turn_speed_radians_per_second) &&
        settings.facing_turn_speed_radians_per_second > 0.0F &&
        settings.facing_turn_speed_radians_per_second <=
            maximum_player_facing_turn_speed_radians_per_second &&
        std::isfinite(settings.maximum_probe_spacing) &&
        settings.maximum_probe_spacing >=
            minimum_player_probe_spacing &&
        settings.maximum_probe_spacing <=
            maximum_player_probe_spacing;
}

bool is_valid(
    const PlayerAirLocomotionSettings& settings) noexcept
{
    return std::isfinite(settings.jump_launch_speed) &&
        settings.jump_launch_speed > 0.0F &&
        settings.jump_launch_speed <=
            maximum_player_jump_launch_speed &&
        std::isfinite(settings.control_acceleration) &&
        settings.control_acceleration > 0.0F &&
        settings.control_acceleration <=
            maximum_player_air_control_acceleration;
}

bool is_valid(const PlayerWadingSettings& settings) noexcept
{
    return std::isfinite(settings.enter_depth) &&
        settings.enter_depth > 0.0F &&
        settings.enter_depth <= maximum_player_wading_depth &&
        std::isfinite(settings.exit_depth) &&
        settings.exit_depth >= 0.0F &&
        settings.exit_depth < settings.enter_depth &&
        std::isfinite(settings.depth_for_minimum_speed) &&
        settings.depth_for_minimum_speed >
            settings.enter_depth &&
        settings.depth_for_minimum_speed <=
            maximum_player_wading_depth &&
        std::isfinite(settings.minimum_speed_multiplier) &&
        settings.minimum_speed_multiplier > 0.0F &&
        settings.minimum_speed_multiplier <= 1.0F;
}

bool is_valid(
    const PlayerSurfaceSwimmingSettings& settings) noexcept
{
    return std::isfinite(settings.enter_depth) &&
        settings.enter_depth > 0.0F &&
        settings.enter_depth <=
            maximum_player_surface_swimming_depth &&
        std::isfinite(settings.exit_depth) &&
        settings.exit_depth >= 0.0F &&
        settings.exit_depth < settings.enter_depth &&
        std::isfinite(settings.surface_center_depth) &&
        settings.surface_center_depth >= 0.0F &&
        settings.surface_center_depth <=
            maximum_player_surface_swimming_depth &&
        std::isfinite(settings.speed) &&
        settings.speed > 0.0F &&
        settings.speed <= maximum_player_horizontal_speed;
}

bool is_valid(const PlayerMovementFrame& frame) noexcept
{
    if (!math::is_finite(frame.right) ||
        !math::is_finite(frame.forward) ||
        frame.right.y != 0.0F ||
        frame.forward.y != 0.0F) {
        return false;
    }
    const auto right_length =
        horizontal_length_squared(frame.right);
    const auto forward_length =
        horizontal_length_squared(frame.forward);
    const auto orthogonality =
        horizontal_dot(frame.right, frame.forward);
    const auto determinant =
        static_cast<double>(frame.right.x) *
            frame.forward.z -
        static_cast<double>(frame.right.z) *
            frame.forward.x;
    constexpr double basis_tolerance = 0.00001;
    return std::abs(right_length - 1.0) <= basis_tolerance &&
        std::abs(forward_length - 1.0) <= basis_tolerance &&
        std::abs(orthogonality) <= basis_tolerance &&
        std::abs(determinant + 1.0) <= basis_tolerance;
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
        !valid_water_state(
            simulation.previous.water,
            simulation.previous.vertical,
            simulation.previous.state,
            simulation.config.wading,
            simulation.config.surface_swimming) ||
        !valid_water_state(
            simulation.current.water,
            simulation.current.vertical,
            simulation.current.state,
            simulation.config.wading,
            simulation.config.surface_swimming) ||
        !valid_horizontal_velocity(
            simulation.previous.horizontal_velocity,
            simulation.previous.vertical.phase,
            simulation.config.ground_locomotion) ||
        !valid_horizontal_velocity(
            simulation.current.horizontal_velocity,
            simulation.current.vertical.phase,
            simulation.config.ground_locomotion) ||
        !is_valid(simulation.previous.consumed_command) ||
        !is_valid(simulation.current.consumed_command) ||
        !is_valid(
            simulation.previous.consumed_movement_frame) ||
        !is_valid(
            simulation.current.consumed_movement_frame) ||
        !canonical_movement_frame(
            simulation.previous.consumed_movement_frame) ||
        !canonical_movement_frame(
            simulation.current.consumed_movement_frame) ||
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
            simulation.previous.water ==
                simulation.current.water &&
            simulation.current.water ==
                PlayerWaterState{} &&
            canonical_zero_vector(
                simulation.previous.horizontal_velocity) &&
            canonical_zero_vector(
                simulation.current.horizontal_velocity) &&
            neutral_command(
                simulation.previous.consumed_command) &&
            neutral_command(
                simulation.current.consumed_command) &&
            neutral_movement_frame(
                simulation.previous.consumed_movement_frame) &&
            neutral_movement_frame(
                simulation.current.consumed_movement_frame);
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
        !is_valid(config.grounding) ||
        !is_valid(config.ground_locomotion) ||
        !is_valid(config.air_locomotion) ||
        !is_valid(config.wading) ||
        !is_valid(config.surface_swimming)) {
        return core::Result<PlayerCapsuleSimulation>::failure(
            character_error(
                core::ErrorCode::invalid_argument,
                "Player capsule requires finite bounded shape, "
                "center bounds, spawn pose, grounding, ground "
                "locomotion, air locomotion, wading, and surface-"
                "swimming settings"));
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
    config.wading.exit_depth =
        canonical_zero(config.wading.exit_depth);
    config.surface_swimming.exit_depth =
        canonical_zero(
            config.surface_swimming.exit_depth);
    config.surface_swimming.surface_center_depth =
        canonical_zero(
            config.surface_swimming.surface_center_depth);
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
    PlayerMovementFrame movement_frame,
    const terrain::HeightTileSurface& terrain_surface,
    const water::GameplayWaterQuery& gameplay_water,
    const float fixed_delta_seconds,
    const std::uint64_t fixed_tick)
{
    if (!is_valid(simulation) ||
        !is_valid(command) ||
        !is_valid(movement_frame) ||
        !water::is_valid(gameplay_water) ||
        !std::isfinite(fixed_delta_seconds) ||
        fixed_delta_seconds <= 0.0F ||
        fixed_delta_seconds >
            maximum_player_fixed_delta_seconds) {
        return core::Result<void>::failure(character_error(
            core::ErrorCode::invalid_argument,
            "Player-capsule advance requires valid state, command, "
            "movement frame, gameplay-water query, and fixed delta "
            "in (0, 0.25] seconds"));
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
    auto water_source_result = validate_source_water(
        gameplay_water,
        support_result.value());
    if (!water_source_result) {
        return core::Result<void>::failure(
            water_source_result.error());
    }

    command.look_yaw_delta_radians =
        canonical_zero(command.look_yaw_delta_radians);
    command.look_pitch_delta_radians =
        canonical_zero(command.look_pitch_delta_radians);
    movement_frame.right =
        canonical_zero(movement_frame.right);
    movement_frame.forward =
        canonical_zero(movement_frame.forward);

    auto candidate = simulation;
    if (command.reset_pressed) {
        auto spawn_result = build_spawn_discontinuity(
            simulation,
            command,
            movement_frame,
            terrain_surface,
            fixed_tick);
        if (!spawn_result) {
            return core::Result<void>::failure(
                spawn_result.error());
        }
        candidate = spawn_result.value();
    }
    else {
        candidate.previous = simulation.current;
        candidate.current = simulation.current;
        candidate.current.consumed_command = command;
        candidate.current.consumed_movement_frame =
            movement_frame;
        candidate.current.fixed_tick = fixed_tick;

        const auto currently_surface_swimming =
            simulation.current.vertical.phase ==
                PlayerGroundPhase::surface_swimming;
        if (currently_surface_swimming) {
            if (!support_result.value()) {
                auto recovery_result =
                    build_spawn_discontinuity(
                        simulation,
                        command,
                        movement_frame,
                        terrain_surface,
                        fixed_tick);
                if (!recovery_result) {
                    return core::Result<void>::failure(
                        recovery_result.error());
                }
                candidate = recovery_result.value();
            }
            else if (
                gameplay_water.disposition ==
                    water::GameplayWaterDisposition::water &&
                gameplay_water.depth >
                    simulation.config.surface_swimming
                        .exit_depth) {
                auto intent_result = build_horizontal_intent(
                    command,
                    movement_frame,
                    simulation.config.ground_locomotion);
                if (!intent_result) {
                    return core::Result<void>::failure(
                        intent_result.error());
                }
                const auto swim_intent =
                    scale_intent_for_surface_swimming(
                        intent_result.value(),
                        simulation.config.surface_swimming);
                auto facing_result =
                    turn_facing_toward_intent(
                        simulation.current.state
                            .facing_yaw_radians,
                        swim_intent,
                        simulation.config.ground_locomotion,
                        fixed_delta_seconds);
                if (!facing_result) {
                    return core::Result<void>::failure(
                        facing_result.error());
                }
                auto velocity_result =
                    move_horizontal_velocity_toward(
                        simulation.current.horizontal_velocity,
                        swim_intent,
                        simulation.config.ground_locomotion,
                        fixed_delta_seconds);
                if (!velocity_result) {
                    return core::Result<void>::failure(
                        velocity_result.error());
                }
                auto traversal_result =
                    traverse_surface_swimming(
                        simulation,
                        *support_result.value(),
                        gameplay_water,
                        velocity_result.value(),
                        terrain_surface,
                        fixed_delta_seconds);
                if (!traversal_result) {
                    return core::Result<void>::failure(
                        traversal_result.error());
                }
                candidate.current.state.center_position =
                    traversal_result.value().center_position;
                candidate.current.state.facing_yaw_radians =
                    facing_result.value();
                candidate.current.vertical =
                    surface_swimming_vertical_state();
                candidate.current.water = {
                    .phase =
                        PlayerWaterPhase::surface_swimming,
                    .depth = gameplay_water.depth,
                    .surface_height =
                        gameplay_water.surface_height,
                };
                candidate.current.horizontal_velocity =
                    traversal_result.value().horizontal_velocity;
            }
            else {
                const auto& support = *support_result.value();
                const auto water_exit =
                    gameplay_water.disposition ==
                    water::GameplayWaterDisposition::water;
                const auto separation =
                    static_cast<double>(
                        simulation.current.state
                            .center_position.y) -
                    static_cast<double>(
                        support.center_position_y);
                const auto can_snap_no_water =
                    !water_exit &&
                    support.walkable &&
                    separation >= -terrain_contact_tolerance &&
                    separation <=
                        static_cast<double>(
                            simulation.config.grounding
                                .snap_distance) +
                            terrain_contact_tolerance;
                if (water_exit || can_snap_no_water) {
                    candidate.current.state.center_position.y =
                        support.center_position_y;
                    candidate.current.vertical =
                        supported_vertical_state(
                            support,
                            false);
                    candidate.current.water =
                        water_exit &&
                            support.walkable &&
                            gameplay_water.depth >
                                simulation.config.wading
                                    .exit_depth
                        ? PlayerWaterState{
                              .phase =
                                  PlayerWaterPhase::wading,
                              .depth = gameplay_water.depth,
                          }
                        : PlayerWaterState{};
                    candidate.current.horizontal_velocity =
                        support.walkable
                        ? simulation.current
                              .horizontal_velocity
                        : math::Float3{};
                }
                else {
                    candidate.current.vertical =
                        falling_vertical_state();
                    candidate.current.water = {};
                    candidate.current.horizontal_velocity =
                        simulation.current
                            .horizontal_velocity;
                }
            }
        }
        else {
            const auto supported =
                simulation.current.vertical.phase ==
                    PlayerGroundPhase::grounded ||
                simulation.current.vertical.phase ==
                    PlayerGroundPhase::landing;
            const auto airborne =
                simulation.current.vertical.phase ==
                    PlayerGroundPhase::rising ||
                simulation.current.vertical.phase ==
                    PlayerGroundPhase::falling;
            const auto tick_start_water =
                classify_tick_start_water(
                    simulation.current.water,
                    simulation.current.vertical,
                    gameplay_water,
                    simulation.config.wading);
            if (supported || airborne) {
            auto intent_result = build_horizontal_intent(
                command,
                movement_frame,
                simulation.config.ground_locomotion);
            if (!intent_result) {
                return core::Result<void>::failure(
                    intent_result.error());
            }
            auto facing_result = turn_facing_toward_intent(
                simulation.current.state.facing_yaw_radians,
                intent_result.value(),
                simulation.config.ground_locomotion,
                fixed_delta_seconds);
            if (!facing_result) {
                return core::Result<void>::failure(
                    facing_result.error());
            }

            const auto enter_surface_swimming =
                supported &&
                !command.jump_pressed &&
                simulation.current.water.phase ==
                    PlayerWaterPhase::wading &&
                gameplay_water.disposition ==
                    water::GameplayWaterDisposition::water &&
                gameplay_water.depth >=
                    simulation.config.surface_swimming
                        .enter_depth;
            if (enter_surface_swimming) {
                const auto swim_intent =
                    scale_intent_for_surface_swimming(
                        intent_result.value(),
                        simulation.config.surface_swimming);
                auto swim_facing_result =
                    turn_facing_toward_intent(
                        simulation.current.state
                            .facing_yaw_radians,
                        swim_intent,
                        simulation.config.ground_locomotion,
                        fixed_delta_seconds);
                if (!swim_facing_result) {
                    return core::Result<void>::failure(
                        swim_facing_result.error());
                }
                auto velocity_result =
                    move_horizontal_velocity_toward(
                        simulation.current.horizontal_velocity,
                        swim_intent,
                        simulation.config.ground_locomotion,
                        fixed_delta_seconds);
                if (!velocity_result) {
                    return core::Result<void>::failure(
                        velocity_result.error());
                }
                auto traversal_result =
                    traverse_surface_swimming(
                        simulation,
                        *support_result.value(),
                        gameplay_water,
                        velocity_result.value(),
                        terrain_surface,
                        fixed_delta_seconds);
                if (!traversal_result) {
                    return core::Result<void>::failure(
                        traversal_result.error());
                }
                candidate.current.state.center_position =
                    traversal_result.value().center_position;
                candidate.current.state.facing_yaw_radians =
                    swim_facing_result.value();
                candidate.current.vertical =
                    surface_swimming_vertical_state();
                candidate.current.water = {
                    .phase =
                        PlayerWaterPhase::surface_swimming,
                    .depth = gameplay_water.depth,
                    .surface_height =
                        gameplay_water.surface_height,
                };
                candidate.current.horizontal_velocity =
                    traversal_result.value().horizontal_velocity;
            }
            else if (supported && !command.jump_pressed) {
                const auto& support = *support_result.value();
                const auto ground_intent =
                    scale_intent_for_wading(
                        intent_result.value(),
                        tick_start_water,
                        simulation.config.wading);
                auto velocity_result =
                    move_horizontal_velocity_toward(
                        simulation.current.horizontal_velocity,
                        ground_intent,
                        simulation.config.ground_locomotion,
                        fixed_delta_seconds);
                if (!velocity_result) {
                    return core::Result<void>::failure(
                        velocity_result.error());
                }
                auto traversal_result = traverse_walkable_ground(
                    simulation,
                    support,
                    velocity_result.value(),
                    terrain_surface,
                    fixed_delta_seconds);
                if (!traversal_result) {
                    return core::Result<void>::failure(
                        traversal_result.error());
                }
                candidate.current.state.center_position =
                    traversal_result.value().center_position;
                candidate.current.state.facing_yaw_radians =
                    facing_result.value();
                candidate.current.vertical =
                    supported_vertical_state(
                        traversal_result.value().support,
                        false);
                candidate.current.water = tick_start_water;
                candidate.current.horizontal_velocity =
                    traversal_result.value().horizontal_velocity;
            }
            else {
                candidate.current.water = {};
                auto velocity_result =
                    move_air_horizontal_velocity_toward(
                        simulation.current.horizontal_velocity,
                        intent_result.value(),
                        simulation.config.air_locomotion,
                        fixed_delta_seconds);
                if (!velocity_result) {
                    return core::Result<void>::failure(
                        velocity_result.error());
                }
                const auto starting_velocity_y = supported
                    ? simulation.config.air_locomotion
                        .jump_launch_speed
                    : simulation.current.vertical.velocity_y;
                auto captured_surface = false;
                if (!supported &&
                    simulation.current.vertical.phase ==
                        PlayerGroundPhase::falling &&
                    gameplay_water.disposition ==
                        water::GameplayWaterDisposition::water &&
                    gameplay_water.depth >=
                        simulation.config.surface_swimming
                            .enter_depth) {
                    auto baseline_result =
                        surface_swimming_baseline(
                            simulation,
                            gameplay_water,
                            *support_result.value());
                    if (!baseline_result) {
                        return core::Result<void>::failure(
                            baseline_result.error());
                    }
                    const auto baseline =
                        static_cast<double>(
                            baseline_result.value());
                    const auto source_y =
                        static_cast<double>(
                            simulation.current.state
                                .center_position.y);
                    const auto next_velocity_y =
                        static_cast<double>(
                            starting_velocity_y) -
                        static_cast<double>(
                            simulation.config.grounding
                                .gravity_magnitude) *
                            static_cast<double>(
                                fixed_delta_seconds);
                    const auto displacement_y =
                        next_velocity_y *
                        static_cast<double>(
                            fixed_delta_seconds);
                    const auto target_y =
                        source_y + displacement_y;
                    if (!representable_float(next_velocity_y) ||
                        !representable_float(displacement_y) ||
                        !representable_float(target_y)) {
                        return core::Result<void>::failure(
                            character_error(
                                core::ErrorCode::unavailable,
                                "Player surface capture exceeded "
                                "finite range"));
                    }

                    auto capture_fraction = 1.0;
                    if (source_y <= baseline) {
                        capture_fraction = 0.0;
                        captured_surface = true;
                    }
                    else if (next_velocity_y <= 0.0 &&
                             target_y <= baseline) {
                        const auto descent =
                            source_y - target_y;
                        if (!std::isfinite(descent) ||
                            descent <= 0.0) {
                            return core::Result<void>::failure(
                                character_error(
                                    core::ErrorCode::unavailable,
                                    "Player surface-capture "
                                    "trajectory is invalid"));
                        }
                        capture_fraction = std::clamp(
                            (source_y - baseline) / descent,
                            0.0,
                            1.0);
                        captured_surface = true;
                    }

                    if (captured_surface) {
                        const auto capture_seconds =
                            static_cast<double>(
                                fixed_delta_seconds) *
                            capture_fraction;
                        if (!representable_float(
                                capture_seconds)) {
                            return core::Result<void>::failure(
                                character_error(
                                    core::ErrorCode::unavailable,
                                    "Player surface-capture time "
                                    "exceeded finite range"));
                        }
                        auto swim_traversal_result =
                            traverse_surface_swimming(
                                simulation,
                                *support_result.value(),
                                gameplay_water,
                                velocity_result.value(),
                                terrain_surface,
                                canonical_zero(
                                    static_cast<float>(
                                        capture_seconds)));
                        if (!swim_traversal_result) {
                            return core::Result<void>::failure(
                                swim_traversal_result.error());
                        }
                        candidate.current.state.center_position =
                            swim_traversal_result.value()
                                .center_position;
                        candidate.current.state
                            .facing_yaw_radians =
                            facing_result.value();
                        candidate.current.vertical =
                            surface_swimming_vertical_state();
                        candidate.current.water = {
                            .phase = PlayerWaterPhase::
                                surface_swimming,
                            .depth = gameplay_water.depth,
                            .surface_height =
                                gameplay_water.surface_height,
                        };
                        candidate.current.horizontal_velocity =
                            swim_traversal_result.value()
                                .horizontal_velocity;
                    }
                }
                if (!captured_surface) {
                    auto traversal_result = traverse_airborne(
                        simulation,
                        velocity_result.value(),
                        starting_velocity_y,
                        terrain_surface,
                        fixed_delta_seconds);
                    if (!traversal_result) {
                        return core::Result<void>::failure(
                            traversal_result.error());
                    }
                    if (traversal_result.value()
                            .requires_recovery) {
                        auto recovery_result =
                            build_spawn_discontinuity(
                                simulation,
                                command,
                                movement_frame,
                                terrain_surface,
                                fixed_tick);
                        if (!recovery_result) {
                            return core::Result<void>::failure(
                                recovery_result.error());
                        }
                        candidate = recovery_result.value();
                    }
                    else {
                        candidate.current.state.center_position =
                            traversal_result.value()
                                .center_position;
                        candidate.current.state
                            .facing_yaw_radians =
                            facing_result.value();
                        candidate.current.vertical =
                            traversal_result.value().vertical;
                        candidate.current.horizontal_velocity =
                            traversal_result.value()
                                .horizontal_velocity;
                    }
                }
            }
            }
            else {
                const auto& support = *support_result.value();
                candidate.current.state.center_position.y =
                    support.center_position_y;
                candidate.current.vertical =
                    supported_vertical_state(
                        support,
                        false);
                candidate.current.water = {};
                candidate.current.horizontal_velocity = {};
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
    candidate.previous.water = candidate.current.water;
    candidate.previous.horizontal_velocity =
        candidate.current.horizontal_velocity;
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
