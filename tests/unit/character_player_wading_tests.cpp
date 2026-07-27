#include <shark/character/player_capsule.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/water/gameplay_water.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] shark::terrain::HeightTileSurface flat_surface()
{
    constexpr std::uint32_t sample_count = 65U;
    auto result = shark::terrain::HeightTileSurface::create({
        .sample_columns = sample_count,
        .sample_rows = sample_count,
        .sample_spacing = 1.0F,
        .origin = {-32.0F, 0.0F, -32.0F},
        .height_offsets = std::vector<float>(
            static_cast<std::size_t>(sample_count) *
                sample_count,
            0.0F),
    });
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::terrain::HeightTileSurface steep_surface()
{
    std::vector<float> heights;
    heights.reserve(25U);
    for (std::uint32_t z = 0U; z < 5U; ++z) {
        static_cast<void>(z);
        for (std::uint32_t x = 0U; x < 5U; ++x) {
            heights.push_back(
                2.0F * (static_cast<float>(x) - 2.0F));
        }
    }
    auto result = shark::terrain::HeightTileSurface::create({
        .sample_columns = 5U,
        .sample_rows = 5U,
        .sample_spacing = 1.0F,
        .origin = {-2.0F, 0.0F, -2.0F},
        .height_offsets = std::move(heights),
    });
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
            .minimum = {-32.0F, -8.0F, -32.0F},
            .maximum = {32.0F, 24.0F, 32.0F},
        },
        .spawn_center_position = spawn,
        .spawn_facing_yaw_radians = 0.0F,
    };
}

[[nodiscard]] shark::character::PlayerCapsuleSimulation make_player(
    const shark::terrain::HeightTileSurface& surface,
    shark::character::PlayerCapsuleConfig config = test_config())
{
    auto result =
        shark::character::create_player_capsule(config, surface);
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::water::CalmWaterBody water_body(
    const float depth,
    const std::optional<shark::water::HorizontalFlow> flow =
        shark::water::HorizontalFlow{})
{
    return {
        .footprint = {
            .center_x = 0.0F,
            .center_z = 0.0F,
            .semi_axis_x = 64.0F,
            .semi_axis_z = 64.0F,
            .x_warp_square_offset = 0.0F,
            .x_warp_divisor = 4'096.0F,
            .z_warp_square_offset = 0.0F,
            .z_warp_divisor = 4'096.0F,
        },
        .support_side =
            shark::water::CalmWaterSupportSide::
                inside_warped_footprint,
        .surface_height = depth,
        .shoreline_depth_tolerance =
            shark::water::default_shoreline_depth_tolerance,
        .flow_velocity = flow,
    };
}

[[nodiscard]] shark::water::GameplayWaterQuery query_at_player(
    const shark::water::CalmWaterBody& body,
    const shark::terrain::HeightTileSurface& surface,
    const shark::character::PlayerCapsuleSimulation& player)
{
    const auto result = shark::water::query_gameplay_water(
        body,
        surface,
        player.current.state.center_position.x,
        player.current.state.center_position.z);
    REQUIRE(result);
    REQUIRE(shark::water::is_valid(result.value()));
    return result.value();
}

void advance_with_body(
    shark::character::PlayerCapsuleSimulation& player,
    const shark::terrain::HeightTileSurface& surface,
    const shark::water::CalmWaterBody& body,
    const std::uint64_t fixed_tick,
    const shark::character::PlayerActionCommand command = {},
    const float fixed_delta_seconds = 1.0F / 60.0F,
    const shark::character::PlayerMovementFrame movement_frame = {})
{
    const auto query = query_at_player(body, surface, player);
    REQUIRE(shark::character::advance_player_capsule(
        player,
        command,
        movement_frame,
        surface,
        query,
        fixed_delta_seconds,
        fixed_tick));
}

[[nodiscard]] bool positive_zero(const float value) noexcept
{
    return value == 0.0F && !std::signbit(value);
}

} // namespace

TEST_CASE(
    "player wading settings and snapshot state are bounded",
    "[character][player-capsule][wading][validation]")
{
    using namespace shark;

    const character::PlayerWadingSettings defaults;
    REQUIRE(defaults.enter_depth == 0.25F);
    REQUIRE(defaults.exit_depth == 0.125F);
    REQUIRE(defaults.depth_for_minimum_speed == 1.5F);
    REQUIRE(defaults.minimum_speed_multiplier == 0.5F);
    REQUIRE(character::is_valid(defaults));

    std::vector<character::PlayerWadingSettings> invalid_settings;
    auto invalid = defaults;
    invalid.enter_depth = 0.0F;
    invalid_settings.push_back(invalid);
    invalid = defaults;
    invalid.enter_depth =
        std::numeric_limits<float>::quiet_NaN();
    invalid_settings.push_back(invalid);
    invalid = defaults;
    invalid.exit_depth = defaults.enter_depth;
    invalid_settings.push_back(invalid);
    invalid = defaults;
    invalid.exit_depth = -0.001F;
    invalid_settings.push_back(invalid);
    invalid = defaults;
    invalid.depth_for_minimum_speed = defaults.enter_depth;
    invalid_settings.push_back(invalid);
    invalid = defaults;
    invalid.depth_for_minimum_speed =
        character::maximum_player_wading_depth + 1.0F;
    invalid_settings.push_back(invalid);
    invalid = defaults;
    invalid.minimum_speed_multiplier = 0.0F;
    invalid_settings.push_back(invalid);
    invalid = defaults;
    invalid.minimum_speed_multiplier = 1.001F;
    invalid_settings.push_back(invalid);

    const auto surface = flat_surface();
    for (const auto& settings : invalid_settings) {
        CAPTURE(
            settings.enter_depth,
            settings.exit_depth,
            settings.depth_for_minimum_speed,
            settings.minimum_speed_multiplier);
        REQUIRE_FALSE(character::is_valid(settings));
        auto config = test_config();
        config.wading = settings;
        REQUIRE_FALSE(character::create_player_capsule(
            config,
            surface));
    }

    auto player = make_player(surface);
    REQUIRE(player.current.water ==
        character::PlayerWaterState{});
    REQUIRE(positive_zero(player.current.water.depth));
    advance_with_body(
        player,
        surface,
        water_body(0.5F),
        1U);
    REQUIRE(character::is_valid(player));

    auto malformed = player;
    malformed.current.water.depth =
        character::default_player_wading_exit_depth;
    REQUIRE_FALSE(character::is_valid(malformed));

    malformed = player;
    malformed.current.water.phase =
        static_cast<character::PlayerWaterPhase>(0);
    REQUIRE_FALSE(character::is_valid(malformed));

    malformed = make_player(surface);
    malformed.current.water.depth = -0.0F;
    REQUIRE_FALSE(character::is_valid(malformed));

    malformed = player;
    malformed.current.vertical = {
        .velocity_y = 1.0F,
        .phase = character::PlayerGroundPhase::rising,
    };
    REQUIRE_FALSE(character::is_valid(malformed));
}

TEST_CASE(
    "wading entry and exit use exact hysteresis thresholds",
    "[character][player-capsule][wading][hysteresis][threshold]")
{
    using namespace shark;

    const auto surface = flat_surface();
    auto player = make_player(surface);
    const auto below_enter = std::nextafter(
        character::default_player_wading_enter_depth,
        -std::numeric_limits<float>::infinity());
    const auto above_exit = std::nextafter(
        character::default_player_wading_exit_depth,
        std::numeric_limits<float>::infinity());

    advance_with_body(
        player,
        surface,
        water_body(below_enter),
        1U);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::dry);

    advance_with_body(
        player,
        surface,
        water_body(character::default_player_wading_enter_depth),
        2U);
    REQUIRE(player.current.water ==
        character::PlayerWaterState{
            .phase = character::PlayerWaterPhase::wading,
            .depth =
                character::default_player_wading_enter_depth,
        });

    advance_with_body(
        player,
        surface,
        water_body(above_exit),
        3U);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::wading);
    REQUIRE(player.current.water.depth == above_exit);

    advance_with_body(
        player,
        surface,
        water_body(character::default_player_wading_exit_depth),
        4U);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::dry);
    REQUIRE(positive_zero(player.current.water.depth));

    advance_with_body(
        player,
        surface,
        water_body(0.2F),
        5U);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::dry);

    advance_with_body(
        player,
        surface,
        water_body(character::default_player_wading_enter_depth),
        6U);
    advance_with_body(
        player,
        surface,
        water_body(0.2F),
        7U);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::wading);
    REQUIRE(player.current.water.depth == 0.2F);

    advance_with_body(
        player,
        surface,
        water_body(-1.0F),
        8U);
    REQUIRE(player.current.water ==
        character::PlayerWaterState{});
    REQUIRE(character::is_valid(player));
}

TEST_CASE(
    "wading scales ground target speed until surface-swim entry",
    "[character][player-capsule][wading][locomotion][speed]")
{
    using namespace shark;

    const auto surface = flat_surface();
    constexpr character::PlayerActionCommand walk{
        .move_forward_held = true,
    };
    constexpr character::PlayerActionCommand run{
        .move_forward_held = true,
        .run_held = true,
    };

    auto player = make_player(surface);
    advance_with_body(
        player,
        surface,
        water_body(0.875F),
        1U,
        walk,
        0.25F);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::wading);
    REQUIRE(player.current.horizontal_velocity ==
        math::Float3{0.0F, 0.0F, -3.0F});
    REQUIRE(player.current.state.center_position.z == -0.75F);

    const auto below_swim_entry = std::nextafter(
        character::default_player_surface_swimming_enter_depth,
        -std::numeric_limits<float>::infinity());
    advance_with_body(
        player,
        surface,
        water_body(below_swim_entry),
        2U,
        walk,
        0.25F);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::wading);
    REQUIRE(player.current.horizontal_velocity.z ==
        Catch::Approx(-2.0F).margin(0.000001F));
    REQUIRE(player.current.state.center_position.z ==
        Catch::Approx(-1.25F).margin(0.000001F));

    advance_with_body(
        player,
        surface,
        water_body(
            character::
                default_player_surface_swimming_enter_depth),
        3U,
        walk,
        0.25F);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::surface_swimming);
    REQUIRE(player.current.horizontal_velocity ==
        math::Float3{0.0F, 0.0F, -3.0F});
    REQUIRE(player.current.state.center_position.z ==
        Catch::Approx(-2.0F).margin(0.000001F));

    auto island_depth_player = make_player(surface);
    advance_with_body(
        island_depth_player,
        surface,
        water_body(0.33984375F),
        1U,
        run,
        0.25F);
    advance_with_body(
        island_depth_player,
        surface,
        water_body(0.33984375F),
        2U,
        run,
        0.25F);
    REQUIRE(
        island_depth_player.current.horizontal_velocity.z ==
        Catch::Approx(-6.7484375F).margin(0.000001F));

    advance_with_body(
        island_depth_player,
        surface,
        water_body(1.359375F),
        3U,
        run,
        0.25F);
    REQUIRE(
        island_depth_player.current.horizontal_velocity.z ==
        Catch::Approx(-3.89375F).margin(0.000001F));

    auto calm = make_player(surface);
    auto flowing = make_player(surface);
    advance_with_body(
        calm,
        surface,
        water_body(0.875F, water::HorizontalFlow{}),
        1U,
        walk,
        0.25F);
    advance_with_body(
        flowing,
        surface,
        water_body(
            0.875F,
            water::HorizontalFlow{100.0F, -50.0F}),
        1U,
        walk,
        0.25F);
    REQUIRE(flowing == calm);
}

TEST_CASE(
    "jumping clears wading and landing reclassifies on the next tick",
    "[character][player-capsule][wading][jump][landing][latency]")
{
    using namespace shark;

    const auto surface = flat_surface();
    const auto body = water_body(0.5F);
    auto player = make_player(surface);

    advance_with_body(player, surface, body, 1U);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::wading);

    advance_with_body(
        player,
        surface,
        body,
        2U,
        {.jump_pressed = true});
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::rising);
    REQUIRE(player.current.vertical.velocity_y ==
        Catch::Approx(
            character::default_player_jump_launch_speed -
            character::default_player_gravity_magnitude / 60.0F)
            .margin(0.000001F));
    REQUIRE(player.current.water ==
        character::PlayerWaterState{});

    std::uint64_t landing_tick = 0U;
    for (std::uint64_t tick = 3U; tick <= 160U; ++tick) {
        advance_with_body(player, surface, body, tick);
        if (player.current.vertical.phase ==
            character::PlayerGroundPhase::landing) {
            landing_tick = tick;
            REQUIRE(player.current.water ==
                character::PlayerWaterState{});
            break;
        }
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});
    }
    REQUIRE(landing_tick != 0U);

    advance_with_body(
        player,
        surface,
        body,
        landing_tick + 1U);
    REQUIRE(player.previous.vertical.phase ==
        character::PlayerGroundPhase::landing);
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::grounded);
    REQUIRE(player.current.water ==
        character::PlayerWaterState{
            .phase = character::PlayerWaterPhase::wading,
            .depth = 0.5F,
        });
}

TEST_CASE(
    "steep contact reset and interpolation collapse publish dry water state",
    "[character][player-capsule][wading][steep][reset][interpolation]")
{
    using namespace shark;

    SECTION("steep contact cannot wade")
    {
        const auto surface = steep_surface();
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
        auto player = make_player(
            surface,
            test_config({
                0.0F,
                support.value()->center_position_y,
                0.0F,
            }));
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::steep_contact);
        advance_with_body(
            player,
            surface,
            water_body(1.0F),
            1U);
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::steep_contact);
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});
    }

    SECTION("reset wins over a valid wet source query")
    {
        const auto surface = flat_surface();
        auto player = make_player(surface);
        advance_with_body(
            player,
            surface,
            water_body(0.5F),
            1U);
        advance_with_body(
            player,
            surface,
            water_body(1.0F),
            2U);
        REQUIRE(player.previous.water.depth == 0.5F);
        REQUIRE(player.current.water.depth == 1.0F);

        REQUIRE(character::collapse_player_capsule_interpolation(
            player));
        REQUIRE(player.previous.water == player.current.water);
        REQUIRE(player.previous.water.depth == 1.0F);

        const character::PlayerActionCommand reset{
            .jump_pressed = true,
            .reset_pressed = true,
        };
        advance_with_body(
            player,
            surface,
            water_body(1.0F),
            3U,
            reset);
        REQUIRE(player.previous.water ==
            character::PlayerWaterState{});
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});
        REQUIRE(player.previous.state == player.current.state);
        REQUIRE(player.current.state.center_position ==
            player.config.spawn_center_position);
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::grounded);
        REQUIRE(player.current.reset_generation == 1U);
        REQUIRE(player.current.consumed_command == reset);

        advance_with_body(
            player,
            surface,
            water_body(0.2F),
            4U);
        REQUIRE(player.current.water.phase ==
            character::PlayerWaterPhase::dry);
    }
}

TEST_CASE(
    "malformed or terrain-inconsistent water observations roll back",
    "[character][player-capsule][wading][validation][rollback]")
{
    using namespace shark;

    const auto surface = flat_surface();
    const auto initial = make_player(surface);
    std::vector<water::GameplayWaterQuery> invalid_queries;

    // Structurally valid, but impossible over the player's valid terrain
    // support.
    invalid_queries.push_back({});
    invalid_queries.push_back({
        .disposition = water::GameplayWaterDisposition::no_water,
        .horizontal_support = false,
        .surface_height = 0.0F,
        .bed_height = 1.0F,
        .depth = 0.0F,
    });

    invalid_queries.push_back({
        .disposition = water::GameplayWaterDisposition::water,
        .horizontal_support = true,
        .surface_height = 0.5F,
        .bed_height = 0.0F,
        .depth = 0.4F,
    });
    invalid_queries.push_back({
        .disposition = water::GameplayWaterDisposition::water,
        .horizontal_support = true,
        .surface_height = std::numeric_limits<float>::max(),
        .bed_height = -std::numeric_limits<float>::max(),
        .depth = std::numeric_limits<float>::max(),
    });
    invalid_queries.push_back({
        .disposition = water::GameplayWaterDisposition::no_water,
        .horizontal_support = false,
        .surface_height =
            std::numeric_limits<float>::quiet_NaN(),
        .bed_height = 0.0F,
        .depth = 0.0F,
    });
    invalid_queries.push_back({
        .disposition =
            static_cast<water::GameplayWaterDisposition>(0),
        .horizontal_support = false,
        .surface_height = 0.0F,
        .bed_height = 0.0F,
        .depth = 0.0F,
    });

    for (const auto& invalid_query : invalid_queries) {
        auto player = initial;
        const auto before = player;
        const auto result = character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            invalid_query,
            1.0F / 60.0F,
            1U);
        REQUIRE_FALSE(result);
        REQUIRE(player == before);
    }

    const auto valid_query = query_at_player(
        water_body(0.5F),
        surface,
        initial);
    REQUIRE(water::is_valid(valid_query));
    auto player = initial;
    REQUIRE(character::advance_player_capsule(
        player,
        {},
        {},
        surface,
        valid_query,
        1.0F / 60.0F,
        1U));
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::wading);
}
