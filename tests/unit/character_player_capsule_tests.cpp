#include <shark/character/player_capsule.hpp>
#include <shark/simulation/fixed_step_clock.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] shark::character::PlayerCapsuleConfig test_config()
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
        .spawn_center_position = {1.0F, 3.0F, -2.0F},
        .spawn_facing_yaw_radians = 0.0F,
    };
}

[[nodiscard]] shark::character::PlayerCapsuleSimulation
make_simulation()
{
    auto result =
        shark::character::create_player_capsule(test_config());
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] bool positive_zero(const float value) noexcept
{
    return value == 0.0F && !std::signbit(value);
}

[[nodiscard]] float& component(
    shark::math::Float3& value,
    const std::size_t axis) noexcept
{
    if (axis == 0U) {
        return value.x;
    }
    if (axis == 1U) {
        return value.y;
    }
    return value.z;
}

[[nodiscard]] float component_value(
    const shark::math::Float3 value,
    const std::size_t axis) noexcept
{
    if (axis == 0U) {
        return value.x;
    }
    if (axis == 1U) {
        return value.y;
    }
    return value.z;
}

struct ScheduleRun final {
    shark::character::PlayerCapsuleSimulation simulation;
    std::vector<shark::character::PlayerCapsuleSnapshot> trace;

    [[nodiscard]] friend bool operator==(
        const ScheduleRun&,
        const ScheduleRun&) = default;
};

[[nodiscard]] shark::character::PlayerActionCommand
scripted_command(const std::uint64_t fixed_tick)
{
    return {
        .move_forward_held = fixed_tick >= 5U &&
            fixed_tick < 40U,
        .move_backward_held = fixed_tick >= 70U &&
            fixed_tick < 88U,
        .move_left_held = (fixed_tick % 7U) == 0U,
        .move_right_held = (fixed_tick % 11U) == 0U,
        .run_held = (fixed_tick % 3U) == 0U,
        .jump_pressed = fixed_tick == 23U,
        .primary_action_pressed = fixed_tick == 61U,
        .reset_pressed =
            fixed_tick == 47U || fixed_tick == 91U,
        .look_yaw_delta_radians =
            (fixed_tick % 5U) == 0U ? 0.125F : 0.0F,
        .look_pitch_delta_radians =
            (fixed_tick % 13U) == 0U ? -0.0625F : 0.0F,
    };
}

[[nodiscard]] ScheduleRun run_schedule(
    const std::uint32_t render_rate_hz)
{
    using namespace shark;

    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();
    auto player = make_simulation();
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
    "player capsule creation publishes one canonical bounded spawn",
    "[character][player-capsule][create][spawn]")
{
    using namespace shark;

    auto config = test_config();
    config.spawn_center_position.x = -0.0F;
    config.spawn_facing_yaw_radians = -0.0F;

    const auto first =
        character::create_player_capsule(config);
    const auto second =
        character::create_player_capsule(config);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first.value() == second.value());
    const auto& player = first.value();

    REQUIRE(character::is_valid(player));
    REQUIRE(player.config.shape.radius == 0.5F);
    REQUIRE(player.config.shape.vertical_half_segment == 0.5F);
    REQUIRE(positive_zero(
        player.config.spawn_center_position.x));
    REQUIRE(positive_zero(
        player.config.spawn_facing_yaw_radians));
    REQUIRE(player.previous == player.current);
    REQUIRE(player.current.state.center_position ==
        player.config.spawn_center_position);
    REQUIRE(player.current.state.facing_yaw_radians ==
        player.config.spawn_facing_yaw_radians);
    REQUIRE(player.current.fixed_tick == 0U);
    REQUIRE(player.current.reset_generation == 0U);
    REQUIRE(player.current.consumed_command ==
        character::PlayerActionCommand{});

    config = test_config();
    config.spawn_facing_yaw_radians = 5.0F * math::pi;
    const auto wrapped =
        character::create_player_capsule(config);
    REQUIRE(wrapped);
    REQUIRE(wrapped.value().current.state.facing_yaw_radians ==
        Catch::Approx(-math::pi).margin(0.000001F));
}

TEST_CASE(
    "player capsule configuration rejects malformed bounded state",
    "[character][player-capsule][create][validation]")
{
    using namespace shark;

    constexpr auto nan =
        std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity =
        std::numeric_limits<float>::infinity();

    SECTION("shape")
    {
        const std::array invalid_shapes{
            character::PlayerCapsuleShape{
                .radius = 0.0F,
                .vertical_half_segment = 0.5F,
            },
            character::PlayerCapsuleShape{
                .radius = -1.0F,
                .vertical_half_segment = 0.5F,
            },
            character::PlayerCapsuleShape{
                .radius = nan,
                .vertical_half_segment = 0.5F,
            },
            character::PlayerCapsuleShape{
                .radius = infinity,
                .vertical_half_segment = 0.5F,
            },
            character::PlayerCapsuleShape{
                .radius = std::nextafter(
                    character::maximum_player_capsule_radius,
                    infinity),
                .vertical_half_segment = 0.5F,
            },
            character::PlayerCapsuleShape{
                .radius = 0.5F,
                .vertical_half_segment = 0.0F,
            },
            character::PlayerCapsuleShape{
                .radius = 0.5F,
                .vertical_half_segment = -0.001F,
            },
            character::PlayerCapsuleShape{
                .radius = 0.5F,
                .vertical_half_segment = nan,
            },
            character::PlayerCapsuleShape{
                .radius = 0.5F,
                .vertical_half_segment = std::nextafter(
                    character::
                        maximum_player_capsule_vertical_half_segment,
                    infinity),
            },
        };
        for (const auto shape : invalid_shapes) {
            auto config = test_config();
            config.shape = shape;
            const auto result =
                character::create_player_capsule(config);
            REQUIRE_FALSE(result);
            REQUIRE(result.error().category() ==
                core::ErrorCategory::simulation);
            REQUIRE(result.error().code() ==
                core::ErrorCode::invalid_argument);
        }

        auto maximum = test_config();
        maximum.shape.radius =
            character::maximum_player_capsule_radius;
        maximum.shape.vertical_half_segment =
            character::
                maximum_player_capsule_vertical_half_segment;
        REQUIRE(character::create_player_capsule(maximum));

        auto adjacent_positive = test_config();
        adjacent_positive.shape.radius =
            std::numeric_limits<float>::denorm_min();
        adjacent_positive.shape.vertical_half_segment =
            std::numeric_limits<float>::denorm_min();
        REQUIRE(character::create_player_capsule(
            adjacent_positive));
    }

    SECTION("bounds")
    {
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            auto config = test_config();
            component(
                config.center_bounds.minimum,
                axis) = 2.0F;
            component(
                config.center_bounds.maximum,
                axis) = 1.0F;
            const auto result =
                character::create_player_capsule(config);
            REQUIRE_FALSE(result);
            REQUIRE(result.error().code() ==
                core::ErrorCode::invalid_argument);
        }

        auto nonfinite = test_config();
        nonfinite.center_bounds.minimum.y = -infinity;
        REQUIRE_FALSE(
            character::create_player_capsule(nonfinite));
    }

    SECTION("spawn")
    {
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            auto below = test_config();
            component(
                below.spawn_center_position,
                axis) = std::nextafter(
                component_value(
                    below.center_bounds.minimum,
                    axis),
                -infinity);
            const auto below_result =
                character::create_player_capsule(below);
            REQUIRE_FALSE(below_result);
            REQUIRE(below_result.error().code() ==
                core::ErrorCode::invalid_argument);

            auto above = test_config();
            component(
                above.spawn_center_position,
                axis) = std::nextafter(
                component_value(
                    above.center_bounds.maximum,
                    axis),
                infinity);
            REQUIRE_FALSE(
                character::create_player_capsule(above));
        }

        auto invalid_position = test_config();
        invalid_position.spawn_center_position.z = nan;
        REQUIRE_FALSE(character::create_player_capsule(
            invalid_position));

        for (const auto yaw : {nan, infinity, -infinity}) {
            auto config = test_config();
            config.spawn_facing_yaw_radians = yaw;
            REQUIRE_FALSE(
                character::create_player_capsule(config));
        }
    }

    SECTION("inclusive bounds")
    {
        auto minimum = test_config();
        minimum.spawn_center_position =
            minimum.center_bounds.minimum;
        const auto minimum_result =
            character::create_player_capsule(minimum);
        REQUIRE(minimum_result);

        auto maximum = test_config();
        maximum.spawn_center_position =
            maximum.center_bounds.maximum;
        const auto maximum_result =
            character::create_player_capsule(maximum);
        REQUIRE(maximum_result);
    }
}

TEST_CASE(
    "player action commands are semantic bounded tick samples",
    "[character][player-capsule][command][validation]")
{
    using namespace shark;

    character::PlayerActionCommand command{
        .move_forward_held = true,
        .move_backward_held = true,
        .move_left_held = true,
        .move_right_held = true,
        .run_held = true,
        .jump_pressed = true,
        .primary_action_pressed = true,
        .look_yaw_delta_radians =
            character::maximum_player_look_delta_radians,
        .look_pitch_delta_radians =
            -character::maximum_player_look_delta_radians,
    };
    REQUIRE(character::is_valid(command));

    auto player = make_simulation();
    REQUIRE(character::advance_player_capsule(
        player,
        command,
        1U));
    REQUIRE(player.current.consumed_command == command);
    const character::PlayerCapsuleState spawn{
        .center_position =
            player.config.spawn_center_position,
        .facing_yaw_radians =
            player.config.spawn_facing_yaw_radians,
    };
    REQUIRE(player.current.state == spawn);

    const auto before = player;
    constexpr auto infinity =
        std::numeric_limits<float>::infinity();
    const std::array<std::pair<float, float>, 4> invalid_look{{
        std::pair{
            std::nextafter(
                character::maximum_player_look_delta_radians,
                infinity),
            0.0F,
        },
        std::pair{
            0.0F,
            std::nextafter(
                -character::maximum_player_look_delta_radians,
                -infinity),
        },
        std::pair{
            std::numeric_limits<float>::quiet_NaN(),
            0.0F,
        },
        std::pair{0.0F, infinity},
    }};
    for (const auto [yaw, pitch] : invalid_look) {
        auto invalid = command;
        invalid.look_yaw_delta_radians = yaw;
        invalid.look_pitch_delta_radians = pitch;
        REQUIRE_FALSE(character::is_valid(invalid));
        const auto result =
            character::advance_player_capsule(
                player,
                invalid,
                2U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(player == before);
    }

    command = {};
    command.look_yaw_delta_radians = -0.0F;
    command.look_pitch_delta_radians = -0.0F;
    REQUIRE(character::advance_player_capsule(
        player,
        command,
        2U));
    REQUIRE(positive_zero(
        player.current.consumed_command
            .look_yaw_delta_radians));
    REQUIRE(positive_zero(
        player.current.consumed_command
            .look_pitch_delta_radians));
}

TEST_CASE(
    "player capsule ticks publish ordered snapshots transactionally",
    "[character][player-capsule][tick][snapshot][rollback]")
{
    using namespace shark;

    auto player = make_simulation();
    const auto initial = player.current;
    const character::PlayerActionCommand first_command{
        .move_forward_held = true,
        .run_held = true,
    };
    REQUIRE(character::advance_player_capsule(
        player,
        first_command,
        1U));
    REQUIRE(player.previous == initial);
    REQUIRE(player.current.fixed_tick == 1U);
    REQUIRE(player.current.reset_generation == 0U);
    REQUIRE(player.current.state == initial.state);
    REQUIRE(player.current.consumed_command == first_command);

    const auto after_first = player.current;
    const character::PlayerActionCommand second_command{
        .move_right_held = true,
        .jump_pressed = true,
    };
    REQUIRE(character::advance_player_capsule(
        player,
        second_command,
        2U));
    REQUIRE(player.previous == after_first);
    REQUIRE(player.current.fixed_tick == 2U);
    REQUIRE(player.current.state == after_first.state);
    REQUIRE(player.current.consumed_command == second_command);
    REQUIRE(character::is_valid(player));

    for (const auto invalid_tick :
         std::array<std::uint64_t, 3>{0U, 2U, 4U}) {
        const auto before = player;
        const auto result =
            character::advance_player_capsule(
                player,
                {},
                invalid_tick);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(player == before);
    }

    player.current.state.center_position.x =
        std::nextafter(
            player.config.center_bounds.maximum.x,
            std::numeric_limits<float>::infinity());
    const auto malformed = player;
    const auto malformed_result =
        character::advance_player_capsule(
            player,
            {},
            3U);
    REQUIRE_FALSE(malformed_result);
    REQUIRE(malformed_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(player == malformed);
}

TEST_CASE(
    "player reset starts a new collapsed snapshot generation",
    "[character][player-capsule][reset][snapshot]")
{
    using namespace shark;

    auto player = make_simulation();
    REQUIRE(character::advance_player_capsule(
        player,
        {.move_forward_held = true},
        1U));

    player.current.state = {
        .center_position = {5.0F, 8.0F, 4.0F},
        .facing_yaw_radians = 1.25F,
    };
    REQUIRE(character::is_valid(player));

    const character::PlayerActionCommand reset{
        .move_backward_held = true,
        .run_held = true,
        .jump_pressed = true,
        .primary_action_pressed = true,
        .reset_pressed = true,
        .look_yaw_delta_radians = 0.25F,
    };
    REQUIRE(character::advance_player_capsule(
        player,
        reset,
        2U));
    const character::PlayerCapsuleState spawn{
        .center_position =
            player.config.spawn_center_position,
        .facing_yaw_radians =
            player.config.spawn_facing_yaw_radians,
    };
    REQUIRE(player.previous.state == spawn);
    REQUIRE(player.current.state == spawn);
    REQUIRE(player.previous.fixed_tick == 1U);
    REQUIRE(player.current.fixed_tick == 2U);
    REQUIRE(player.previous.reset_generation == 1U);
    REQUIRE(player.current.reset_generation == 1U);
    REQUIRE(player.previous.consumed_command ==
        character::PlayerActionCommand{});
    REQUIRE(player.current.consumed_command == reset);

    const auto middle =
        character::interpolate_player_capsule(
            player,
            0.5F);
    REQUIRE(middle);
    REQUIRE(middle.value() == spawn);

    REQUIRE(character::advance_player_capsule(
        player,
        {.reset_pressed = true},
        3U));
    REQUIRE(player.previous.fixed_tick == 2U);
    REQUIRE(player.current.fixed_tick == 3U);
    REQUIRE(player.previous.reset_generation == 2U);
    REQUIRE(player.current.reset_generation == 2U);
    REQUIRE(player.previous.state == spawn);
    REQUIRE(player.current.state == spawn);
}

TEST_CASE(
    "player capsule rejects malformed history and tick overflow",
    "[character][player-capsule][validation][overflow][rollback]")
{
    using namespace shark;

    SECTION("snapshot gap")
    {
        auto player = make_simulation();
        REQUIRE(character::advance_player_capsule(
            player,
            {},
            1U));
        player.previous.fixed_tick = 4U;
        const auto before = player;
        REQUIRE_FALSE(character::is_valid(player));
        const auto result =
            character::advance_player_capsule(
                player,
                {},
                2U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(player == before);
    }

    SECTION("generation mismatch")
    {
        auto player = make_simulation();
        REQUIRE(character::advance_player_capsule(
            player,
            {},
            1U));
        player.previous.reset_generation = 1U;
        const auto before = player;
        REQUIRE_FALSE(character::is_valid(player));
        REQUIRE_FALSE(character::advance_player_capsule(
            player,
            {},
            2U));
        REQUIRE(player == before);
    }

    SECTION("fixed tick overflow")
    {
        auto player = make_simulation();
        player.previous.fixed_tick =
            std::numeric_limits<std::uint64_t>::max() - 1U;
        player.current.fixed_tick =
            std::numeric_limits<std::uint64_t>::max();
        const auto before = player;
        REQUIRE(character::is_valid(player));
        const auto result =
            character::advance_player_capsule(
                player,
                {.reset_pressed = true},
                0U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unavailable);
        REQUIRE(player == before);
    }
}

TEST_CASE(
    "player capsule interpolation uses bounded position and shortest yaw",
    "[character][player-capsule][interpolation][yaw]")
{
    using namespace shark;

    auto player = make_simulation();
    player.previous = {
        .state = {
            .center_position = {-8.0F, 0.0F, 4.0F},
            .facing_yaw_radians =
                170.0F * math::pi / 180.0F,
        },
        .fixed_tick = 7U,
    };
    player.current = {
        .state = {
            .center_position = {8.0F, 16.0F, -4.0F},
            .facing_yaw_radians =
                -170.0F * math::pi / 180.0F,
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
        math::Float3{0.0F, 8.0F, 0.0F});
    REQUIRE(std::abs(middle.value().facing_yaw_radians) ==
        Catch::Approx(math::pi).margin(0.000001F));
    REQUIRE(player == before);

    constexpr auto nan =
        std::numeric_limits<float>::quiet_NaN();
    for (const auto alpha : {-0.001F, 1.001F, nan}) {
        const auto result =
            character::interpolate_player_capsule(
                player,
                alpha);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    player.current.state.facing_yaw_radians = math::pi;
    REQUIRE_FALSE(character::is_valid(player));
    REQUIRE_FALSE(character::interpolate_player_capsule(
        player,
        0.5F));
}

TEST_CASE(
    "player capsule command traces are invariant across render partitions",
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
        baseline.simulation.current.reset_generation == 2U);

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        REQUIRE(run_schedule(render_rate) == baseline);
    }
}
