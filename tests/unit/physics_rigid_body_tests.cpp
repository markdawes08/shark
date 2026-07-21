#include <shark/physics/rigid_body.hpp>
#include <shark/simulation/fixed_step_clock.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

constexpr float comparison_margin = 0.00001F;

void require_float3(
    const shark::math::Float3 actual,
    const shark::math::Float3 expected,
    const float margin = comparison_margin)
{
    REQUIRE(actual.x == Catch::Approx(expected.x).margin(margin));
    REQUIRE(actual.y == Catch::Approx(expected.y).margin(margin));
    REQUIRE(actual.z == Catch::Approx(expected.z).margin(margin));
}

void require_quaternion(
    const shark::math::Quaternion actual,
    const shark::math::Quaternion expected,
    const float margin = comparison_margin)
{
    REQUIRE(actual.x == Catch::Approx(expected.x).margin(margin));
    REQUIRE(actual.y == Catch::Approx(expected.y).margin(margin));
    REQUIRE(actual.z == Catch::Approx(expected.z).margin(margin));
    REQUIRE(actual.w == Catch::Approx(expected.w).margin(margin));
}

[[nodiscard]] shark::physics::SolidSphereMassProperties
make_test_mass_properties()
{
    auto result =
        shark::physics::make_solid_sphere_mass_properties(
            2.0F,
            1.5F);
    REQUIRE(result);
    return std::move(result).value();
}

struct AngularRun final {
    shark::physics::RigidBodyState state;
    std::uint64_t step_count{};
};

[[nodiscard]] AngularRun run_angular_schedule(
    const std::uint32_t render_rate_hz,
    const std::uint32_t duration_seconds)
{
    using namespace shark;

    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();
    auto state = physics::RigidBodyState{
        .position = {3.0F, 4.0F, -2.0F},
        .orientation = {},
        .linear_velocity = {1.0F, 2.0F, 3.0F},
        .angular_velocity = {0.25F, -0.5F, 0.75F},
    };
    const auto properties = make_test_mass_properties();
    constexpr math::Float3 torque{0.9F, -0.45F, 0.18F};
    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) *
        duration_seconds;
    auto previous_timestamp = std::chrono::nanoseconds{0};

    for (std::uint64_t frame = 1;
         frame <= frame_count;
         ++frame) {
        const auto timestamp = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                frame * 1'000'000'000ULL / render_rate_hz)};
        const auto frame_result =
            clock.advance(timestamp - previous_timestamp);
        REQUIRE(frame_result);
        previous_timestamp = timestamp;
        for (std::uint32_t step = 0;
             step < frame_result.value().step_count;
             ++step) {
            REQUIRE(physics::advance_rigid_body_angular_motion(
                state,
                properties,
                torque,
                clock.fixed_delta_seconds()));
        }
    }

    return AngularRun{
        .state = state,
        .step_count = clock.total_step_count(),
    };
}

} // namespace

TEST_CASE(
    "rigid body identity and solid sphere inertia are explicit",
    "[physics][rigid-body][mass][identity]")
{
    using namespace shark;

    const physics::RigidBodyState identity{};
    REQUIRE(physics::is_valid(identity));
    REQUIRE(identity.orientation == math::Quaternion{});
    REQUIRE(math::is_finite(identity.orientation));
    REQUIRE(math::is_unit(identity.orientation));

    const auto result =
        physics::make_solid_sphere_mass_properties(
            2.0F,
            3.0F);
    REQUIRE(result);
    const auto& properties = result.value();
    REQUIRE(physics::is_valid(properties));
    REQUIRE(properties.mass == 2.0F);
    REQUIRE(properties.inverse_mass == 0.5F);
    REQUIRE(properties.radius == 3.0F);
    REQUIRE(
        properties.moment_of_inertia ==
        Catch::Approx(7.2F).margin(0.000001F));
    REQUIRE(
        properties.inverse_moment_of_inertia ==
        Catch::Approx(1.0F / 7.2F).margin(0.000001F));
}

TEST_CASE(
    "zero torque preserves a stationary angular state",
    "[physics][rigid-body][angular][zero-torque]")
{
    using namespace shark;

    physics::RigidBodyState state{
        .position = {3.0F, 4.0F, -2.0F},
        .orientation = {},
        .linear_velocity = {1.0F, 2.0F, 3.0F},
        .angular_velocity = {},
    };
    const auto before = state;
    const auto result =
        physics::advance_rigid_body_angular_motion(
            state,
            make_test_mass_properties(),
            {},
            1.0F / 60.0F);

    REQUIRE(result);
    REQUIRE(state == before);
}

TEST_CASE(
    "angular integration normalizes orientation deterministically",
    "[physics][rigid-body][angular][normalization]")
{
    using namespace shark;

    physics::RigidBodyState state{
        .orientation = {0.0F, 0.0F, 0.0F, 1.000001F},
    };
    REQUIRE(physics::is_valid(state));
    REQUIRE(math::is_unit(state.orientation));
    REQUIRE(state.orientation != math::Quaternion{});

    const auto result =
        physics::advance_rigid_body_angular_motion(
            state,
            make_test_mass_properties(),
            {},
            1.0F / 60.0F);

    REQUIRE(result);
    REQUIRE(math::is_unit(state.orientation, 0.000001F));
    require_quaternion(state.orientation, math::Quaternion{}, 0.000001F);
}

TEST_CASE(
    "constant angular velocity applies exact axis-angle increments",
    "[physics][rigid-body][angular][constant-velocity]")
{
    using namespace shark;

    physics::RigidBodyState state{
        .position = {4.0F, 5.0F, 6.0F},
        .orientation = {},
        .linear_velocity = {-1.0F, 2.0F, -3.0F},
        .angular_velocity = {0.0F, math::pi, 0.0F},
    };
    const auto linear_state_before = state;
    const auto properties = make_test_mass_properties();

    REQUIRE(physics::advance_rigid_body_angular_motion(
        state,
        properties,
        {},
        0.25F));
    require_float3(state.angular_velocity, {0.0F, math::pi, 0.0F});
    require_quaternion(
        state.orientation,
        {
            0.0F,
            std::sin(math::pi / 8.0F),
            0.0F,
            std::cos(math::pi / 8.0F),
        },
        0.000001F);
    REQUIRE(state.position == linear_state_before.position);
    REQUIRE(
        state.linear_velocity ==
        linear_state_before.linear_velocity);

    REQUIRE(physics::advance_rigid_body_angular_motion(
        state,
        properties,
        {},
        0.25F));
    require_quaternion(
        state.orientation,
        {
            0.0F,
            std::sin(math::pi / 4.0F),
            0.0F,
            std::cos(math::pi / 4.0F),
        },
        0.000001F);
    REQUIRE(math::is_unit(state.orientation, 0.000001F));
}

TEST_CASE(
    "solid sphere torque has the analytic semi-implicit response",
    "[physics][rigid-body][angular][torque][inertia]")
{
    using namespace shark;

    auto properties_result =
        physics::make_solid_sphere_mass_properties(
            2.0F,
            0.5F);
    REQUIRE(properties_result);
    const auto properties =
        std::move(properties_result).value();
    REQUIRE(
        properties.moment_of_inertia ==
        Catch::Approx(0.2F).margin(0.000001F));
    REQUIRE(
        properties.inverse_moment_of_inertia ==
        Catch::Approx(5.0F).margin(0.000001F));

    physics::RigidBodyState state{};
    constexpr math::Float3 torque{0.0F, 0.0F, 0.6F};
    REQUIRE(physics::advance_rigid_body_angular_motion(
        state,
        properties,
        torque,
        0.25F));
    require_float3(state.angular_velocity, {0.0F, 0.0F, 0.75F});
    require_quaternion(
        state.orientation,
        {0.0F, 0.0F, std::sin(0.09375F), std::cos(0.09375F)},
        0.000001F);

    REQUIRE(physics::advance_rigid_body_angular_motion(
        state,
        properties,
        torque,
        0.25F));
    require_float3(state.angular_velocity, {0.0F, 0.0F, 1.5F});
    require_quaternion(
        state.orientation,
        {0.0F, 0.0F, std::sin(0.28125F), std::cos(0.28125F)},
        0.000001F);
}

TEST_CASE(
    "angular integration rejects invalid input transactionally",
    "[physics][rigid-body][angular][validation]")
{
    using namespace shark;

    const physics::RigidBodyState baseline{
        .position = {3.0F, 4.0F, -2.0F},
        .orientation = {},
        .linear_velocity = {1.0F, 2.0F, 3.0F},
        .angular_velocity = {0.25F, -0.5F, 0.75F},
    };
    const auto properties = make_test_mass_properties();
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity = std::numeric_limits<float>::infinity();

    SECTION("mass factory rejects nonpositive and nonfinite inputs")
    {
        const std::array invalid_inputs{
            std::pair{0.0F, 1.0F},
            std::pair{-1.0F, 1.0F},
            std::pair{1.0F, 0.0F},
            std::pair{1.0F, -1.0F},
            std::pair{nan, 1.0F},
            std::pair{1.0F, nan},
            std::pair{infinity, 1.0F},
            std::pair{1.0F, infinity},
        };
        for (const auto& [mass, radius] : invalid_inputs) {
            CAPTURE(mass, radius);
            const auto result =
                physics::make_solid_sphere_mass_properties(
                    mass,
                    radius);
            REQUIRE_FALSE(result);
            REQUIRE(
                result.error().code() ==
                core::ErrorCode::invalid_argument);
        }
    }

    SECTION("invalid delta or torque leaves state unchanged")
    {
        const std::array invalid_inputs{
            std::pair{math::Float3{}, 0.0F},
            std::pair{math::Float3{}, -1.0F / 60.0F},
            std::pair{math::Float3{}, 0.250001F},
            std::pair{math::Float3{}, nan},
            std::pair{math::Float3{}, infinity},
            std::pair{math::Float3{nan, 0.0F, 0.0F}, 1.0F / 60.0F},
            std::pair{math::Float3{0.0F, infinity, 0.0F}, 1.0F / 60.0F},
        };
        for (const auto& [torque, delta] : invalid_inputs) {
            CAPTURE(torque.x, torque.y, torque.z, delta);
            auto state = baseline;
            const auto result =
                physics::advance_rigid_body_angular_motion(
                    state,
                    properties,
                    torque,
                    delta);
            REQUIRE_FALSE(result);
            REQUIRE(
                result.error().code() ==
                core::ErrorCode::invalid_argument);
            REQUIRE(state == baseline);
        }
    }

    SECTION("invalid state or properties leave state unchanged")
    {
        auto invalid_state = baseline;
        invalid_state.orientation = {0.0F, 0.0F, 0.0F, 2.0F};
        const auto invalid_state_before = invalid_state;
        const auto state_result =
            physics::advance_rigid_body_angular_motion(
                invalid_state,
                properties,
                {},
                1.0F / 60.0F);
        REQUIRE_FALSE(state_result);
        REQUIRE(
            state_result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(invalid_state == invalid_state_before);

        auto invalid_properties = properties;
        invalid_properties.inverse_moment_of_inertia = 0.0F;
        auto state = baseline;
        const auto properties_result =
            physics::advance_rigid_body_angular_motion(
                state,
                invalid_properties,
                {},
                1.0F / 60.0F);
        REQUIRE_FALSE(properties_result);
        REQUIRE(
            properties_result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(state == baseline);

        invalid_properties = properties;
        invalid_properties.inverse_mass *= 2.0F;
        state = baseline;
        const auto inconsistent_properties_result =
            physics::advance_rigid_body_angular_motion(
                state,
                invalid_properties,
                {},
                1.0F / 60.0F);
        REQUIRE_FALSE(inconsistent_properties_result);
        REQUIRE(
            inconsistent_properties_result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(state == baseline);
    }

    SECTION("finite arithmetic overflow leaves state unchanged")
    {
        auto overflow_properties_result =
            physics::make_solid_sphere_mass_properties(
                1.0F,
                0.25F);
        REQUIRE(overflow_properties_result);
        const auto overflow_properties =
            std::move(overflow_properties_result).value();
        auto state = baseline;
        const auto before = state;
        const auto result =
            physics::advance_rigid_body_angular_motion(
                state,
                overflow_properties,
                {std::numeric_limits<float>::max(), 0.0F, 0.0F},
                0.25F);
        REQUIRE_FALSE(result);
        REQUIRE(
            result.error().code() ==
            core::ErrorCode::unavailable);
        REQUIRE(state == before);
    }
}

TEST_CASE(
    "rigid body interpolation takes the quaternion shortest path",
    "[physics][rigid-body][interpolation][shortest-path]")
{
    using namespace shark;

    const auto sine = std::sin(85.0F * math::pi / 180.0F);
    const auto cosine = std::cos(85.0F * math::pi / 180.0F);
    const physics::RigidBodyState previous{
        .position = {0.0F, 2.0F, 4.0F},
        .orientation = {0.0F, sine, 0.0F, cosine},
        .linear_velocity = {2.0F, 4.0F, 6.0F},
        .angular_velocity = {-2.0F, -4.0F, -6.0F},
    };
    const physics::RigidBodyState current{
        .position = {8.0F, 6.0F, 2.0F},
        .orientation = {0.0F, -sine, 0.0F, cosine},
        .linear_velocity = {6.0F, 2.0F, -2.0F},
        .angular_velocity = {2.0F, 4.0F, 6.0F},
    };
    const auto previous_before = previous;
    const auto current_before = current;

    const auto result =
        physics::interpolate_rigid_body(
            previous,
            current,
            0.5F);
    REQUIRE(result);
    const auto& interpolated = result.value();
    require_float3(interpolated.position, {4.0F, 4.0F, 3.0F});
    require_float3(
        interpolated.linear_velocity,
        {4.0F, 3.0F, 2.0F});
    require_float3(interpolated.angular_velocity, {});
    require_quaternion(
        interpolated.orientation,
        {0.0F, 1.0F, 0.0F, 0.0F},
        0.000001F);
    REQUIRE(math::is_unit(interpolated.orientation, 0.000001F));
    REQUIRE(previous == previous_before);
    REQUIRE(current == current_before);
}

TEST_CASE(
    "rigid body interpolation rejects malformed snapshots",
    "[physics][rigid-body][interpolation][validation]")
{
    using namespace shark;

    const physics::RigidBodyState previous{};
    const physics::RigidBodyState current{
        .position = {4.0F, -2.0F, 8.0F},
        .orientation = {},
        .linear_velocity = {8.0F, 4.0F, -4.0F},
        .angular_velocity = {2.0F, 1.0F, -1.0F},
    };
    const auto previous_before = previous;
    const auto current_before = current;
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();

    for (const auto alpha : {-0.001F, 1.001F, nan}) {
        CAPTURE(alpha);
        const auto result =
            physics::interpolate_rigid_body(
                previous,
                current,
                alpha);
        REQUIRE_FALSE(result);
        REQUIRE(
            result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(previous == previous_before);
        REQUIRE(current == current_before);
    }

    auto invalid_previous = previous;
    invalid_previous.angular_velocity.x =
        std::numeric_limits<float>::infinity();
    const auto invalid_previous_before = invalid_previous;
    const auto invalid_result =
        physics::interpolate_rigid_body(
            invalid_previous,
            current,
            0.5F);
    REQUIRE_FALSE(invalid_result);
    REQUIRE(
        invalid_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(invalid_previous == invalid_previous_before);
    REQUIRE(current == current_before);
}

TEST_CASE(
    "angular motion is invariant across render partitions",
    "[physics][rigid-body][angular][fixed-step][invariance]")
{
    using namespace shark;

    constexpr std::array<std::uint32_t, 4> render_rates{
        30,
        60,
        120,
        144,
    };
    const auto baseline = run_angular_schedule(render_rates[0], 2);
    REQUIRE(baseline.step_count == 120);
    REQUIRE(math::is_unit(baseline.state.orientation));
    REQUIRE((
        baseline.state.position ==
        math::Float3{3.0F, 4.0F, -2.0F}));
    REQUIRE(
        baseline.state.linear_velocity ==
        (math::Float3{1.0F, 2.0F, 3.0F}));
    require_float3(
        baseline.state.angular_velocity,
        {1.25F, -1.0F, 0.95F},
        0.0001F);

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        const auto run = run_angular_schedule(render_rate, 2);
        REQUIRE(run.step_count == baseline.step_count);
        REQUIRE(run.state == baseline.state);
    }
}
