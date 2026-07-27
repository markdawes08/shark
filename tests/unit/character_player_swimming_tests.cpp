#include <shark/character/player_capsule.hpp>
#include <shark/core/error.hpp>
#include <shark/terrain/height_tile.hpp>
#include <shark/water/gameplay_water.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] shark::terrain::HeightTileSurface flat_surface(
    const std::uint32_t sample_count = 65U,
    const float origin_x = -32.0F,
    const float origin_z = -32.0F)
{
    auto result = shark::terrain::HeightTileSurface::create({
        .sample_columns = sample_count,
        .sample_rows = sample_count,
        .sample_spacing = 1.0F,
        .origin = {origin_x, 0.0F, origin_z},
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
    const float surface_height,
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
        .surface_height = surface_height,
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

void enter_surface_swimming(
    shark::character::PlayerCapsuleSimulation& player,
    const shark::terrain::HeightTileSurface& surface,
    const shark::water::CalmWaterBody& body,
    const float fixed_delta_seconds = 1.0F / 60.0F)
{
    using namespace shark;

    advance_with_body(
        player,
        surface,
        body,
        1U,
        {},
        fixed_delta_seconds);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::wading);
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::grounded);

    advance_with_body(
        player,
        surface,
        body,
        2U,
        {},
        fixed_delta_seconds);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::surface_swimming);
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::surface_swimming);
}

[[nodiscard]] bool positive_zero(const float value) noexcept
{
    return value == 0.0F && !std::signbit(value);
}

} // namespace

TEST_CASE(
    "surface-swimming settings and snapshot state are bounded",
    "[character][player-capsule][surface-swimming][validation]")
{
    using namespace shark;

    const character::PlayerSurfaceSwimmingSettings defaults;
    REQUIRE(defaults.enter_depth == 1.5F);
    REQUIRE(defaults.exit_depth == 1.25F);
    REQUIRE(defaults.surface_center_depth == 0.5F);
    REQUIRE(defaults.speed == 3.0F);
    REQUIRE(character::is_valid(defaults));

    std::vector<character::PlayerSurfaceSwimmingSettings>
        invalid_settings;
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
    invalid.surface_center_depth = -0.001F;
    invalid_settings.push_back(invalid);
    invalid = defaults;
    invalid.surface_center_depth =
        std::numeric_limits<float>::infinity();
    invalid_settings.push_back(invalid);
    invalid = defaults;
    invalid.speed = 0.0F;
    invalid_settings.push_back(invalid);
    invalid = defaults;
    invalid.speed =
        std::numeric_limits<float>::quiet_NaN();
    invalid_settings.push_back(invalid);

    const auto surface = flat_surface();
    for (const auto& settings : invalid_settings) {
        CAPTURE(
            settings.enter_depth,
            settings.exit_depth,
            settings.surface_center_depth,
            settings.speed);
        REQUIRE_FALSE(character::is_valid(settings));
        auto config = test_config();
        config.surface_swimming = settings;
        REQUIRE_FALSE(character::create_player_capsule(
            config,
            surface));
    }

    std::vector<character::PlayerCapsuleConfig> invalid_configs;
    auto invalid_config = test_config();
    invalid_config.surface_swimming.exit_depth =
        invalid_config.wading.enter_depth;
    invalid_configs.push_back(invalid_config);
    invalid_config = test_config();
    invalid_config.surface_swimming.enter_depth =
        std::nextafter(
            invalid_config.wading.depth_for_minimum_speed,
            -std::numeric_limits<float>::infinity());
    invalid_configs.push_back(invalid_config);
    invalid_config = test_config();
    invalid_config.surface_swimming.speed =
        invalid_config.ground_locomotion.run_speed + 1.0F;
    invalid_configs.push_back(invalid_config);
    for (const auto& config : invalid_configs) {
        REQUIRE(character::is_valid(config.surface_swimming));
        REQUIRE_FALSE(character::create_player_capsule(
            config,
            surface));
    }

    auto dry_with_active_surface = make_player(surface);
    dry_with_active_surface.current.water.surface_height = 1.0F;
    REQUIRE_FALSE(character::is_valid(
        dry_with_active_surface));

    auto player = make_player(surface);
    enter_surface_swimming(
        player,
        surface,
        water_body(4.0F));
    REQUIRE(character::is_valid(player));
    REQUIRE(player.current.water ==
        character::PlayerWaterState{
            .phase =
                character::PlayerWaterPhase::surface_swimming,
            .depth = 4.0F,
            .surface_height = 4.0F,
        });
    REQUIRE(positive_zero(
        player.current.vertical.velocity_y));
    REQUIRE(player.current.vertical.support_normal ==
        math::Float3{});

    auto malformed = player;
    malformed.current.water.depth =
        defaults.exit_depth;
    REQUIRE_FALSE(character::is_valid(malformed));

    malformed = player;
    malformed.current.water.surface_height = -0.0F;
    REQUIRE_FALSE(character::is_valid(malformed));

    malformed = player;
    malformed.current.water.phase =
        static_cast<character::PlayerWaterPhase>(0);
    REQUIRE_FALSE(character::is_valid(malformed));

    malformed = player;
    malformed.current.vertical.phase =
        character::PlayerGroundPhase::grounded;
    malformed.current.vertical.support_normal =
        {0.0F, 1.0F, 0.0F};
    REQUIRE_FALSE(character::is_valid(malformed));

    malformed = player;
    malformed.current.vertical.velocity_y = 1.0F;
    REQUIRE_FALSE(character::is_valid(malformed));

    malformed = player;
    malformed.current.vertical.support_normal =
        {0.0F, 1.0F, 0.0F};
    REQUIRE_FALSE(character::is_valid(malformed));

    malformed = player;
    malformed.current.water = {
        .phase = character::PlayerWaterPhase::wading,
        .depth = 1.4F,
    };
    REQUIRE_FALSE(character::is_valid(malformed));

    malformed = player;
    malformed.current.vertical.phase =
        static_cast<character::PlayerGroundPhase>(0);
    REQUIRE_FALSE(character::is_valid(malformed));

    malformed = player;
    malformed.current.state.center_position.y =
        player.current.water.surface_height -
        player.config.surface_swimming.surface_center_depth -
        0.001F;
    REQUIRE_FALSE(character::is_valid(malformed));

    const auto valid_query = query_at_player(
        water_body(4.0F),
        surface,
        player);
    auto rollback = player;
    rollback.current.vertical.velocity_y = 1.0F;
    const auto before = rollback;
    const auto result = character::advance_player_capsule(
        rollback,
        {},
        {},
        surface,
        valid_query,
        1.0F / 60.0F,
        3U);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().category() ==
        core::ErrorCategory::simulation);
    REQUIRE(result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(rollback == before);
}

TEST_CASE(
    "surface swimming uses exact directional hysteresis and surface height",
    "[character][player-capsule][surface-swimming][hysteresis][height]")
{
    using namespace shark;

    const auto surface = flat_surface();
    auto player = make_player(surface);
    const auto below_enter = std::nextafter(
        character::PlayerSurfaceSwimmingSettings{}.enter_depth,
        -std::numeric_limits<float>::infinity());
    const auto above_exit = std::nextafter(
        character::PlayerSurfaceSwimmingSettings{}.exit_depth,
        std::numeric_limits<float>::infinity());

    advance_with_body(
        player,
        surface,
        water_body(below_enter),
        1U);
    REQUIRE(player.current.water ==
        character::PlayerWaterState{
            .phase = character::PlayerWaterPhase::wading,
            .depth = below_enter,
        });
    REQUIRE(positive_zero(
        player.current.water.surface_height));

    advance_with_body(
        player,
        surface,
        water_body(below_enter),
        2U);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::wading);

    advance_with_body(
        player,
        surface,
        water_body(1.5F),
        3U);
    REQUIRE(player.current.water ==
        character::PlayerWaterState{
            .phase =
                character::PlayerWaterPhase::surface_swimming,
            .depth = 1.5F,
            .surface_height = 1.5F,
        });
    REQUIRE(player.current.state.center_position.y == 1.0F);

    advance_with_body(
        player,
        surface,
        water_body(above_exit),
        4U);
    REQUIRE(player.current.water ==
        character::PlayerWaterState{
            .phase =
                character::PlayerWaterPhase::surface_swimming,
            .depth = above_exit,
            .surface_height = above_exit,
        });
    REQUIRE(player.current.state.center_position.y == 1.0F);

    advance_with_body(
        player,
        surface,
        water_body(1.25F),
        5U);
    REQUIRE(player.current.water ==
        character::PlayerWaterState{
            .phase = character::PlayerWaterPhase::wading,
            .depth = 1.25F,
        });
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::grounded);
    REQUIRE(player.current.state.center_position.y == 1.0F);
    REQUIRE(positive_zero(
        player.current.water.surface_height));

    advance_with_body(
        player,
        surface,
        water_body(1.359375F),
        6U);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::wading);

    advance_with_body(
        player,
        surface,
        water_body(1.5F),
        7U);
    REQUIRE(player.current.water.phase ==
        character::PlayerWaterPhase::surface_swimming);
    advance_with_body(
        player,
        surface,
        water_body(1.359375F),
        8U);
    REQUIRE(player.current.water ==
        character::PlayerWaterState{
            .phase =
                character::PlayerWaterPhase::surface_swimming,
            .depth = 1.359375F,
            .surface_height = 1.359375F,
        });
    REQUIRE(player.current.state.center_position.y == 1.0F);

    advance_with_body(
        player,
        surface,
        water_body(-1.0F),
        9U);
    REQUIRE(player.current.water ==
        character::PlayerWaterState{});
    REQUIRE(player.current.vertical.phase ==
        character::PlayerGroundPhase::grounded);
    REQUIRE(player.current.state.center_position.y == 1.0F);
}

TEST_CASE(
    "surface-swim movement is camera relative fixed speed and ignores run and flow",
    "[character][player-capsule][surface-swimming][locomotion]")
{
    using namespace shark;

    const auto surface = flat_surface();
    const auto calm_body = water_body(4.0F);
    const auto flowing_body = water_body(
        4.0F,
        water::HorizontalFlow{100.0F, -50.0F});
    auto walk = make_player(surface);
    auto run = make_player(surface);
    auto flowing = make_player(surface);
    enter_surface_swimming(
        walk,
        surface,
        calm_body,
        0.25F);
    enter_surface_swimming(
        run,
        surface,
        calm_body,
        0.25F);
    enter_surface_swimming(
        flowing,
        surface,
        flowing_body,
        0.25F);

    constexpr character::PlayerActionCommand diagonal_walk{
        .move_forward_held = true,
        .move_right_held = true,
    };
    constexpr character::PlayerActionCommand diagonal_run{
        .move_forward_held = true,
        .move_right_held = true,
        .run_held = true,
    };
    constexpr character::PlayerMovementFrame rotated_frame{
        .right = {0.0F, 0.0F, 1.0F},
        .forward = {1.0F, 0.0F, 0.0F},
    };

    advance_with_body(
        walk,
        surface,
        calm_body,
        3U,
        diagonal_walk,
        0.25F,
        rotated_frame);
    advance_with_body(
        run,
        surface,
        calm_body,
        3U,
        diagonal_run,
        0.25F,
        rotated_frame);
    advance_with_body(
        flowing,
        surface,
        flowing_body,
        3U,
        diagonal_run,
        0.25F,
        rotated_frame);

    REQUIRE(run.current.state == walk.current.state);
    REQUIRE(run.current.vertical == walk.current.vertical);
    REQUIRE(run.current.water == walk.current.water);
    REQUIRE(run.current.horizontal_velocity ==
        walk.current.horizontal_velocity);
    REQUIRE(flowing.current.state == walk.current.state);
    REQUIRE(flowing.current.vertical == walk.current.vertical);
    REQUIRE(flowing.current.water == walk.current.water);
    REQUIRE(flowing.current.horizontal_velocity ==
        walk.current.horizontal_velocity);
    REQUIRE(std::hypot(
        walk.current.horizontal_velocity.x,
        walk.current.horizontal_velocity.z) ==
        Catch::Approx(3.0F).margin(0.000001F));
    REQUIRE(walk.current.horizontal_velocity.x ==
        Catch::Approx(
            3.0F / std::sqrt(2.0F))
            .margin(0.000001F));
    REQUIRE(walk.current.horizontal_velocity.z ==
        Catch::Approx(
            3.0F / std::sqrt(2.0F))
            .margin(0.000001F));
    REQUIRE(walk.current.state.center_position.x ==
        Catch::Approx(
            walk.previous.state.center_position.x +
            walk.current.horizontal_velocity.x * 0.25F)
            .margin(0.000001F));
    REQUIRE(walk.current.state.center_position.z ==
        Catch::Approx(
            walk.previous.state.center_position.z +
            walk.current.horizontal_velocity.z * 0.25F)
            .margin(0.000001F));
    REQUIRE(walk.current.state.center_position.y == 3.5F);

    advance_with_body(
        walk,
        surface,
        calm_body,
        4U,
        {},
        0.25F,
        rotated_frame);
    REQUIRE(walk.current.horizontal_velocity ==
        math::Float3{});
    REQUIRE(walk.current.state.center_position ==
        walk.previous.state.center_position);
}

TEST_CASE(
    "surface-swim exits distinguish wading shore air and steep support",
    "[character][player-capsule][surface-swimming][exit][terrain]")
{
    using namespace shark;

    const auto surface = flat_surface();

    SECTION("shallow water exits to supported wading")
    {
        auto player = make_player(surface);
        enter_surface_swimming(
            player,
            surface,
            water_body(4.0F));
        advance_with_body(
            player,
            surface,
            water_body(1.25F),
            3U);
        REQUIRE(player.current.water ==
            character::PlayerWaterState{
                .phase = character::PlayerWaterPhase::wading,
                .depth = 1.25F,
            });
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::grounded);
        REQUIRE(player.current.state.center_position.y == 1.0F);
    }

    SECTION("no water near support snaps to dry ground")
    {
        auto player = make_player(surface);
        enter_surface_swimming(
            player,
            surface,
            water_body(1.5F));
        REQUIRE(player.current.state.center_position.y == 1.0F);
        advance_with_body(
            player,
            surface,
            water_body(-1.0F),
            3U);
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::grounded);
        REQUIRE(player.current.state.center_position.y == 1.0F);
    }

    SECTION("no water above distant support begins dry falling")
    {
        auto player = make_player(surface);
        enter_surface_swimming(
            player,
            surface,
            water_body(4.0F),
            0.25F);
        advance_with_body(
            player,
            surface,
            water_body(4.0F),
            3U,
            {.move_forward_held = true},
            0.25F);
        const auto swim_position =
            player.current.state.center_position;
        const auto swim_velocity =
            player.current.horizontal_velocity;
        REQUIRE(swim_position.y == 3.5F);
        REQUIRE(swim_velocity ==
            math::Float3{0.0F, 0.0F, -3.0F});

        advance_with_body(
            player,
            surface,
            water_body(-1.0F),
            4U,
            {},
            0.25F);
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::falling);
        REQUIRE(player.current.vertical.velocity_y == 0.0F);
        REQUIRE(player.current.state.center_position ==
            swim_position);
        REQUIRE(player.current.horizontal_velocity ==
            swim_velocity);
    }

    SECTION("shallow water over steep support exits dry and stopped")
    {
        auto player = make_player(surface);
        enter_surface_swimming(
            player,
            surface,
            water_body(4.0F));
        const auto steep = steep_surface();
        const auto query = query_at_player(
            water_body(1.25F),
            steep,
            player);
        REQUIRE(character::advance_player_capsule(
            player,
            {.move_forward_held = true},
            {},
            steep,
            query,
            1.0F / 60.0F,
            3U));
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::steep_contact);
        REQUIRE(player.current.vertical.support_normal.y <
            player.config.grounding.minimum_walkable_normal_y);
        REQUIRE(player.current.horizontal_velocity ==
            math::Float3{});
    }

    SECTION(
        "no water near steep support falls dry with swim momentum")
    {
        const auto steep = steep_surface();
        const auto steep_support =
            character::query_player_terrain_support(
                {},
                {},
                steep,
                0.0F,
                0.0F);
        REQUIRE(steep_support);
        REQUIRE(steep_support.value());
        REQUIRE_FALSE(steep_support.value()->walkable);

        const auto aligned_surface_height =
            steep_support.value()->center_position_y +
            character::PlayerSurfaceSwimmingSettings{}
                .surface_center_depth;
        const auto aligned_body =
            water_body(aligned_surface_height);
        auto player = make_player(surface);
        enter_surface_swimming(
            player,
            surface,
            aligned_body,
            0.25F);
        advance_with_body(
            player,
            surface,
            aligned_body,
            3U,
            {.move_forward_held = true},
            0.25F);
        REQUIRE(player.current.state.center_position.y ==
            Catch::Approx(
                steep_support.value()->center_position_y)
                .margin(0.000001F));
        const auto swim_position =
            player.current.state.center_position;
        const auto swim_velocity =
            player.current.horizontal_velocity;
        REQUIRE(swim_velocity ==
            math::Float3{0.0F, 0.0F, -3.0F});

        const auto no_water = query_at_player(
            water_body(0.0F),
            steep,
            player);
        REQUIRE(no_water.disposition ==
            water::GameplayWaterDisposition::no_water);
        REQUIRE(character::advance_player_capsule(
            player,
            {},
            {},
            steep,
            no_water,
            0.25F,
            4U));
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::falling);
        REQUIRE(player.current.vertical.velocity_y == 0.0F);
        REQUIRE(player.current.state.center_position ==
            swim_position);
        REQUIRE(player.current.horizontal_velocity ==
            swim_velocity);
    }
}

TEST_CASE(
    "surface swimming has explicit supported-jump and airborne capture rules",
    "[character][player-capsule][surface-swimming][jump][airborne]")
{
    using namespace shark;

    const auto surface = flat_surface();
    const auto deep_body = water_body(4.0F);

    SECTION("supported jump wins before surface-swim entry")
    {
        auto player = make_player(surface);
        advance_with_body(
            player,
            surface,
            deep_body,
            1U);
        REQUIRE(player.current.water.phase ==
            character::PlayerWaterPhase::wading);

        advance_with_body(
            player,
            surface,
            deep_body,
            2U,
            {.jump_pressed = true});
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::rising);
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});

        advance_with_body(
            player,
            surface,
            deep_body,
            3U);
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::rising);
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});
    }

    SECTION("jump is consumed but ignored while surface swimming")
    {
        auto player = make_player(surface);
        enter_surface_swimming(
            player,
            surface,
            deep_body);
        advance_with_body(
            player,
            surface,
            deep_body,
            3U,
            {.jump_pressed = true});
        REQUIRE(player.current.consumed_command.jump_pressed);
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::surface_swimming);
        REQUIRE(player.current.water.phase ==
            character::PlayerWaterPhase::surface_swimming);
        REQUIRE(player.current.state.center_position.y == 3.5F);
        REQUIRE(positive_zero(
            player.current.vertical.velocity_y));
    }

    SECTION("falling captures at the surface before terrain")
    {
        auto player = make_player(
            surface,
            test_config({0.0F, 5.0F, 0.0F}));
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::falling);

        advance_with_body(
            player,
            surface,
            deep_body,
            1U,
            {},
            0.25F);
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::falling);
        REQUIRE(player.current.state.center_position.y > 3.5F);
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});

        advance_with_body(
            player,
            surface,
            deep_body,
            2U,
            {},
            0.25F);
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::surface_swimming);
        REQUIRE(player.current.water ==
            character::PlayerWaterState{
                .phase =
                    character::PlayerWaterPhase::
                        surface_swimming,
                .depth = 4.0F,
                .surface_height = 4.0F,
            });
        REQUIRE(player.current.state.center_position.y == 3.5F);
        REQUIRE(player.current.state.center_position.y > 1.0F);
    }
}

TEST_CASE(
    "surface swimming resets collapses and recovers without wet history",
    "[character][player-capsule][surface-swimming][reset][recovery][interpolation]")
{
    using namespace shark;

    const auto surface = flat_surface();
    const auto deep_body = water_body(4.0F);

    SECTION("collapse retains the authoritative swim then reset wins")
    {
        auto player = make_player(surface);
        enter_surface_swimming(
            player,
            surface,
            deep_body);
        advance_with_body(
            player,
            surface,
            water_body(5.0F),
            3U);
        REQUIRE(player.previous.water.surface_height == 4.0F);
        REQUIRE(player.current.water.surface_height == 5.0F);
        REQUIRE(player.current.state.center_position.y == 4.5F);

        REQUIRE(character::collapse_player_capsule_interpolation(
            player));
        REQUIRE(player.previous.state == player.current.state);
        REQUIRE(player.previous.vertical ==
            player.current.vertical);
        REQUIRE(player.previous.water == player.current.water);

        const character::PlayerActionCommand reset{
            .jump_pressed = true,
            .reset_pressed = true,
        };
        advance_with_body(
            player,
            surface,
            water_body(5.0F),
            4U,
            reset);
        REQUIRE(player.previous.state == player.current.state);
        REQUIRE(player.current.state.center_position ==
            player.config.spawn_center_position);
        REQUIRE(player.previous.water ==
            character::PlayerWaterState{});
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::grounded);
        REQUIRE(player.current.horizontal_velocity ==
            math::Float3{});
        REQUIRE(player.current.reset_generation == 1U);
        REQUIRE(player.current.consumed_command == reset);
    }

    SECTION("missing source terrain performs collapsed recovery")
    {
        auto player = make_player(surface);
        enter_surface_swimming(
            player,
            surface,
            deep_body,
            0.25F);
        for (std::uint64_t tick = 3U;
             tick <= 7U;
             ++tick) {
            advance_with_body(
                player,
                surface,
                deep_body,
                tick,
                {.move_right_held = true},
                0.25F);
        }
        REQUIRE(player.current.state.center_position.x > 2.0F);
        REQUIRE(player.current.water.phase ==
            character::PlayerWaterPhase::surface_swimming);

        const auto local_surface =
            flat_surface(5U, -2.0F, -2.0F);
        const auto missing_query = query_at_player(
            deep_body,
            local_surface,
            player);
        REQUIRE(missing_query.disposition ==
            water::GameplayWaterDisposition::out_of_terrain);
        REQUIRE(character::advance_player_capsule(
            player,
            {},
            {},
            local_surface,
            missing_query,
            0.25F,
            8U));
        REQUIRE(player.previous.state == player.current.state);
        REQUIRE(player.current.state.center_position ==
            player.config.spawn_center_position);
        REQUIRE(player.previous.water ==
            character::PlayerWaterState{});
        REQUIRE(player.current.water ==
            character::PlayerWaterState{});
        REQUIRE(player.current.vertical.phase ==
            character::PlayerGroundPhase::grounded);
        REQUIRE(player.current.reset_generation == 1U);
    }
}

TEST_CASE(
    "surface-swim traversal keeps a safe terrain prefix and failures roll back",
    "[character][player-capsule][surface-swimming][terrain][rollback]")
{
    using namespace shark;

    SECTION("missing traversal support stops at the last safe probe")
    {
        const auto surface =
            flat_surface(5U, -2.0F, -2.0F);
        const auto body = water_body(4.0F);
        auto player = make_player(surface);
        enter_surface_swimming(
            player,
            surface,
            body,
            0.25F);

        for (std::uint64_t tick = 3U;
             tick <= 8U;
             ++tick) {
            advance_with_body(
                player,
                surface,
                body,
                tick,
                {.move_right_held = true},
                0.25F);
        }
        REQUIRE(player.current.state.center_position.x == 2.0F);
        REQUIRE(player.current.state.center_position.y == 3.5F);
        REQUIRE(player.current.horizontal_velocity ==
            math::Float3{});
        REQUIRE(player.current.water.phase ==
            character::PlayerWaterPhase::surface_swimming);
    }

    SECTION("an intrusive steep probe stops before the obstacle")
    {
        const auto flat = flat_surface();
        const auto body = water_body(4.0F);
        auto player = make_player(flat);
        enter_surface_swimming(
            player,
            flat,
            body,
            0.25F);

        const auto steep = steep_surface();
        advance_with_body(
            player,
            steep,
            body,
            3U,
            {.move_right_held = true},
            0.25F);
        REQUIRE(player.current.state.center_position.x == 0.75F);
        REQUIRE(player.current.state.center_position.y == 3.5F);
        REQUIRE(player.current.horizontal_velocity ==
            math::Float3{3.0F, 0.0F, 0.0F});

        advance_with_body(
            player,
            steep,
            body,
            4U,
            {.move_right_held = true},
            0.25F);
        REQUIRE(player.current.state.center_position.x == 0.75F);
        REQUIRE(player.current.state.center_position.y == 3.5F);
        REQUIRE(player.current.horizontal_velocity ==
            math::Float3{});
        REQUIRE(player.current.water.phase ==
            character::PlayerWaterPhase::surface_swimming);
    }

    SECTION("out-of-bounds surface target is transactional")
    {
        const auto surface = flat_surface();
        auto player = make_player(surface);
        enter_surface_swimming(
            player,
            surface,
            water_body(4.0F));
        const auto before = player;
        const auto query = query_at_player(
            water_body(25.0F),
            surface,
            player);
        const auto result = character::advance_player_capsule(
            player,
            {},
            {},
            surface,
            query,
            1.0F / 60.0F,
            3U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().category() ==
            core::ErrorCategory::simulation);
        REQUIRE(result.error().code() ==
            core::ErrorCode::unavailable);
        REQUIRE(player == before);
    }
}
