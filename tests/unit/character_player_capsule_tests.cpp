#include <shark/character/player_capsule.hpp>
#include <shark/simulation/fixed_step_clock.hpp>
#include <shark/terrain/height_tile.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float comparison_margin = 0.00001F;

[[nodiscard]] shark::terrain::HeightTile make_plane_tile(
    const float slope_x = 0.0F)
{
    std::vector<float> heights;
    heights.reserve(25U);
    for (std::uint32_t z = 0U; z < 5U; ++z) {
        static_cast<void>(z);
        for (std::uint32_t x = 0U; x < 5U; ++x) {
            heights.push_back(
                slope_x *
                (static_cast<float>(x) - 2.0F));
        }
    }
    return {
        .sample_columns = 5U,
        .sample_rows = 5U,
        .sample_spacing = 1.0F,
        .origin = {-2.0F, 0.0F, -2.0F},
        .height_offsets = std::move(heights),
    };
}

[[nodiscard]] shark::terrain::HeightTile make_twisted_tile()
{
    return {
        .sample_columns = 2U,
        .sample_rows = 2U,
        .sample_spacing = 1.0F,
        .origin = {},
        .height_offsets = {
            0.0F, 0.0F,
            0.0F, 1.0F,
        },
    };
}

[[nodiscard]] shark::terrain::HeightTile make_wide_plane_tile(
    const float slope_x = 0.0F)
{
    constexpr std::uint32_t sample_count = 17U;
    std::vector<float> heights;
    heights.reserve(
        static_cast<std::size_t>(sample_count) * sample_count);
    for (std::uint32_t z = 0U; z < sample_count; ++z) {
        static_cast<void>(z);
        for (std::uint32_t x = 0U; x < sample_count; ++x) {
            heights.push_back(
                slope_x *
                (static_cast<float>(x) - 8.0F));
        }
    }
    return {
        .sample_columns = sample_count,
        .sample_rows = sample_count,
        .sample_spacing = 1.0F,
        .origin = {-8.0F, 0.0F, -8.0F},
        .height_offsets = std::move(heights),
    };
}

[[nodiscard]] shark::terrain::HeightTile
make_steep_barrier_tile()
{
    constexpr std::uint32_t columns = 7U;
    constexpr std::uint32_t rows = 3U;
    std::vector<float> heights;
    heights.reserve(
        static_cast<std::size_t>(columns) * rows);
    for (std::uint32_t z = 0U; z < rows; ++z) {
        static_cast<void>(z);
        for (std::uint32_t x = 0U; x < columns; ++x) {
            heights.push_back(x <= 3U ? 0.0F : 2.0F);
        }
    }
    return {
        .sample_columns = columns,
        .sample_rows = rows,
        .sample_spacing = 1.0F,
        .origin = {-3.0F, 0.0F, -1.0F},
        .height_offsets = std::move(heights),
    };
}

[[nodiscard]] shark::terrain::HeightTileSurface make_surface(
    shark::terrain::HeightTile tile = make_plane_tile())
{
    auto result =
        shark::terrain::HeightTileSurface::create(std::move(tile));
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::character::PlayerCapsuleConfig test_config(
    const shark::math::Float3 spawn = {0.0F, 1.0F, 0.0F})
{
    return {
        .shape = {
            .radius = 0.5F,
            .vertical_half_segment = 0.5F,
        },
        .center_bounds = {
            .minimum = {-16.0F, -8.0F, -16.0F},
            .maximum = {16.0F, 24.0F, 16.0F},
        },
        .spawn_center_position = spawn,
        .spawn_facing_yaw_radians = 0.0F,
    };
}

[[nodiscard]] shark::character::PlayerCapsuleSimulation
make_simulation(
    const shark::terrain::HeightTileSurface& surface,
    shark::character::PlayerCapsuleConfig config = test_config())
{
    auto result =
        shark::character::create_player_capsule(
            config,
            surface);
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] bool positive_zero(const float value) noexcept
{
    return value == 0.0F && !std::signbit(value);
}

[[nodiscard]] shark::character::PlayerMovementFrame
movement_frame_for_yaw(const float yaw)
{
    const auto cosine = static_cast<float>(
        std::cos(static_cast<double>(yaw)));
    const auto sine = static_cast<float>(
        std::sin(static_cast<double>(yaw)));
    const auto canonical = [](const float value) {
        return value == 0.0F ? 0.0F : value;
    };
    return {
        .right = {
            canonical(cosine),
            0.0F,
            canonical(sine),
        },
        .forward = {
            canonical(sine),
            0.0F,
            canonical(-cosine),
        },
    };
}

void require_float3(
    const shark::math::Float3 actual,
    const shark::math::Float3 expected,
    const float margin = comparison_margin)
{
    REQUIRE(actual.x == Catch::Approx(expected.x).margin(margin));
    REQUIRE(actual.y == Catch::Approx(expected.y).margin(margin));
    REQUIRE(actual.z == Catch::Approx(expected.z).margin(margin));
}

[[nodiscard]] shark::character::PlayerActionCommand
scripted_command(const std::uint64_t fixed_tick)
{
    return {
        .move_forward_held =
            fixed_tick >= 5U && fixed_tick < 40U,
        .move_backward_held =
            fixed_tick >= 90U && fixed_tick < 105U,
        .move_left_held = fixed_tick % 7U == 0U,
        .move_right_held = fixed_tick % 11U == 0U,
        .run_held = fixed_tick % 3U == 0U,
        .jump_pressed = fixed_tick == 23U,
        .primary_action_pressed = fixed_tick == 61U,
        .reset_pressed = fixed_tick == 75U,
        .look_yaw_delta_radians =
            fixed_tick % 5U == 0U ? 0.125F : 0.0F,
        .look_pitch_delta_radians =
            fixed_tick % 13U == 0U ? -0.0625F : 0.0F,
    };
}

struct ScheduleRun final {
    shark::character::PlayerCapsuleSimulation simulation;
    std::vector<shark::character::PlayerCapsuleSnapshot> trace;

    [[nodiscard]] friend bool operator==(
        const ScheduleRun&,
        const ScheduleRun&) = default;
};

[[nodiscard]] ScheduleRun run_schedule(
    const std::uint32_t render_rate_hz)
{
    using namespace shark;

    const auto surface =
        make_surface(make_wide_plane_tile());
    auto player = make_simulation(
        surface);
    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();
    std::vector<character::PlayerCapsuleSnapshot> trace;
    trace.reserve(120U);

    auto previous_timestamp = std::chrono::nanoseconds{0};
    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) * 2U;
    for (std::uint64_t frame = 1U;
         frame <= frame_count;
         ++frame) {
        const auto timestamp = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                frame * 1'000'000'000ULL / render_rate_hz)};
        const auto frame_result =
            clock.advance(timestamp - previous_timestamp);
        REQUIRE(frame_result);
        previous_timestamp = timestamp;

        const auto first_tick =
            clock.total_step_count() -
            frame_result.value().step_count + 1U;
        for (std::uint32_t step = 0U;
             step < frame_result.value().step_count;
             ++step) {
            const auto fixed_tick = first_tick + step;
            REQUIRE(character::advance_player_capsule(
                player,
                scripted_command(fixed_tick),
                {},
                surface,
                clock.fixed_delta_seconds(),
                fixed_tick));
            trace.push_back(player.current);
        }
    }

    REQUIRE(clock.total_step_count() == 120U);
    REQUIRE(trace.size() == 120U);
    return {
        .simulation = player,
        .trace = std::move(trace),
    };
}

} // namespace

TEST_CASE(
    "player creation classifies and canonicalizes terrain spawn",
    "[character][player-capsule][create][grounding]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto config = test_config({0.0F, 1.02F, 0.0F});
    config.spawn_facing_yaw_radians = 5.0F * math::pi;
    const auto first =
        character::create_player_capsule(config, surface);
    const auto second =
        character::create_player_capsule(config, surface);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first.value() == second.value());
    const auto& player = first.value();

    REQUIRE(character::is_valid(player));
    REQUIRE(player.previous == player.current);
    REQUIRE(player.config.spawn_center_position.y == 1.0F);
    REQUIRE(player.current.state.center_position.y == 1.0F);
    REQUIRE(player.current.state.facing_yaw_radians ==
        Catch::Approx(-math::pi).margin(0.000001F));
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::grounded);
    REQUIRE(positive_zero(player.current.vertical.velocity_y));
    require_float3(
        player.current.vertical.support_normal,
        {0.0F, 1.0F, 0.0F});
    REQUIRE(player.current.fixed_tick == 0U);
    REQUIRE(player.current.reset_generation == 0U);
    REQUIRE(player.current.horizontal_velocity ==
        math::Float3{});
    REQUIRE(player.current.consumed_movement_frame ==
        character::PlayerMovementFrame{});

    const auto airborne = character::create_player_capsule(
        test_config({0.0F, 2.0F, 0.0F}),
        surface);
    REQUIRE(airborne);
    REQUIRE(airborne.value().current.vertical.phase ==
        character::PlayerGroundPhase::falling);
    REQUIRE(positive_zero(
        airborne.value().current.vertical.velocity_y));
    REQUIRE(airborne.value().current.vertical.support_normal ==
        math::Float3{});

    REQUIRE_FALSE(character::create_player_capsule(
        test_config({0.0F, 0.98F, 0.0F}),
        surface));

    auto outside = test_config({4.0F, 3.0F, 0.0F});
    const auto missed =
        character::create_player_capsule(outside, surface);
    REQUIRE(missed);
    REQUIRE(missed.value().current.vertical.phase ==
        character::PlayerGroundPhase::falling);
}

TEST_CASE(
    "player configuration rejects malformed grounding and shape",
    "[character][player-capsule][create][validation]")
{
    using namespace shark;

    const auto surface = make_surface();
    constexpr auto nan =
        std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity =
        std::numeric_limits<float>::infinity();

    auto require_invalid =
        [&surface](const character::PlayerCapsuleConfig config) {
            const auto result =
                character::create_player_capsule(
                    config,
                    surface);
            REQUIRE_FALSE(result);
            REQUIRE(result.error().code() ==
                core::ErrorCode::invalid_argument);
        };

    auto config = test_config();
    config.shape.radius = 0.0F;
    require_invalid(config);
    config = test_config();
    config.shape.vertical_half_segment =
        std::nextafter(
            character::
                maximum_player_capsule_vertical_half_segment,
            infinity);
    require_invalid(config);
    config = test_config();
    config.center_bounds.maximum.x =
        config.center_bounds.minimum.x - 1.0F;
    require_invalid(config);
    config = test_config();
    config.spawn_center_position.x =
        config.center_bounds.maximum.x + 1.0F;
    require_invalid(config);
    config = test_config();
    config.spawn_facing_yaw_radians = nan;
    require_invalid(config);

    const std::array invalid_gravity{
        0.0F,
        -1.0F,
        nan,
        infinity,
        std::nextafter(
            character::maximum_player_gravity_magnitude,
            infinity),
    };
    for (const auto value : invalid_gravity) {
        config = test_config();
        config.grounding.gravity_magnitude = value;
        require_invalid(config);
    }
    const std::array invalid_normal_y{
        0.0F,
        -0.1F,
        nan,
        std::nextafter(1.0F, infinity),
    };
    for (const auto value : invalid_normal_y) {
        config = test_config();
        config.grounding.minimum_walkable_normal_y =
            value;
        require_invalid(config);
    }
    const std::array invalid_snap{
        -0.001F,
        nan,
        infinity,
        std::nextafter(
            character::maximum_player_ground_snap_distance,
            infinity),
    };
    for (const auto value : invalid_snap) {
        config = test_config();
        config.grounding.snap_distance = value;
        require_invalid(config);
    }

    const std::array invalid_speed{
        0.0F,
        -1.0F,
        nan,
        infinity,
        std::nextafter(
            character::maximum_player_horizontal_speed,
            infinity),
    };
    for (const auto value : invalid_speed) {
        config = test_config();
        config.ground_locomotion.walk_speed = value;
        require_invalid(config);
        config = test_config();
        config.ground_locomotion.run_speed = value;
        require_invalid(config);
    }
    config = test_config();
    config.ground_locomotion.run_speed =
        config.ground_locomotion.walk_speed - 0.01F;
    require_invalid(config);

    const std::array invalid_acceleration{
        0.0F,
        -1.0F,
        nan,
        infinity,
        std::nextafter(
            character::maximum_player_ground_acceleration,
            infinity),
    };
    for (const auto value : invalid_acceleration) {
        config = test_config();
        config.ground_locomotion.acceleration = value;
        require_invalid(config);
        config = test_config();
        config.ground_locomotion.braking_deceleration =
            value;
        require_invalid(config);
    }
    config = test_config();
    config.ground_locomotion
        .facing_turn_speed_radians_per_second = 0.0F;
    require_invalid(config);
    config = test_config();
    config.ground_locomotion.maximum_probe_spacing =
        std::nextafter(
            character::minimum_player_probe_spacing,
            0.0F);
    require_invalid(config);
    config = test_config();
    config.ground_locomotion.maximum_probe_spacing =
        std::nextafter(
            character::maximum_player_probe_spacing,
            infinity);
    require_invalid(config);

    REQUIRE(character::PlayerAirLocomotionSettings{} ==
        character::PlayerAirLocomotionSettings{
            .jump_launch_speed = 6.5F,
            .control_acceleration = 12.0F,
        });
    const std::array invalid_jump_speed{
        0.0F,
        -1.0F,
        nan,
        infinity,
        std::nextafter(
            character::maximum_player_jump_launch_speed,
            infinity),
    };
    for (const auto value : invalid_jump_speed) {
        config = test_config();
        config.air_locomotion.jump_launch_speed = value;
        require_invalid(config);
    }
    const std::array invalid_air_acceleration{
        0.0F,
        -1.0F,
        nan,
        infinity,
        std::nextafter(
            character::maximum_player_air_control_acceleration,
            infinity),
    };
    for (const auto value : invalid_air_acceleration) {
        config = test_config();
        config.air_locomotion.control_acceleration = value;
        require_invalid(config);
    }
    REQUIRE(character::is_valid(
        character::PlayerAirLocomotionSettings{
            .jump_launch_speed =
                character::maximum_player_jump_launch_speed,
            .control_acceleration =
                character::
                    maximum_player_air_control_acceleration,
        }));
}

TEST_CASE(
    "canonical terrain support preserves slope and edge ownership",
    "[character][player-capsule][terrain-support][slope][edge]")
{
    using namespace shark;

    const character::PlayerCapsuleShape shape;
    const character::PlayerGroundingSettings settings;

    SECTION("flat support")
    {
        const auto surface = make_surface();
        const auto support =
            character::query_player_terrain_support(
                shape,
                settings,
                surface,
                0.0F,
                0.0F);
        REQUIRE(support);
        REQUIRE(support.value());
        REQUIRE(support.value()->walkable);
        REQUIRE(support.value()->center_position_y == 1.0F);
        const auto expected =
            surface.sample_lod0_surface(0.0F, 0.0F);
        REQUIRE(expected);
        REQUIRE(support.value()->surface == *expected);
    }

    SECTION("threshold is inclusive")
    {
        const auto surface =
            make_surface(make_plane_tile(0.5F));
        const auto sample =
            surface.sample_lod0_surface(0.0F, 0.0F);
        REQUIRE(sample);
        auto threshold = settings;
        threshold.minimum_walkable_normal_y =
            sample->normal.y;
        const auto inclusive =
            character::query_player_terrain_support(
                shape,
                threshold,
                surface,
                0.0F,
                0.0F);
        REQUIRE(inclusive);
        REQUIRE(inclusive.value());
        REQUIRE(inclusive.value()->walkable);

        threshold.minimum_walkable_normal_y =
            std::nextafter(sample->normal.y, 1.0F);
        const auto unwalkable =
            character::query_player_terrain_support(
                shape,
                threshold,
                surface,
                0.0F,
                0.0F);
        REQUIRE(unwalkable);
        REQUIRE(unwalkable.value());
        REQUIRE_FALSE(unwalkable.value()->walkable);
        REQUIRE(
            unwalkable.value()->center_position_y ==
            Catch::Approx(
                sample->position.y +
                shape.vertical_half_segment +
                shape.radius / sample->normal.y)
                .margin(0.000001F));
    }

    SECTION("steep support")
    {
        const auto surface =
            make_surface(make_plane_tile(2.0F));
        const auto support =
            character::query_player_terrain_support(
                shape,
                settings,
                surface,
                0.0F,
                0.0F);
        REQUIRE(support);
        REQUIRE(support.value());
        REQUIRE_FALSE(support.value()->walkable);
    }

    SECTION("diagonal and maximum edge")
    {
        const auto surface =
            make_surface(make_twisted_tile());
        constexpr std::array points{
            math::Float3{0.5F, 0.0F, 0.5F},
            math::Float3{1.0F, 0.0F, 1.0F},
        };
        for (const auto point : points) {
            const auto sample =
                surface.sample_lod0_surface(
                    point.x,
                    point.z);
            const auto support =
                character::query_player_terrain_support(
                    shape,
                    settings,
                    surface,
                    point.x,
                    point.z);
            REQUIRE(sample);
            REQUIRE(support);
            REQUIRE(support.value());
            REQUIRE(support.value()->surface == sample);
        }
        const auto miss =
            character::query_player_terrain_support(
                shape,
                settings,
                surface,
                std::nextafter(
                    1.0F,
                    std::numeric_limits<float>::infinity()),
                1.0F);
        REQUIRE(miss);
        REQUIRE_FALSE(miss.value());
    }

    REQUIRE_FALSE(character::query_player_terrain_support(
        shape,
        settings,
        make_surface(),
        std::numeric_limits<float>::quiet_NaN(),
        0.0F));
}

TEST_CASE(
    "player movement frames and locomotion settings are bounded",
    "[character][player-capsule][locomotion][validation]")
{
    using namespace shark;

    REQUIRE(character::is_valid(
        character::PlayerGroundLocomotionSettings{}));
    REQUIRE(character::is_valid(
        character::PlayerMovementFrame{}));
    for (const auto yaw :
         std::array{0.0F, math::half_pi, -math::pi, 1.25F}) {
        REQUIRE(character::is_valid(
            movement_frame_for_yaw(yaw)));
    }

    auto frame = character::PlayerMovementFrame{};
    frame.right.y = 0.01F;
    REQUIRE_FALSE(character::is_valid(frame));
    frame = {};
    frame.forward = frame.right;
    REQUIRE_FALSE(character::is_valid(frame));
    frame = {};
    frame.forward.z = 1.0F;
    REQUIRE_FALSE(character::is_valid(frame));
    frame = {};
    frame.right.x = 0.5F;
    REQUIRE_FALSE(character::is_valid(frame));
    frame = {};
    frame.forward.x =
        std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(character::is_valid(frame));

    frame = {};
    frame.right.y = -0.0F;
    frame.right.z = -0.0F;
    frame.forward.x = -0.0F;
    frame.forward.y = -0.0F;
    REQUIRE(character::is_valid(frame));
    const auto surface = make_surface();
    auto player = make_simulation(surface);
    REQUIRE(character::advance_player_capsule(
        player,
        {},
        frame,
        surface,
        1.0F / 60.0F,
        1U));
    REQUIRE_FALSE(std::signbit(
        player.current.consumed_movement_frame.right.y));
    REQUIRE_FALSE(std::signbit(
        player.current.consumed_movement_frame.right.z));
    REQUIRE_FALSE(std::signbit(
        player.current.consumed_movement_frame.forward.x));
    REQUIRE_FALSE(std::signbit(
        player.current.consumed_movement_frame.forward.y));
}

TEST_CASE(
    "ground locomotion is camera relative and diagonal normalized",
    "[character][player-capsule][locomotion][camera-relative]")
{
    using namespace shark;

    const auto surface = make_surface(make_wide_plane_tile());

    auto forward = make_simulation(surface);
    REQUIRE(character::advance_player_capsule(
        forward,
        {.move_forward_held = true},
        {},
        surface,
        0.25F,
        1U));
    require_float3(
        forward.current.horizontal_velocity,
        {0.0F, 0.0F, -4.0F});
    require_float3(
        forward.current.state.center_position,
        {0.0F, 1.0F, -1.0F});
    REQUIRE(positive_zero(
        forward.current.state.facing_yaw_radians));

    auto rotated = make_simulation(surface);
    const character::PlayerMovementFrame quarter_turn{
        .right = {0.0F, 0.0F, 1.0F},
        .forward = {1.0F, 0.0F, 0.0F},
    };
    REQUIRE(character::advance_player_capsule(
        rotated,
        {.move_forward_held = true},
        quarter_turn,
        surface,
        0.25F,
        1U));
    require_float3(
        rotated.current.horizontal_velocity,
        {4.0F, 0.0F, 0.0F});
    require_float3(
        rotated.current.state.center_position,
        {1.0F, 1.0F, 0.0F});
    REQUIRE(
        rotated.current.state.facing_yaw_radians ==
        Catch::Approx(math::half_pi).margin(0.000001F));
    REQUIRE(
        rotated.current.consumed_movement_frame ==
        quarter_turn);

    auto diagonal = make_simulation(surface);
    REQUIRE(character::advance_player_capsule(
        diagonal,
        {
            .move_forward_held = true,
            .move_right_held = true,
        },
        {},
        surface,
        0.25F,
        1U));
    const auto diagonal_speed = std::sqrt(
        static_cast<double>(
            diagonal.current.horizontal_velocity.x) *
            diagonal.current.horizontal_velocity.x +
        static_cast<double>(
            diagonal.current.horizontal_velocity.z) *
            diagonal.current.horizontal_velocity.z);
    REQUIRE(diagonal_speed ==
        Catch::Approx(4.0).margin(0.000001));
    REQUIRE(
        diagonal.current.horizontal_velocity.x ==
        Catch::Approx(
            -diagonal.current.horizontal_velocity.z)
            .margin(0.000001F));
    const auto diagonal_distance = std::sqrt(
        static_cast<double>(
            diagonal.current.state.center_position.x) *
            diagonal.current.state.center_position.x +
        static_cast<double>(
            diagonal.current.state.center_position.z) *
            diagonal.current.state.center_position.z);
    REQUIRE(diagonal_distance ==
        Catch::Approx(1.0).margin(0.000001));

    auto cancelled = make_simulation(surface);
    REQUIRE(character::advance_player_capsule(
        cancelled,
        {
            .move_forward_held = true,
            .move_backward_held = true,
            .move_left_held = true,
            .move_right_held = true,
        },
        quarter_turn,
        surface,
        0.25F,
        1U));
    REQUIRE(cancelled.current.state.center_position ==
        math::Float3{0.0F, 1.0F, 0.0F});
    REQUIRE(cancelled.current.horizontal_velocity ==
        math::Float3{});
}

TEST_CASE(
    "ground velocity accelerates brakes and reverses deterministically",
    "[character][player-capsule][locomotion][acceleration][braking]")
{
    using namespace shark;

    const auto surface = make_surface(make_wide_plane_tile());
    auto player = make_simulation(surface);
    const character::PlayerActionCommand run_right{
        .move_right_held = true,
        .run_held = true,
    };
    REQUIRE(character::advance_player_capsule(
        player,
        run_right,
        {},
        surface,
        0.25F,
        1U));
    REQUIRE(player.current.horizontal_velocity.x == 6.0F);
    REQUIRE(player.current.state.center_position.x == 1.5F);
    REQUIRE(character::advance_player_capsule(
        player,
        run_right,
        {},
        surface,
        0.25F,
        2U));
    REQUIRE(player.current.horizontal_velocity.x == 7.0F);
    REQUIRE(player.current.state.center_position.x == 3.25F);

    REQUIRE(character::advance_player_capsule(
        player,
        {},
        {},
        surface,
        0.125F,
        3U));
    REQUIRE(player.current.horizontal_velocity.x == 3.0F);
    REQUIRE(player.current.state.center_position.x == 3.625F);
    REQUIRE(character::advance_player_capsule(
        player,
        {},
        {},
        surface,
        0.125F,
        4U));
    REQUIRE(player.current.horizontal_velocity ==
        math::Float3{});
    REQUIRE(player.current.state.center_position.x == 3.625F);

    auto reversing = make_simulation(surface);
    REQUIRE(character::advance_player_capsule(
        reversing,
        run_right,
        {},
        surface,
        0.25F,
        1U));
    REQUIRE(character::advance_player_capsule(
        reversing,
        run_right,
        {},
        surface,
        0.25F,
        2U));
    REQUIRE(character::advance_player_capsule(
        reversing,
        {
            .move_left_held = true,
            .run_held = true,
        },
        {},
        surface,
        0.25F,
        3U));
    REQUIRE(reversing.current.horizontal_velocity.x == -1.0F);
    REQUIRE(reversing.current.state.center_position.x == 3.0F);
    REQUIRE(
        reversing.current.state.facing_yaw_radians ==
        Catch::Approx(math::half_pi - 2.5F)
            .margin(0.000001F));
}

TEST_CASE(
    "ground traversal accepts a safe prefix and rejects steep terrain",
    "[character][player-capsule][locomotion][terrain][slope]")
{
    using namespace shark;

    const auto barrier =
        make_surface(make_steep_barrier_tile());
    auto player = make_simulation(
        barrier,
        test_config({-0.5F, 1.0F, 0.0F}));
    REQUIRE(character::advance_player_capsule(
        player,
        {
            .move_right_held = true,
            .run_held = true,
        },
        {},
        barrier,
        0.25F,
        1U));
    REQUIRE(player.current.state.center_position.x == -0.25F);
    REQUIRE(player.current.state.center_position.y == 1.0F);
    REQUIRE(player.current.horizontal_velocity ==
        math::Float3{});
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::grounded);
    REQUIRE(
        player.current.state.facing_yaw_radians ==
        Catch::Approx(math::half_pi).margin(0.000001F));

    const auto blocked = player;
    REQUIRE(character::advance_player_capsule(
        player,
        {
            .move_right_held = true,
            .run_held = true,
        },
        {},
        barrier,
        0.25F,
        2U));
    REQUIRE(player.current.state.center_position ==
        blocked.current.state.center_position);
    REQUIRE(player.current.horizontal_velocity ==
        math::Float3{});

    const auto gentle =
        make_surface(make_wide_plane_tile(0.25F));
    const auto support =
        character::query_player_terrain_support(
            {},
            {},
            gentle,
            0.0F,
            0.0F);
    REQUIRE(support);
    REQUIRE(support.value());
    auto slope = make_simulation(
        gentle,
        test_config({
            0.0F,
            support.value()->center_position_y,
            0.0F,
        }));
    REQUIRE(character::advance_player_capsule(
        slope,
        {.move_right_held = true},
        {},
        gentle,
        0.25F,
        1U));
    const auto destination_support =
        character::query_player_terrain_support(
            slope.config.shape,
            slope.config.grounding,
            gentle,
            1.0F,
            0.0F);
    REQUIRE(destination_support);
    REQUIRE(destination_support.value());
    REQUIRE(slope.current.state.center_position ==
        math::Float3{
            1.0F,
            destination_support.value()->center_position_y,
            0.0F,
        });
    REQUIRE(slope.current.vertical.support_normal ==
        destination_support.value()->surface.normal);

    auto bounded_config = test_config();
    bounded_config.center_bounds.maximum.x = 0.6F;
    const auto bounded_surface =
        make_surface(make_wide_plane_tile());
    auto bounded = make_simulation(
        bounded_surface,
        bounded_config);
    REQUIRE(character::advance_player_capsule(
        bounded,
        {.move_right_held = true},
        {},
        bounded_surface,
        0.25F,
        1U));
    REQUIRE(bounded.current.state.center_position.x == 0.5F);
    REQUIRE(bounded.current.horizontal_velocity ==
        math::Float3{});
}

TEST_CASE(
    "ground facing follows the bounded shortest yaw arc",
    "[character][player-capsule][locomotion][facing]")
{
    using namespace shark;

    const auto surface = make_surface(make_wide_plane_tile());
    auto config = test_config();
    config.spawn_facing_yaw_radians = math::pi - 0.5F;
    auto player = make_simulation(surface, config);
    const auto initial_yaw =
        player.current.state.facing_yaw_radians;
    const auto desired_yaw = -math::pi + 0.5F;
    REQUIRE(character::advance_player_capsule(
        player,
        {.move_forward_held = true},
        movement_frame_for_yaw(desired_yaw),
        surface,
        0.01F,
        1U));
    const auto applied = std::remainder(
        static_cast<double>(
            player.current.state.facing_yaw_radians) -
            initial_yaw,
        static_cast<double>(math::two_pi));
    REQUIRE(applied ==
        Catch::Approx(0.1).margin(0.000001));
    const auto remaining = std::remainder(
        static_cast<double>(desired_yaw) -
            player.current.state.facing_yaw_radians,
        static_cast<double>(math::two_pi));
    REQUIRE(remaining > 0.0);
    REQUIRE(remaining ==
        Catch::Approx(0.9).margin(0.000002));
}

TEST_CASE(
    "falling accepts air control while steep contact remains stable",
    "[character][player-capsule][locomotion][airborne][steep]")
{
    using namespace shark;

    const auto flat = make_surface(make_wide_plane_tile());
    auto falling = make_simulation(
        flat,
        test_config({0.0F, 3.0F, 0.0F}));
    REQUIRE(character::advance_player_capsule(
        falling,
        {
            .move_forward_held = true,
            .move_right_held = true,
            .run_held = true,
        },
        movement_frame_for_yaw(math::half_pi),
        flat,
        0.1F,
        1U));
    const auto controlled_component =
        static_cast<float>(1.2 / std::sqrt(2.0));
    REQUIRE(falling.current.state.center_position.x ==
        Catch::Approx(controlled_component * 0.1F)
            .margin(0.000001F));
    REQUIRE(falling.current.state.center_position.z ==
        Catch::Approx(controlled_component * 0.1F)
            .margin(0.000001F));
    REQUIRE(falling.current.state.facing_yaw_radians ==
        Catch::Approx(1.0F).margin(0.000001F));
    require_float3(
        falling.current.horizontal_velocity,
        {
            controlled_component,
            0.0F,
            controlled_component,
        });

    const auto steep_surface =
        make_surface(make_wide_plane_tile(2.0F));
    const auto support =
        character::query_player_terrain_support(
            {},
            {},
            steep_surface,
            0.0F,
            0.0F);
    REQUIRE(support);
    REQUIRE(support.value());
    REQUIRE_FALSE(support.value()->walkable);
    auto steep = make_simulation(
        steep_surface,
        test_config({
            0.0F,
            support.value()->center_position_y,
            0.0F,
        }));
    REQUIRE(steep.current.vertical.phase ==
        character::PlayerGroundPhase::steep_contact);
    const auto before = steep.current.state;
    REQUIRE(character::advance_player_capsule(
        steep,
        {
            .move_forward_held = true,
            .run_held = true,
        },
        movement_frame_for_yaw(math::half_pi),
        steep_surface,
        0.25F,
        1U));
    REQUIRE(steep.current.state == before);
    REQUIRE(steep.current.horizontal_velocity ==
        math::Float3{});
}

TEST_CASE(
    "grounded player remains exactly stable under gravity",
    "[character][player-capsule][grounded][stability]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto player = make_simulation(surface);
    for (std::uint64_t tick = 1U; tick <= 600U; ++tick) {
        const character::PlayerActionCommand command{
            .primary_action_pressed = tick % 2U == 0U,
        };
        REQUIRE(character::advance_player_capsule(
            player,
            command,
            {},
            surface,
            1.0F / 60.0F,
            tick));
        REQUIRE(player.current.state.center_position ==
            math::Float3{0.0F, 1.0F, 0.0F});
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::grounded);
        REQUIRE(positive_zero(
            player.current.vertical.velocity_y));
        REQUIRE(player.current.vertical.support_normal ==
            math::Float3{0.0F, 1.0F, 0.0F});
        REQUIRE(player.current.consumed_command == command);
    }
    REQUIRE(character::is_valid(player));
}

TEST_CASE(
    "grounded jump reaches one apex lands once and ignores air pulses",
    "[character][player-capsule][jump][apex][landing]")
{
    using namespace shark;

    const auto surface = make_surface(make_wide_plane_tile());
    auto player = make_simulation(surface);
    constexpr auto delta_seconds = 1.0F / 60.0F;
    auto expected_y = 1.0;
    auto expected_velocity =
        static_cast<double>(
            character::default_player_jump_launch_speed);
    std::uint64_t first_falling_tick = 0U;
    std::uint64_t landing_tick = 0U;
    std::uint32_t landing_count = 0U;
    auto highest_y = player.current.state.center_position.y;

    for (std::uint64_t tick = 1U; tick <= 120U; ++tick) {
        expected_velocity -=
            static_cast<double>(
                character::default_player_gravity_magnitude) *
            delta_seconds;
        expected_y += expected_velocity * delta_seconds;
        const character::PlayerActionCommand command{
            .jump_pressed = tick == 1U || tick == 5U,
        };
        REQUIRE(character::advance_player_capsule(
            player,
            command,
            {},
            surface,
            delta_seconds,
            tick));

        highest_y = std::max(
            highest_y,
            player.current.state.center_position.y);
        if (player.current.vertical.phase ==
            character::PlayerGroundPhase::landing) {
            ++landing_count;
            landing_tick = tick;
            REQUIRE(player.previous.vertical.phase ==
                character::PlayerGroundPhase::falling);
            REQUIRE(player.current.state.center_position ==
                math::Float3{0.0F, 1.0F, 0.0F});
            REQUIRE(positive_zero(
                player.current.vertical.velocity_y));
            REQUIRE(player.current.horizontal_velocity ==
                math::Float3{});
            break;
        }

        const auto expected_phase = expected_velocity > 0.0
            ? character::PlayerGroundPhase::rising
            : character::PlayerGroundPhase::falling;
        REQUIRE(player.current.vertical.phase == expected_phase);
        if (expected_phase ==
                character::PlayerGroundPhase::falling &&
            first_falling_tick == 0U) {
            first_falling_tick = tick;
            REQUIRE(player.previous.vertical.phase ==
                character::PlayerGroundPhase::rising);
        }
        REQUIRE(player.current.vertical.velocity_y ==
            Catch::Approx(
                static_cast<float>(expected_velocity))
                .margin(0.00001F));
        REQUIRE(player.current.state.center_position.y ==
            Catch::Approx(static_cast<float>(expected_y))
                .margin(0.00001F));
        REQUIRE(player.current.vertical.support_normal ==
            math::Float3{});
    }

    REQUIRE(first_falling_tick == 40U);
    REQUIRE(landing_tick == 79U);
    REQUIRE(landing_count == 1U);
    REQUIRE(highest_y ==
        Catch::Approx(3.0995F).margin(0.00002F));
    REQUIRE(character::advance_player_capsule(
        player,
        {},
        {},
        surface,
        delta_seconds,
        landing_tick + 1U));
    REQUIRE(player.previous.vertical.phase ==
        character::PlayerGroundPhase::landing);
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::grounded);
}

TEST_CASE(
    "air control is camera relative capped and preserves neutral momentum",
    "[character][player-capsule][jump][air-control][facing]")
{
    using namespace shark;

    const auto surface = make_surface(make_wide_plane_tile());
    auto player = make_simulation(
        surface,
        test_config({0.0F, 20.0F, 0.0F}));
    const auto frame = movement_frame_for_yaw(math::half_pi);
    constexpr auto delta_seconds = 0.05F;
    const character::PlayerActionCommand controlled{
        .move_forward_held = true,
        .run_held = true,
    };
    for (std::uint64_t tick = 1U; tick <= 12U; ++tick) {
        REQUIRE(character::advance_player_capsule(
            player,
            controlled,
            frame,
            surface,
            delta_seconds,
            tick));
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::falling);
    }
    require_float3(
        player.current.horizontal_velocity,
        {7.0F, 0.0F, 0.0F});
    REQUIRE(player.current.state.facing_yaw_radians ==
        Catch::Approx(math::half_pi).margin(0.000001F));

    const auto before_neutral = player.current;
    REQUIRE(character::advance_player_capsule(
        player,
        {.jump_pressed = true},
        {},
        surface,
        delta_seconds,
        13U));
    REQUIRE(player.current.horizontal_velocity ==
        before_neutral.horizontal_velocity);
    REQUIRE(player.current.state.center_position.x ==
        Catch::Approx(
            before_neutral.state.center_position.x +
                7.0F * delta_seconds)
            .margin(0.000001F));
    REQUIRE(player.current.vertical.velocity_y <
        before_neutral.vertical.velocity_y);
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::falling);
}

TEST_CASE(
    "air trajectory blocks rising terrain and horizontal bounds",
    "[character][player-capsule][jump][terrain][bounds]")
{
    using namespace shark;

    SECTION("rising terrain intrusion")
    {
        const auto surface =
            make_surface(make_steep_barrier_tile());
        auto player = make_simulation(
            surface,
            test_config({-0.5F, 1.0F, 0.0F}));
        REQUIRE(character::advance_player_capsule(
            player,
            {
                .move_right_held = true,
                .run_held = true,
                .jump_pressed = true,
            },
            {},
            surface,
            0.25F,
            1U));
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::rising);
        REQUIRE(player.current.state.center_position.x >= -0.5F);
        REQUIRE(player.current.state.center_position.x <= 0.0F);
        REQUIRE(player.current.state.center_position.y ==
            Catch::Approx(2.011875F).margin(0.00001F));
        REQUIRE(player.current.horizontal_velocity ==
            math::Float3{});
    }

    SECTION("inclusive horizontal center bound")
    {
        const auto surface =
            make_surface(make_wide_plane_tile());
        auto config = test_config();
        config.center_bounds.maximum.x = 0.3F;
        auto player = make_simulation(surface, config);
        REQUIRE(character::advance_player_capsule(
            player,
            {
                .move_right_held = true,
                .run_held = true,
                .jump_pressed = true,
            },
            {},
            surface,
            0.25F,
            1U));
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::rising);
        REQUIRE(player.current.state.center_position.x > 0.0F);
        REQUIRE(player.current.state.center_position.x <= 0.3F);
        REQUIRE(player.current.horizontal_velocity ==
            math::Float3{});
        REQUIRE(player.current.state.center_position.y ==
            Catch::Approx(2.011875F).margin(0.00001F));
    }
}

TEST_CASE(
    "descending sampled path lands on an intermediate raised band",
    "[character][player-capsule][airborne][terrain][landing]")
{
    using namespace shark;

    constexpr std::uint32_t columns = 17U;
    constexpr std::uint32_t rows = 5U;
    std::vector<float> heights;
    heights.reserve(
        static_cast<std::size_t>(columns) * rows);
    for (std::uint32_t z = 0U; z < rows; ++z) {
        static_cast<void>(z);
        for (std::uint32_t x = 0U; x < columns; ++x) {
            heights.push_back(
                x >= 7U && x <= 10U ? 1.0F : 0.0F);
        }
    }
    const auto surface = make_surface({
        .sample_columns = columns,
        .sample_rows = rows,
        .sample_spacing = 0.25F,
        .origin = {-2.0F, 0.0F, -0.5F},
        .height_offsets = std::move(heights),
    });
    auto player = make_simulation(
        surface,
        test_config({-1.0F, 1.0F, 0.0F}));
    constexpr auto delta_seconds = 0.25F;
    REQUIRE(character::advance_player_capsule(
        player,
        {},
        {},
        surface,
        1.0F / 60.0F,
        1U));
    player.current.state.center_position.y = 5.0F;
    player.current.vertical = {
        .velocity_y = -13.1475F,
        .phase = character::PlayerGroundPhase::falling,
    };
    player.current.horizontal_velocity = {7.0F, 0.0F, 0.0F};
    REQUIRE(character::is_valid(player));

    const auto endpoint_x =
        player.current.state.center_position.x +
        player.current.horizontal_velocity.x * delta_seconds;
    const auto endpoint_y =
        player.current.state.center_position.y +
        (player.current.vertical.velocity_y -
            character::default_player_gravity_magnitude *
                delta_seconds) *
            delta_seconds;
    const auto endpoint_support =
        character::query_player_terrain_support(
            player.config.shape,
            player.config.grounding,
            surface,
            endpoint_x,
            0.0F);
    REQUIRE(endpoint_support);
    REQUIRE(endpoint_support.value());
    REQUIRE(endpoint_support.value()->center_position_y == 1.0F);
    REQUIRE(endpoint_y >
        endpoint_support.value()->center_position_y +
            player.config.grounding.snap_distance);

    REQUIRE(character::advance_player_capsule(
        player,
        {},
        {},
        surface,
        delta_seconds,
        2U));
    REQUIRE(player.previous.vertical.phase ==
        character::PlayerGroundPhase::falling);
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::landing);
    REQUIRE(player.current.state.center_position.x >= -0.25F);
    REQUIRE(player.current.state.center_position.x <= 0.5F);
    REQUIRE(player.current.state.center_position.x < endpoint_x);
    REQUIRE(player.current.state.center_position.y == 2.0F);
    require_float3(
        player.current.horizontal_velocity,
        {7.0F, 0.0F, 0.0F});
    REQUIRE(player.current.vertical.support_normal ==
        math::Float3{0.0F, 1.0F, 0.0F});
}

TEST_CASE(
    "missing terrain fall recovers atomically to authored spawn",
    "[character][player-capsule][jump][recovery][terrain-edge]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto config = test_config({1.75F, 1.0F, 0.0F});
    config.center_bounds.minimum.y = 0.0F;
    auto player = make_simulation(surface, config);
    REQUIRE(character::advance_player_capsule(
        player,
        {
            .move_right_held = true,
            .run_held = true,
            .jump_pressed = true,
        },
        {},
        surface,
        0.25F,
        1U));
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::rising);
    REQUIRE(player.current.state.center_position.x > 2.0F);
    REQUIRE(player.current.horizontal_velocity.x == 3.0F);

    std::uint64_t recovery_tick = 0U;
    for (std::uint64_t tick = 2U; tick <= 40U; ++tick) {
        const auto result = character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            0.1F,
            tick);
        const auto error_message = result
            ? std::string{}
            : std::string{result.error().message()};
        CAPTURE(tick, error_message);
        REQUIRE(result);
        if (player.current.reset_generation == 1U) {
            recovery_tick = tick;
            break;
        }
    }
    REQUIRE(recovery_tick > 1U);
    REQUIRE_FALSE(
        player.current.consumed_command.reset_pressed);
    REQUIRE(player.previous.state == player.current.state);
    REQUIRE(player.previous.vertical == player.current.vertical);
    REQUIRE(player.previous.horizontal_velocity ==
        math::Float3{});
    REQUIRE(player.current.horizontal_velocity ==
        math::Float3{});
    REQUIRE(player.current.state.center_position ==
        player.config.spawn_center_position);
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::grounded);
    REQUIRE(player.previous.fixed_tick == recovery_tick - 1U);
    REQUIRE(player.current.fixed_tick == recovery_tick);

    REQUIRE(character::advance_player_capsule(
        player,
        {.jump_pressed = true},
        {},
        surface,
        1.0F / 60.0F,
        recovery_tick + 1U));
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::rising);
    REQUIRE(player.current.reset_generation == 1U);
}

TEST_CASE(
    "airborne traversal bounds probe work before publication",
    "[character][player-capsule][airborne][bounds][rollback]")
{
    using namespace shark;

    const auto surface = make_surface(make_wide_plane_tile());

    SECTION("deep fall lands before crossing the recovery bound")
    {
        auto player = make_simulation(surface);
        REQUIRE(character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            1.0F / 60.0F,
            1U));
        player.current.state.center_position.y = 2.0F;
        player.current.vertical = {
            .velocity_y = -1.0e20F,
            .phase = character::PlayerGroundPhase::falling,
        };
        REQUIRE(character::is_valid(player));

        REQUIRE(character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            0.25F,
            2U));
        REQUIRE(player.current.reset_generation == 0U);
        REQUIRE(player.previous.state.center_position.y == 2.0F);
        REQUIRE(player.current.state.center_position ==
            math::Float3{0.0F, 1.0F, 0.0F});
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::landing);
    }

    SECTION("deep unsupported fall recovers after bounded probing")
    {
        auto player = make_simulation(surface);
        REQUIRE(character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            1.0F / 60.0F,
            1U));
        player.current.state.center_position =
            {12.0F, 2.0F, 0.0F};
        player.current.vertical = {
            .velocity_y = -1.0e20F,
            .phase = character::PlayerGroundPhase::falling,
        };
        REQUIRE(character::is_valid(player));

        REQUIRE(character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            0.25F,
            2U));
        REQUIRE(player.current.reset_generation == 1U);
        REQUIRE(player.previous.state == player.current.state);
        REQUIRE(player.current.state.center_position ==
            player.config.spawn_center_position);
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::grounded);
    }

    SECTION("within-bounds path exceeding probe budget rolls back")
    {
        auto config = test_config();
        config.center_bounds.minimum.y = -100.0F;
        config.center_bounds.maximum.y = 100.0F;
        config.ground_locomotion.maximum_probe_spacing = 0.01F;
        auto player = make_simulation(surface, config);
        REQUIRE(character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            1.0F / 60.0F,
            1U));
        player.current.state.center_position.y = 80.0F;
        player.current.vertical = {
            .velocity_y = -200.0F,
            .phase = character::PlayerGroundPhase::falling,
        };
        REQUIRE(character::is_valid(player));
        const auto before = player;

        const auto result = character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            0.25F,
            2U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unavailable);
        REQUIRE(player == before);
    }
}

TEST_CASE(
    "falling player integrates lands once and becomes grounded",
    "[character][player-capsule][falling][landing]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto player = make_simulation(
        surface,
        test_config({0.0F, 3.0F, 0.0F}));
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::falling);

    constexpr auto delta_seconds = 0.1F;
    auto expected_y = 3.0;
    auto expected_velocity = 0.0;
    std::uint64_t landing_tick = 0U;
    for (std::uint64_t tick = 1U; tick <= 20U; ++tick) {
        expected_velocity -= 9.81 * delta_seconds;
        expected_y += expected_velocity * delta_seconds;
        REQUIRE(character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            delta_seconds,
            tick));
        if (player.current.vertical.phase ==
            character::PlayerGroundPhase::landing) {
            landing_tick = tick;
            REQUIRE(player.previous.vertical.phase ==
                character::PlayerGroundPhase::falling);
            REQUIRE(player.current.state.center_position.y ==
                1.0F);
            REQUIRE(positive_zero(
                player.current.vertical.velocity_y));
            break;
        }
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::falling);
        REQUIRE(
            player.current.vertical.velocity_y ==
            Catch::Approx(
                static_cast<float>(expected_velocity))
                .margin(0.00001F));
        REQUIRE(
            player.current.state.center_position.y ==
            Catch::Approx(
                static_cast<float>(expected_y))
                .margin(0.00001F));
    }
    REQUIRE(landing_tick > 0U);

    REQUIRE(character::advance_player_capsule(
        player,
        {.move_forward_held = true},
        {},
        surface,
        delta_seconds,
        landing_tick + 1U));
    REQUIRE(player.previous.vertical.phase ==
        character::PlayerGroundPhase::landing);
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::grounded);
    REQUIRE(player.current.state.center_position.y == 1.0F);
    REQUIRE(
        player.current.horizontal_velocity.z ==
        Catch::Approx(-2.4F).margin(0.000001F));
    REQUIRE(
        player.current.state.center_position.z ==
        Catch::Approx(-0.24F).margin(0.000001F));
}

TEST_CASE(
    "gentle and steep faces publish distinct contact phases",
    "[character][player-capsule][grounding][steep-contact]")
{
    using namespace shark;

    SECTION("gentle")
    {
        const auto surface =
            make_surface(make_plane_tile(0.5F));
        const auto support =
            character::query_player_terrain_support(
                {},
                {},
                surface,
                0.0F,
                0.0F);
        REQUIRE(support);
        REQUIRE(support.value());
        auto config = test_config({
            0.0F,
            support.value()->center_position_y,
            0.0F,
        });
        auto player = make_simulation(surface, config);
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::grounded);
        REQUIRE(character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            1.0F / 60.0F,
            1U));
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::grounded);
        REQUIRE(player.current.vertical.support_normal ==
            support.value()->surface.normal);
    }

    SECTION("steep")
    {
        const auto surface =
            make_surface(make_plane_tile(2.0F));
        const auto support =
            character::query_player_terrain_support(
                {},
                {},
                surface,
                0.0F,
                0.0F);
        REQUIRE(support);
        REQUIRE(support.value());
        REQUIRE_FALSE(support.value()->walkable);
        auto player = make_simulation(
            surface,
            test_config({
                0.0F,
                support.value()->center_position_y,
                0.0F,
            }));
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::steep_contact);
        REQUIRE(character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            1.0F / 60.0F,
            1U));
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::steep_contact);
        REQUIRE(positive_zero(
            player.current.vertical.velocity_y));

        auto falling = make_simulation(
            surface,
            test_config({0.0F, 5.0F, 0.0F}));
        std::uint64_t tick = 0U;
        do {
            ++tick;
            REQUIRE(character::advance_player_capsule(
                falling,
                {},
                {},
                surface,
                0.1F,
                tick));
            if (tick == 1U) {
                falling.current.horizontal_velocity =
                    {0.0F, 0.0F, -1.0F};
                REQUIRE(character::is_valid(falling));
            }
        } while (
            falling.current.vertical.phase ==
                character::PlayerGroundPhase::falling &&
            tick < 30U);
        REQUIRE(falling.current.vertical.phase ==
            character::PlayerGroundPhase::steep_contact);
        REQUIRE(falling.current.state.center_position.y ==
            support.value()->center_position_y);
        REQUIRE(falling.current.horizontal_velocity ==
            math::Float3{});
    }
}

TEST_CASE(
    "airborne reset restores and collapses grounded spawn",
    "[character][player-capsule][reset][airborne]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto player = make_simulation(surface);
    REQUIRE(character::advance_player_capsule(
        player,
        {},
        {},
        surface,
        1.0F / 60.0F,
        1U));

    player.current.state.center_position.y = 5.0F;
    player.current.vertical = {
        .velocity_y = -2.0F,
        .phase = character::PlayerGroundPhase::falling,
        .support_normal = {},
    };
    REQUIRE(character::is_valid(player));

    const character::PlayerActionCommand reset{
        .move_backward_held = true,
        .run_held = true,
        .jump_pressed = true,
        .reset_pressed = true,
    };
    const character::PlayerMovementFrame reset_frame{
        .right = {0.0F, 0.0F, 1.0F},
        .forward = {1.0F, 0.0F, 0.0F},
    };
    REQUIRE(character::advance_player_capsule(
        player,
        reset,
        reset_frame,
        surface,
        1.0F / 60.0F,
        2U));
    REQUIRE(player.previous.state ==
        player.current.state);
    REQUIRE(player.previous.vertical ==
        player.current.vertical);
    REQUIRE(player.previous.horizontal_velocity ==
        math::Float3{});
    REQUIRE(player.current.horizontal_velocity ==
        math::Float3{});
    REQUIRE(player.current.state.center_position ==
        player.config.spawn_center_position);
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::grounded);
    REQUIRE(player.current.reset_generation == 1U);
    REQUIRE(player.previous.fixed_tick == 1U);
    REQUIRE(player.current.fixed_tick == 2U);
    REQUIRE(player.previous.consumed_command ==
        character::PlayerActionCommand{});
    REQUIRE(player.current.consumed_command == reset);
    REQUIRE(player.previous.consumed_movement_frame ==
        character::PlayerMovementFrame{});
    REQUIRE(player.current.consumed_movement_frame ==
        reset_frame);

    const auto middle =
        character::interpolate_player_capsule(
            player,
            0.5F);
    REQUIRE(middle);
    REQUIRE(middle.value() == player.current.state);
}

TEST_CASE(
    "player interpolation collapse copies pose and vertical authority",
    "[character][player-capsule][interpolation][collapse]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto player = make_simulation(
        surface,
        test_config({0.0F, 3.0F, 0.0F}));
    REQUIRE(character::advance_player_capsule(
        player,
        {.move_forward_held = true},
        {},
        surface,
        0.1F,
        1U));
    REQUIRE(player.previous.state != player.current.state);
    REQUIRE(player.previous.vertical !=
        player.current.vertical);

    auto expected = player;
    expected.previous.state = expected.current.state;
    expected.previous.vertical =
        expected.current.vertical;
    expected.previous.horizontal_velocity =
        expected.current.horizontal_velocity;
    REQUIRE(character::collapse_player_capsule_interpolation(
        player));
    REQUIRE(player == expected);
    for (const auto alpha :
         std::array{0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
        const auto interpolated =
            character::interpolate_player_capsule(
                player,
                alpha);
        REQUIRE(interpolated);
        REQUIRE(interpolated.value() ==
            player.current.state);
    }

    auto invalid = player;
    invalid.current.vertical.velocity_y = 1.0F;
    const auto before = invalid;
    REQUIRE_FALSE(
        character::collapse_player_capsule_interpolation(
            invalid));
    REQUIRE(invalid == before);

    const auto wide = make_surface(make_wide_plane_tile());
    auto moving = make_simulation(wide);
    REQUIRE(character::advance_player_capsule(
        moving,
        {.move_right_held = true},
        {},
        wide,
        0.25F,
        1U));
    REQUIRE(moving.previous.horizontal_velocity !=
        moving.current.horizontal_velocity);
    auto moving_expected = moving;
    moving_expected.previous.state =
        moving_expected.current.state;
    moving_expected.previous.vertical =
        moving_expected.current.vertical;
    moving_expected.previous.horizontal_velocity =
        moving_expected.current.horizontal_velocity;
    REQUIRE(character::collapse_player_capsule_interpolation(
        moving));
    REQUIRE(moving == moving_expected);
}

TEST_CASE(
    "player advance rejects invalid input and rolls back",
    "[character][player-capsule][validation][rollback]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto player = make_simulation(surface);

    for (const auto delta :
         std::array{
             0.0F,
             -0.1F,
             std::numeric_limits<float>::quiet_NaN(),
             std::nextafter(
                 character::maximum_player_fixed_delta_seconds,
                 std::numeric_limits<float>::infinity()),
         }) {
        const auto before = player;
        const auto result =
            character::advance_player_capsule(
                player,
                {},
                {},
                surface,
                delta,
                1U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(player == before);
    }

    for (const auto tick :
         std::array<std::uint64_t, 3>{0U, 2U, 8U}) {
        const auto before = player;
        REQUIRE_FALSE(character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            1.0F / 60.0F,
            tick));
        REQUIRE(player == before);
    }

    auto invalid_command = character::PlayerActionCommand{};
    invalid_command.look_yaw_delta_radians =
        std::nextafter(
            character::maximum_player_look_delta_radians,
            std::numeric_limits<float>::infinity());
    const auto command_before = player;
    REQUIRE_FALSE(character::advance_player_capsule(
        player,
        invalid_command,
        {},
        surface,
        1.0F / 60.0F,
        1U));
    REQUIRE(player == command_before);

    auto invalid_frame = character::PlayerMovementFrame{};
    invalid_frame.forward.x =
        std::numeric_limits<float>::quiet_NaN();
    const auto frame_before = player;
    REQUIRE_FALSE(character::advance_player_capsule(
        player,
        {},
        invalid_frame,
        surface,
        1.0F / 60.0F,
        1U));
    REQUIRE(player == frame_before);

    auto malformed = player;
    malformed.current.vertical.phase =
        character::PlayerGroundPhase::falling;
    malformed.current.vertical.support_normal =
        {0.0F, 1.0F, 0.0F};
    const auto malformed_before = malformed;
    REQUIRE_FALSE(character::advance_player_capsule(
        malformed,
        {},
        {},
        surface,
        1.0F / 60.0F,
        1U));
    REQUIRE(malformed == malformed_before);

    auto malformed_rising = player;
    malformed_rising.current.vertical = {
        .velocity_y = 0.0F,
        .phase = character::PlayerGroundPhase::rising,
    };
    const auto rising_before = malformed_rising;
    REQUIRE_FALSE(character::advance_player_capsule(
        malformed_rising,
        {},
        {},
        surface,
        1.0F / 60.0F,
        1U));
    REQUIRE(malformed_rising == rising_before);

    auto malformed_falling = player;
    malformed_falling.current.vertical = {
        .velocity_y = 1.0F,
        .phase = character::PlayerGroundPhase::falling,
    };
    const auto falling_before = malformed_falling;
    REQUIRE_FALSE(character::advance_player_capsule(
        malformed_falling,
        {},
        {},
        surface,
        1.0F / 60.0F,
        1U));
    REQUIRE(malformed_falling == falling_before);

    auto malformed_horizontal = player;
    malformed_horizontal.current.horizontal_velocity =
        {1.0F, 0.0F, 0.0F};
    const auto horizontal_before = malformed_horizontal;
    REQUIRE_FALSE(character::advance_player_capsule(
        malformed_horizontal,
        {},
        {},
        surface,
        1.0F / 60.0F,
        1U));
    REQUIRE(malformed_horizontal == horizontal_before);

    auto excessive_air_speed = make_simulation(
        surface,
        test_config({0.0F, 3.0F, 0.0F}));
    REQUIRE(character::advance_player_capsule(
        excessive_air_speed,
        {},
        {},
        surface,
        1.0F / 60.0F,
        1U));
    excessive_air_speed.current.horizontal_velocity =
        {
            excessive_air_speed.config.ground_locomotion.run_speed +
                0.01F,
            0.0F,
            0.0F,
        };
    const auto excessive_before = excessive_air_speed;
    REQUIRE_FALSE(character::advance_player_capsule(
        excessive_air_speed,
        {},
        {},
        surface,
        1.0F / 60.0F,
        2U));
    REQUIRE(excessive_air_speed == excessive_before);

    auto below = player;
    REQUIRE(character::advance_player_capsule(
        below,
        {},
        {},
        surface,
        1.0F / 60.0F,
        1U));
    below.current.state.center_position.y = 0.5F;
    below.current.vertical = {
        .velocity_y = -1.0F,
        .phase = character::PlayerGroundPhase::falling,
    };
    const auto below_before = below;
    const auto below_result =
        character::advance_player_capsule(
            below,
            {},
            {},
            surface,
            1.0F / 60.0F,
            2U);
    REQUIRE_FALSE(below_result);
    REQUIRE(below_result.error().code() ==
        core::ErrorCode::invalid_state);
    REQUIRE(below == below_before);

    auto bounded = make_simulation(surface);
    REQUIRE(character::advance_player_capsule(
        bounded,
        {},
        {},
        surface,
        1.0F / 60.0F,
        1U));
    bounded.config.center_bounds.maximum.y = 2.0F;
    bounded.current.state.center_position.y = 1.9F;
    bounded.current.vertical = {
        .velocity_y = 6.5F,
        .phase = character::PlayerGroundPhase::rising,
    };
    REQUIRE(character::is_valid(bounded));
    const auto bounded_before = bounded;
    const auto bounded_result =
        character::advance_player_capsule(
            bounded,
            {},
            {},
            surface,
            0.25F,
            2U);
    REQUIRE_FALSE(bounded_result);
    REQUIRE(bounded_result.error().code() ==
        core::ErrorCode::unavailable);
    REQUIRE(bounded == bounded_before);
}

TEST_CASE(
    "player reset and fixed tick overflow remain transactional",
    "[character][player-capsule][overflow][rollback]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto player = make_simulation(surface);
    player.previous.fixed_tick =
        std::numeric_limits<std::uint64_t>::max() - 1U;
    player.current.fixed_tick =
        std::numeric_limits<std::uint64_t>::max();
    player.previous.reset_generation =
        std::numeric_limits<std::uint64_t>::max();
    player.current.reset_generation =
        std::numeric_limits<std::uint64_t>::max();
    REQUIRE(character::is_valid(player));
    const auto before = player;
    const auto result =
        character::advance_player_capsule(
            player,
            {.reset_pressed = true},
            {},
            surface,
            1.0F / 60.0F,
            0U);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code() ==
        core::ErrorCode::unavailable);
    REQUIRE(player == before);
}

TEST_CASE(
    "player pose interpolation remains bounded and shortest arc",
    "[character][player-capsule][interpolation][yaw]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto player = make_simulation(
        surface,
        test_config({0.0F, 5.0F, 0.0F}));
    player.previous = {
        .state = {
            .center_position = {-8.0F, 4.0F, 4.0F},
            .facing_yaw_radians =
                170.0F * math::pi / 180.0F,
        },
        .vertical = {
            .velocity_y = -2.0F,
            .phase = character::PlayerGroundPhase::falling,
        },
        .fixed_tick = 7U,
    };
    player.current = {
        .state = {
            .center_position = {8.0F, 2.0F, -4.0F},
            .facing_yaw_radians =
                -170.0F * math::pi / 180.0F,
        },
        .vertical = {
            .velocity_y = -4.0F,
            .phase = character::PlayerGroundPhase::falling,
        },
        .fixed_tick = 8U,
    };
    REQUIRE(character::is_valid(player));
    const auto before = player;

    const auto start =
        character::interpolate_player_capsule(
            player,
            0.0F);
    const auto middle =
        character::interpolate_player_capsule(
            player,
            0.5F);
    const auto end =
        character::interpolate_player_capsule(
            player,
            1.0F);
    REQUIRE(start);
    REQUIRE(middle);
    REQUIRE(end);
    REQUIRE(start.value() == player.previous.state);
    REQUIRE(end.value() == player.current.state);
    REQUIRE(middle.value().center_position ==
        math::Float3{0.0F, 3.0F, 0.0F});
    REQUIRE(std::abs(middle.value().facing_yaw_radians) ==
        Catch::Approx(math::pi).margin(0.000001F));
    REQUIRE(player == before);

    constexpr auto nan =
        std::numeric_limits<float>::quiet_NaN();
    for (const auto alpha : {-0.001F, 1.001F, nan}) {
        REQUIRE_FALSE(
            character::interpolate_player_capsule(
                player,
                alpha));
    }

    auto seam_config = test_config();
    seam_config.spawn_facing_yaw_radians =
        1.3000000365e-7F;
    auto seam = make_simulation(surface, seam_config);
    REQUIRE(character::advance_player_capsule(
        seam,
        {.look_yaw_delta_radians =
             std::nextafter(
                 math::pi,
                 -std::numeric_limits<float>::infinity())},
        {},
        surface,
        1.0F / 60.0F,
        1U));
}

TEST_CASE(
    "player vertical snapshots are invariant across render partitions",
    "[character][player-capsule][fixed-step][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30U,
        60U,
        120U,
        144U,
    };
    const auto baseline = run_schedule(render_rates[0]);
    REQUIRE(baseline.simulation.current.fixed_tick == 120U);
    REQUIRE(
        baseline.simulation.current.reset_generation == 1U);
    REQUIRE(baseline.trace[22].consumed_command.jump_pressed);
    REQUIRE(baseline.trace[22].vertical.phase ==
        shark::character::PlayerGroundPhase::rising);
    REQUIRE(baseline.trace[22].vertical.velocity_y > 0.0F);
    REQUIRE(baseline.trace[61].vertical.phase ==
        shark::character::PlayerGroundPhase::falling);
    REQUIRE(baseline.trace[61].vertical.velocity_y <= 0.0F);
    REQUIRE(baseline.trace[74].consumed_command.reset_pressed);
    REQUIRE(baseline.trace[74].vertical.phase ==
        shark::character::PlayerGroundPhase::grounded);

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        REQUIRE(run_schedule(render_rate) == baseline);
    }
}
