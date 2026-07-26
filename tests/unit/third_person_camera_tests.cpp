#include <shark/world/third_person_camera.hpp>

#include <shark/core/error.hpp>
#include <shark/core/math.hpp>
#include <shark/terrain/height_tile.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

constexpr float comparison_margin = 0.00001F;

[[nodiscard]] shark::world::ThirdPersonCameraRig make_rig(
    shark::world::ThirdPersonCameraConfig config = {})
{
    auto result =
        shark::world::create_third_person_camera_rig(config);
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::terrain::HeightTileSurface make_surface(
    shark::terrain::HeightTile tile)
{
    auto result =
        shark::terrain::HeightTileSurface::create(std::move(tile));
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] shark::terrain::HeightTile make_flat_tile()
{
    return {
        .sample_columns = 7U,
        .sample_rows = 7U,
        .sample_spacing = 2.0F,
        .origin = {-6.0F, 0.0F, -6.0F},
        .height_offsets = std::vector<float>(49U, 0.0F),
    };
}

[[nodiscard]] shark::terrain::HeightTile make_ramp_tile()
{
    std::vector<float> heights;
    heights.reserve(49U);
    for (std::uint32_t z = 0U; z < 7U; ++z) {
        for (std::uint32_t x = 0U; x < 7U; ++x) {
            static_cast<void>(x);
            heights.push_back(static_cast<float>(z) * 1.5F);
        }
    }
    return {
        .sample_columns = 7U,
        .sample_rows = 7U,
        .sample_spacing = 2.0F,
        .origin = {-6.0F, 0.0F, -6.0F},
        .height_offsets = std::move(heights),
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

void require_invalid_config(
    const shark::world::ThirdPersonCameraConfig config)
{
    const auto result =
        shark::world::create_third_person_camera_rig(config);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().category() ==
        shark::core::ErrorCategory::simulation);
    REQUIRE(result.error().code() ==
        shark::core::ErrorCode::invalid_argument);
}

[[nodiscard]] shark::world::ThirdPersonOrbitDelta
scripted_delta(const std::uint64_t fixed_tick) noexcept
{
    return {
        .yaw_radians =
            fixed_tick % 5U == 0U ? 0.125F : -0.03125F,
        .pitch_radians =
            fixed_tick % 11U == 0U ? 0.0625F : -0.0078125F,
        .boom_distance =
            fixed_tick % 13U == 0U ? -0.25F : 0.03125F,
    };
}

struct PartitionRun final {
    shark::world::ThirdPersonCameraRig rig;
    std::vector<shark::world::ThirdPersonOrbitSnapshot> trace;

    [[nodiscard]] friend bool operator==(
        const PartitionRun&,
        const PartitionRun&) = default;
};

[[nodiscard]] PartitionRun run_render_partition(
    const std::uint32_t render_rate_hz)
{
    auto rig = make_rig();
    std::vector<shark::world::ThirdPersonOrbitSnapshot> trace;
    trace.reserve(120U);

    std::uint64_t emitted_ticks = 0U;
    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) * 2U;
    for (std::uint64_t frame = 1U;
         frame <= frame_count;
         ++frame) {
        const auto target_ticks =
            frame * 60U / render_rate_hz;
        while (emitted_ticks < target_ticks) {
            ++emitted_ticks;
            REQUIRE(shark::world::advance_third_person_camera_rig(
                rig,
                scripted_delta(emitted_ticks),
                emitted_ticks));
            trace.push_back(rig.current);
        }
    }

    REQUIRE(emitted_ticks == 120U);
    REQUIRE(trace.size() == 120U);
    return {
        .rig = rig,
        .trace = std::move(trace),
    };
}

[[nodiscard]] shark::world::ThirdPersonOrbitState obstructed_orbit()
{
    return {
        .yaw_radians = 0.0F,
        .pitch_radians = 0.25F,
        .boom_distance = 9.0F,
    };
}

void require_safe_applied_boom(
    const shark::world::ThirdPersonCameraPlacement& placement,
    const shark::world::ThirdPersonCameraConfig& config,
    const shark::terrain::HeightTileSurface& surface)
{
    REQUIRE(placement.terrain_obstructed);
    REQUIRE(placement.applied_boom_distance >= 0.0F);
    REQUIRE(placement.applied_boom_distance <
        placement.desired_boom_distance);

    const auto safe_result =
        surface.closest_lod0_point_to_segment(
            shark::terrain::Segment3{
                .first_endpoint = placement.target_position,
                .second_endpoint =
                    placement.camera.transform.position,
            },
            config.obstruction_clearance);
    REQUIRE(safe_result);
    REQUIRE_FALSE(safe_result.value());
}

} // namespace

TEST_CASE(
    "third-person camera creation publishes a canonical bounded orbit",
    "[world][third-person-camera][create]")
{
    using namespace shark;

    world::ThirdPersonCameraConfig config;
    config.initial_yaw_radians = 5.0F * math::pi;
    config.target_height_offset = -0.0F;
    const auto first =
        world::create_third_person_camera_rig(config);
    const auto second =
        world::create_third_person_camera_rig(config);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first.value() == second.value());
    const auto& rig = first.value();

    REQUIRE(world::is_valid(rig));
    REQUIRE(rig.previous == rig.current);
    REQUIRE(rig.current.fixed_tick == 0U);
    REQUIRE(rig.current.consumed_delta ==
        world::ThirdPersonOrbitDelta{});
    REQUIRE(rig.current.state.yaw_radians ==
        Catch::Approx(-math::pi).margin(0.000001F));
    REQUIRE(rig.current.state.pitch_radians ==
        world::default_third_person_initial_pitch);
    REQUIRE(rig.current.state.boom_distance ==
        world::default_third_person_initial_boom_distance);
    REQUIRE(rig.config.target_height_offset == 0.0F);
    REQUIRE_FALSE(std::signbit(
        rig.config.target_height_offset));
}

TEST_CASE(
    "third-person camera configuration rejects malformed bounds",
    "[world][third-person-camera][validation]")
{
    using namespace shark;

    constexpr auto nan =
        std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity =
        std::numeric_limits<float>::infinity();
    world::ThirdPersonCameraConfig config;

    SECTION("target height")
    {
        config.target_height_offset = -0.001F;
        require_invalid_config(config);
        config.target_height_offset =
            std::nextafter(
                world::maximum_third_person_target_height_offset,
                infinity);
        require_invalid_config(config);
        config.target_height_offset = nan;
        require_invalid_config(config);
    }
    SECTION("pitch")
    {
        config.minimum_pitch_radians =
            -world::maximum_camera_pitch - 0.001F;
        require_invalid_config(config);
        config = {};
        config.maximum_pitch_radians =
            world::maximum_camera_pitch + 0.001F;
        require_invalid_config(config);
        config = {};
        config.minimum_pitch_radians = 0.1F;
        config.maximum_pitch_radians = -0.1F;
        require_invalid_config(config);
        config = {};
        config.initial_pitch_radians =
            config.maximum_pitch_radians + 0.001F;
        require_invalid_config(config);
        config = {};
        config.initial_yaw_radians = infinity;
        require_invalid_config(config);
    }
    SECTION("boom")
    {
        config.minimum_boom_distance = 0.0F;
        require_invalid_config(config);
        config = {};
        config.maximum_boom_distance =
            std::nextafter(
                world::maximum_third_person_boom_distance,
                infinity);
        require_invalid_config(config);
        config = {};
        config.minimum_boom_distance = 10.0F;
        config.maximum_boom_distance = 9.0F;
        require_invalid_config(config);
        config = {};
        config.initial_boom_distance = 1.0F;
        require_invalid_config(config);
    }
    SECTION("clearance")
    {
        config.obstruction_clearance = 0.0F;
        require_invalid_config(config);
        config.obstruction_clearance = nan;
        require_invalid_config(config);
        config.obstruction_clearance =
            std::nextafter(
                world::maximum_third_person_obstruction_clearance,
                infinity);
        require_invalid_config(config);
    }
}

TEST_CASE(
    "third-person fixed-tick advance wraps clamps and rolls back",
    "[world][third-person-camera][advance][rollback]")
{
    using namespace shark;

    world::ThirdPersonCameraConfig config;
    config.initial_yaw_radians = math::pi - 0.1F;
    auto rig = make_rig(config);
    REQUIRE(world::advance_third_person_camera_rig(
        rig,
        world::ThirdPersonOrbitDelta{
            .yaw_radians = 0.2F,
            .pitch_radians = 100.0F,
            .boom_distance = -100.0F,
        },
        1U));
    REQUIRE(world::is_valid(rig));
    REQUIRE(rig.previous.fixed_tick == 0U);
    REQUIRE(rig.current.fixed_tick == 1U);
    REQUIRE(rig.current.state.yaw_radians ==
        Catch::Approx(-math::pi + 0.1F).margin(0.000001F));
    REQUIRE(rig.current.state.pitch_radians ==
        config.maximum_pitch_radians);
    REQUIRE(rig.current.state.boom_distance ==
        config.minimum_boom_distance);

    const auto before_wrong_tick = rig;
    const auto wrong_tick =
        world::advance_third_person_camera_rig(
            rig,
            {},
            3U);
    REQUIRE_FALSE(wrong_tick);
    REQUIRE(rig == before_wrong_tick);

    auto invalid_delta = world::ThirdPersonOrbitDelta{};
    invalid_delta.pitch_radians =
        std::numeric_limits<float>::quiet_NaN();
    const auto before_invalid_delta = rig;
    const auto invalid_delta_result =
        world::advance_third_person_camera_rig(
            rig,
            invalid_delta,
            2U);
    REQUIRE_FALSE(invalid_delta_result);
    REQUIRE(rig == before_invalid_delta);

    auto invalid_source = rig;
    invalid_source.current.state.boom_distance = -1.0F;
    const auto invalid_source_before = invalid_source;
    REQUIRE_FALSE(world::advance_third_person_camera_rig(
        invalid_source,
        {},
        2U));
    REQUIRE(invalid_source == invalid_source_before);

    auto overflow = rig;
    overflow.previous.fixed_tick =
        std::numeric_limits<std::uint64_t>::max() - 1U;
    overflow.current.fixed_tick =
        std::numeric_limits<std::uint64_t>::max();
    REQUIRE(world::is_valid(overflow));
    const auto overflow_before = overflow;
    const auto overflow_result =
        world::advance_third_person_camera_rig(
            overflow,
            {},
            0U);
    REQUIRE_FALSE(overflow_result);
    REQUIRE(overflow_result.error().code() ==
        core::ErrorCode::unavailable);
    REQUIRE(overflow == overflow_before);

    SECTION("float rounding at positive pi remains canonical")
    {
        world::ThirdPersonCameraConfig seam_config;
        seam_config.initial_yaw_radians =
            1.3000000365e-7F;
        auto seam_rig = make_rig(seam_config);
        REQUIRE(world::advance_third_person_camera_rig(
            seam_rig,
            world::ThirdPersonOrbitDelta{
                .yaw_radians = std::nextafter(
                    math::pi,
                    -std::numeric_limits<float>::infinity()),
            },
            1U));
        REQUIRE(world::is_valid(seam_rig));
        REQUIRE(seam_rig.current.state.yaw_radians ==
            -math::pi);
    }
}

TEST_CASE(
    "third-person interpolation follows the shortest yaw arc",
    "[world][third-person-camera][interpolation]")
{
    using namespace shark;

    world::ThirdPersonCameraConfig config;
    config.initial_yaw_radians = math::pi - 0.1F;
    auto rig = make_rig(config);
    REQUIRE(world::advance_third_person_camera_rig(
        rig,
        world::ThirdPersonOrbitDelta{
            .yaw_radians = 0.2F,
            .pitch_radians = 0.2F,
            .boom_distance = 2.0F,
        },
        1U));

    const auto at_previous =
        world::interpolate_third_person_camera_rig(rig, 0.0F);
    const auto halfway =
        world::interpolate_third_person_camera_rig(rig, 0.5F);
    const auto at_current =
        world::interpolate_third_person_camera_rig(rig, 1.0F);
    REQUIRE(at_previous);
    REQUIRE(halfway);
    REQUIRE(at_current);
    REQUIRE(at_previous.value() == rig.previous.state);
    REQUIRE(at_current.value() == rig.current.state);
    REQUIRE(halfway.value().yaw_radians ==
        Catch::Approx(-math::pi).margin(0.000001F));
    REQUIRE(halfway.value().pitch_radians ==
        Catch::Approx(-0.15F).margin(0.000001F));
    REQUIRE(halfway.value().boom_distance ==
        Catch::Approx(10.0F).margin(0.000001F));

    REQUIRE_FALSE(world::interpolate_third_person_camera_rig(
        rig,
        -0.001F));
    REQUIRE_FALSE(world::interpolate_third_person_camera_rig(
        rig,
        std::numeric_limits<float>::quiet_NaN()));
}

TEST_CASE(
    "third-person interpolation collapse preserves authoritative metadata",
    "[world][third-person-camera][interpolation][collapse]")
{
    using namespace shark;

    auto rig = make_rig();
    REQUIRE(world::advance_third_person_camera_rig(
        rig,
        world::ThirdPersonOrbitDelta{
            .yaw_radians = 0.5F,
            .pitch_radians = 0.125F,
            .boom_distance = -1.0F,
        },
        1U));
    REQUIRE(world::advance_third_person_camera_rig(
        rig,
        world::ThirdPersonOrbitDelta{
            .yaw_radians = -0.25F,
            .pitch_radians = -0.0625F,
            .boom_distance = 0.5F,
        },
        2U));
    REQUIRE(rig.previous.state != rig.current.state);

    auto expected = rig;
    expected.previous.state = expected.current.state;
    REQUIRE(world::collapse_third_person_camera_interpolation(rig));
    REQUIRE(rig == expected);
    REQUIRE(world::is_valid(rig));

    constexpr std::array interpolation_alphas{
        0.0F,
        0.125F,
        0.5F,
        0.875F,
        1.0F,
    };
    for (const auto alpha : interpolation_alphas) {
        const auto interpolated =
            world::interpolate_third_person_camera_rig(
                rig,
                alpha);
        REQUIRE(interpolated);
        REQUIRE(interpolated.value() == rig.current.state);
    }

    auto invalid = rig;
    invalid.previous.state.boom_distance = -1.0F;
    const auto invalid_before = invalid;
    const auto invalid_result =
        world::collapse_third_person_camera_interpolation(
            invalid);
    REQUIRE_FALSE(invalid_result);
    REQUIRE(invalid_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(invalid == invalid_before);
}

TEST_CASE(
    "horizontal camera basis uses yaw and Shark world conventions",
    "[world][third-person-camera][basis]")
{
    using namespace shark;

    const auto zero =
        world::horizontal_camera_basis(0.0F);
    REQUIRE(zero);
    require_float3(zero.value().right, {1.0F, 0.0F, 0.0F});
    require_float3(zero.value().forward, {0.0F, 0.0F, -1.0F});

    const auto quarter_turn =
        world::horizontal_camera_basis(math::half_pi);
    REQUIRE(quarter_turn);
    require_float3(
        quarter_turn.value().right,
        {0.0F, 0.0F, 1.0F});
    require_float3(
        quarter_turn.value().forward,
        {1.0F, 0.0F, 0.0F});
    REQUIRE_FALSE(world::horizontal_camera_basis(
        std::numeric_limits<float>::infinity()));
}

TEST_CASE(
    "third-person camera preserves its lens above unobstructed terrain",
    "[world][third-person-camera][placement][unobstructed]")
{
    using namespace shark;

    const world::ThirdPersonCameraConfig config;
    const world::ThirdPersonOrbitState orbit{
        .yaw_radians = 0.0F,
        .pitch_radians = -0.25F,
        .boom_distance = 9.0F,
    };
    const world::PerspectiveLens lens{
        .vertical_fov_radians = 0.9F,
        .near_plane = 0.05F,
        .far_plane = 750.0F,
    };
    const auto surface = make_surface(make_flat_tile());
    const auto result = world::build_third_person_camera(
        config,
        orbit,
        {0.0F, 1.0F, 0.0F},
        lens,
        surface);
    REQUIRE(result);
    const auto& placement = result.value();

    REQUIRE_FALSE(placement.terrain_obstructed);
    REQUIRE(placement.desired_boom_distance == 9.0F);
    REQUIRE(placement.applied_boom_distance == 9.0F);
    REQUIRE(placement.camera.lens.vertical_fov_radians ==
        lens.vertical_fov_radians);
    REQUIRE(placement.camera.lens.near_plane ==
        lens.near_plane);
    REQUIRE(placement.camera.lens.far_plane ==
        lens.far_plane);
    REQUIRE(placement.camera.transform.yaw_radians ==
        orbit.yaw_radians);
    REQUIRE(placement.camera.transform.pitch_radians ==
        orbit.pitch_radians);
    require_float3(
        placement.target_position,
        {0.0F, 1.75F, 0.0F});
    REQUIRE(placement.camera.transform.position.y >
        placement.target_position.y);

    const auto direction = world::camera_basis(
        placement.camera.transform);
    const math::Float3 reconstructed_target{
        placement.camera.transform.position.x +
            direction.forward.x *
                placement.applied_boom_distance,
        placement.camera.transform.position.y +
            direction.forward.y *
                placement.applied_boom_distance,
        placement.camera.transform.position.z +
            direction.forward.z *
                placement.applied_boom_distance,
    };
    require_float3(
        reconstructed_target,
        placement.target_position,
        0.00002F);
}

TEST_CASE(
    "canonical LOD0 terrain conservatively shortens obstructed booms",
    "[world][third-person-camera][placement][obstruction][terrain]")
{
    using namespace shark;

    const world::ThirdPersonCameraConfig config;
    const world::PerspectiveLens lens;

    SECTION("flat")
    {
        const auto surface = make_surface(make_flat_tile());
        const auto first = world::build_third_person_camera(
            config,
            obstructed_orbit(),
            {0.0F, 0.0F, 0.0F},
            lens,
            surface);
        const auto second = world::build_third_person_camera(
            config,
            obstructed_orbit(),
            {0.0F, 0.0F, 0.0F},
            lens,
            surface);
        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(first.value() == second.value());
        require_safe_applied_boom(
            first.value(),
            config,
            surface);
    }

    SECTION("ramp")
    {
        const auto surface = make_surface(make_ramp_tile());
        const world::ThirdPersonOrbitState orbit{
            .yaw_radians = 0.0F,
            .pitch_radians = -0.25F,
            .boom_distance = 9.0F,
        };
        const auto result = world::build_third_person_camera(
            config,
            orbit,
            {0.0F, 1.0F, -5.0F},
            lens,
            surface);
        REQUIRE(result);
        require_safe_applied_boom(
            result.value(),
            config,
            surface);

        const auto unobstructed =
            world::build_third_person_camera(
                config,
                orbit,
                {0.0F, 12.0F, -5.0F},
                lens,
                surface);
        REQUIRE(unobstructed);
        REQUIRE_FALSE(unobstructed.value().terrain_obstructed);
        REQUIRE(unobstructed.value().applied_boom_distance ==
            orbit.boom_distance);
    }

    SECTION("project-owned deterministic canonical tile")
    {
        const auto surface = make_surface(
            terrain::make_deterministic_height_tile());
        const auto ground =
            surface.sample_lod0_height(0.0F, -8.0F);
        REQUIRE(ground);
        const math::Float3 target{
            0.0F,
            ground.value(),
            -8.0F,
        };
        const auto obstructed =
            world::build_third_person_camera(
                config,
                obstructed_orbit(),
                target,
                lens,
                surface);
        REQUIRE(obstructed);
        require_safe_applied_boom(
            obstructed.value(),
            config,
            surface);

        const world::ThirdPersonOrbitState raised{
            .yaw_radians = 0.0F,
            .pitch_radians = -0.5F,
            .boom_distance = 6.0F,
        };
        const auto unobstructed =
            world::build_third_person_camera(
                config,
                raised,
                target,
                lens,
                surface);
        REQUIRE(unobstructed);
        REQUIRE_FALSE(unobstructed.value().terrain_obstructed);
        REQUIRE(unobstructed.value().applied_boom_distance ==
            raised.boom_distance);
    }
}

TEST_CASE(
    "third-person camera rejects invalid construction inputs",
    "[world][third-person-camera][placement][validation]")
{
    using namespace shark;

    const auto surface = make_surface(make_flat_tile());
    const world::ThirdPersonCameraConfig config;
    const world::ThirdPersonOrbitState orbit;
    const world::PerspectiveLens lens;
    constexpr auto nan =
        std::numeric_limits<float>::quiet_NaN();

    REQUIRE_FALSE(world::build_third_person_camera(
        config,
        orbit,
        {nan, 0.0F, 0.0F},
        lens,
        surface));

    auto invalid_orbit = orbit;
    invalid_orbit.boom_distance =
        config.maximum_boom_distance + 1.0F;
    REQUIRE_FALSE(world::build_third_person_camera(
        config,
        invalid_orbit,
        {},
        lens,
        surface));

    auto invalid_lens = lens;
    invalid_lens.near_plane = 0.0F;
    REQUIRE_FALSE(world::build_third_person_camera(
        config,
        orbit,
        {},
        invalid_lens,
        surface));

    auto obstructed_target_config = config;
    obstructed_target_config.target_height_offset = 0.0F;
    const auto obstructed_target =
        world::build_third_person_camera(
            obstructed_target_config,
            orbit,
            {0.0F, 0.0F, 0.0F},
            lens,
            surface);
    REQUIRE_FALSE(obstructed_target);
    REQUIRE(obstructed_target.error().code() ==
        core::ErrorCode::unavailable);
}

TEST_CASE(
    "third-person fixed ticks are invariant across render partitions",
    "[world][third-person-camera][fixed-tick][invariance]")
{
    const auto baseline = run_render_partition(60U);
    constexpr std::array render_rates{
        30U,
        120U,
        144U,
    };
    for (const auto render_rate : render_rates) {
        const auto candidate =
            run_render_partition(render_rate);
        REQUIRE(candidate == baseline);
    }
}
