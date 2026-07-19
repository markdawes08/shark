#include <shark/physics/ballistic_body.hpp>
#include <shark/simulation/fixed_step_clock.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

struct BallisticRun final {
    shark::physics::BallisticBodyState state;
    std::uint64_t step_count{};
};

[[nodiscard]] BallisticRun run_ballistic_schedule(
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
    physics::BallisticBodyState state{
        .position = {3.0F, 20.0F, -2.0F},
        .linear_velocity = {4.0F, 6.0F, -1.0F},
    };

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
            REQUIRE(physics::advance_ballistic_body(
                state,
                physics::standard_gravity,
                clock.fixed_delta_seconds()));
        }
    }

    return BallisticRun{
        .state = state,
        .step_count = clock.total_step_count(),
    };
}

void require_float3(
    const shark::math::Float3 actual,
    const shark::math::Float3 expected,
    const float margin = 0.0001F)
{
    REQUIRE(actual.x == Catch::Approx(expected.x).margin(margin));
    REQUIRE(actual.y == Catch::Approx(expected.y).margin(margin));
    REQUIRE(actual.z == Catch::Approx(expected.z).margin(margin));
}

} // namespace

TEST_CASE(
    "ballistic body advances with semi-implicit Euler",
    "[physics][ballistic][integration]")
{
    using namespace shark;

    physics::BallisticBodyState state{
        .position = {3.0F, 20.0F, -2.0F},
        .linear_velocity = {4.0F, 6.0F, -1.0F},
    };
    constexpr auto fixed_delta = 1.0F / 60.0F;
    const auto first_step = physics::advance_ballistic_body(
        state,
        physics::standard_gravity,
        fixed_delta);
    REQUIRE(first_step);
    require_float3(
        state.linear_velocity,
        {4.0F, 5.83650017F, -1.0F},
        0.000001F);
    require_float3(
        state.position,
        {3.06666660F, 20.09727478F, -2.01666665F},
        0.000001F);

    state = {
        .position = {3.0F, 20.0F, -2.0F},
        .linear_velocity = {4.0F, 6.0F, -1.0F},
    };
    constexpr std::uint32_t step_count = 120;
    for (std::uint32_t step = 0;
         step < step_count;
         ++step) {
        REQUIRE(physics::advance_ballistic_body(
            state,
            physics::standard_gravity,
            fixed_delta));
    }

    // Semi-implicit Euler with constant acceleration has the discrete
    // recurrence:
    // v_n = v_0 + n*a*h
    // p_n = p_0 + n*h*v_0 + a*h*h*n*(n+1)/2.
    require_float3(
        state.linear_velocity,
        {4.0F, -13.62F, -1.0F});
    require_float3(
        state.position,
        {11.0F, 12.2165F, -4.0F});
}

TEST_CASE(
    "ballistic trajectory is bit-identical across render rates",
    "[physics][ballistic][fixed-step][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30,
        60,
        120,
        144,
    };
    const auto baseline = run_ballistic_schedule(render_rates[0], 2);
    REQUIRE(baseline.step_count == 120);

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        const auto run = run_ballistic_schedule(render_rate, 2);
        REQUIRE(run.step_count == baseline.step_count);
        REQUIRE(run.state == baseline.state);
    }
    require_float3(
        baseline.state.position,
        {11.0F, 12.2165F, -4.0F});
    require_float3(
        baseline.state.linear_velocity,
        {4.0F, -13.62F, -1.0F});
}

TEST_CASE(
    "ballistic integration rejects invalid input without mutation",
    "[physics][ballistic][validation]")
{
    using namespace shark;

    const physics::BallisticBodyState baseline{
        .position = {3.0F, 20.0F, -2.0F},
        .linear_velocity = {4.0F, 6.0F, -1.0F},
    };
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity = std::numeric_limits<float>::infinity();

    const std::array invalid_inputs{
        std::pair{
            math::Float3{nan, 0.0F, 0.0F},
            1.0F / 60.0F,
        },
        std::pair{
            physics::standard_gravity,
            0.0F,
        },
        std::pair{
            physics::standard_gravity,
            -1.0F / 60.0F,
        },
        std::pair{
            physics::standard_gravity,
            0.250001F,
        },
        std::pair{
            physics::standard_gravity,
            infinity,
        },
    };
    for (const auto& [acceleration, delta] : invalid_inputs) {
        auto state = baseline;
        const auto result = physics::advance_ballistic_body(
            state,
            acceleration,
            delta);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
        REQUIRE(state == baseline);
    }

    auto invalid_state = baseline;
    invalid_state.position.x = infinity;
    const auto invalid_state_before = invalid_state;
    const auto invalid_state_result =
        physics::advance_ballistic_body(
            invalid_state,
            physics::standard_gravity,
            1.0F / 60.0F);
    REQUIRE_FALSE(invalid_state_result);
    REQUIRE(invalid_state_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(invalid_state == invalid_state_before);

    physics::BallisticBodyState overflowing{
        .position = {
            std::numeric_limits<float>::max(),
            0.0F,
            0.0F,
        },
        .linear_velocity = {
            std::numeric_limits<float>::max(),
            0.0F,
            0.0F,
        },
    };
    const auto overflowing_before = overflowing;
    const auto overflow_result = physics::advance_ballistic_body(
        overflowing,
        {},
        0.25F);
    REQUIRE_FALSE(overflow_result);
    REQUIRE(overflow_result.error().code() ==
        core::ErrorCode::unavailable);
    REQUIRE(overflowing == overflowing_before);
}

TEST_CASE(
    "ballistic snapshots interpolate without changing authority",
    "[physics][ballistic][interpolation]")
{
    using namespace shark;

    const physics::BallisticBodyState previous{
        .position = {0.0F, 0.0F, 0.0F},
        .linear_velocity = {0.0F, 0.0F, 0.0F},
    };
    const physics::BallisticBodyState current{
        .position = {4.0F, -2.0F, 8.0F},
        .linear_velocity = {8.0F, 4.0F, -4.0F},
    };
    const auto previous_before = previous;
    const auto current_before = current;

    const auto start =
        physics::interpolate_ballistic_body(
            previous,
            current,
            0.0F);
    const auto quarter =
        physics::interpolate_ballistic_body(
            previous,
            current,
            0.25F);
    const auto end =
        physics::interpolate_ballistic_body(
            previous,
            current,
            1.0F);
    REQUIRE(start);
    REQUIRE(quarter);
    REQUIRE(end);
    REQUIRE(start.value() == previous);
    REQUIRE(end.value() == current);
    require_float3(quarter.value().position, {1.0F, -0.5F, 2.0F});
    require_float3(
        quarter.value().linear_velocity,
        {2.0F, 1.0F, -1.0F});
    REQUIRE(previous == previous_before);
    REQUIRE(current == current_before);

    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    for (const auto alpha : {-0.001F, 1.001F, nan}) {
        const auto result =
            physics::interpolate_ballistic_body(
                previous,
                current,
                alpha);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    auto invalid_previous = previous;
    invalid_previous.linear_velocity.z =
        std::numeric_limits<float>::infinity();
    const auto invalid_state =
        physics::interpolate_ballistic_body(
            invalid_previous,
            current,
            0.5F);
    REQUIRE_FALSE(invalid_state);
    REQUIRE(invalid_state.error().code() ==
        core::ErrorCode::invalid_argument);
}
