#include <shark/character/player_capsule.hpp>
#include <shark/simulation/fixed_step_clock.hpp>
#include <shark/terrain/height_tile.hpp>

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

    const auto surface = make_surface();
    auto player = make_simulation(
        surface,
        test_config({0.0F, 5.0F, 0.0F}));
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
    "grounded player remains exactly stable under gravity",
    "[character][player-capsule][grounded][stability]")
{
    using namespace shark;

    const auto surface = make_surface();
    auto player = make_simulation(surface);
    for (std::uint64_t tick = 1U; tick <= 600U; ++tick) {
        const character::PlayerActionCommand command{
            .move_forward_held = tick % 2U == 0U,
            .run_held = tick % 3U == 0U,
        };
        REQUIRE(character::advance_player_capsule(
            player,
            command,
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
        {},
        surface,
        delta_seconds,
        landing_tick + 1U));
    REQUIRE(player.previous.vertical.phase ==
        character::PlayerGroundPhase::landing);
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::grounded);
    REQUIRE(player.current.state.center_position.y == 1.0F);
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
                surface,
                0.1F,
                tick));
        } while (
            falling.current.vertical.phase ==
                character::PlayerGroundPhase::falling &&
            tick < 30U);
        REQUIRE(falling.current.vertical.phase ==
            character::PlayerGroundPhase::steep_contact);
        REQUIRE(falling.current.state.center_position.y ==
            support.value()->center_position_y);
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
    REQUIRE(character::advance_player_capsule(
        player,
        reset,
        surface,
        1.0F / 60.0F,
        2U));
    REQUIRE(player.previous.state ==
        player.current.state);
    REQUIRE(player.previous.vertical ==
        player.current.vertical);
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
        surface,
        1.0F / 60.0F,
        1U));
    REQUIRE(player == command_before);

    auto malformed = player;
    malformed.current.vertical.phase =
        character::PlayerGroundPhase::falling;
    malformed.current.vertical.support_normal =
        {0.0F, 1.0F, 0.0F};
    const auto malformed_before = malformed;
    REQUIRE_FALSE(character::advance_player_capsule(
        malformed,
        {},
        surface,
        1.0F / 60.0F,
        1U));
    REQUIRE(malformed == malformed_before);

    auto below = player;
    REQUIRE(character::advance_player_capsule(
        below,
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
            surface,
            1.0F / 60.0F,
            2U);
    REQUIRE_FALSE(below_result);
    REQUIRE(below_result.error().code() ==
        core::ErrorCode::invalid_state);
    REQUIRE(below == below_before);

    auto bounded = make_simulation(
        surface,
        test_config({0.0F, 2.0F, 0.0F}));
    bounded.config.center_bounds.minimum.y = 1.999F;
    const auto bounded_before = bounded;
    const auto bounded_result =
        character::advance_player_capsule(
            bounded,
            {},
            surface,
            0.25F,
            1U);
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

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        REQUIRE(run_schedule(render_rate) == baseline);
    }
}
