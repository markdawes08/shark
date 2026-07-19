#include <shark/simulation/fixed_step_clock.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <utility>

namespace {

using namespace std::chrono_literals;

struct ScheduleResult final {
    std::uint64_t emitted_steps{};
    std::uint64_t clock_steps{};
    float interpolation_alpha{};
};

[[nodiscard]] ScheduleResult run_exact_schedule(
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

    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) *
        duration_seconds;
    auto previous_timestamp = 0ns;
    std::uint64_t emitted_steps = 0;
    for (std::uint64_t frame = 1;
         frame <= frame_count;
         ++frame) {
        const auto timestamp = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                frame * 1'000'000'000ULL / render_rate_hz)};
        const auto elapsed = timestamp - previous_timestamp;
        previous_timestamp = timestamp;

        const auto advance_result = clock.advance(elapsed);
        REQUIRE(advance_result);
        const auto& update = advance_result.value();
        emitted_steps += update.step_count;
        REQUIRE(update.accepted_elapsed == elapsed);
        REQUIRE(update.discarded_elapsed == 0ns);
        REQUIRE(update.interpolation_alpha >= 0.0F);
        REQUIRE(update.interpolation_alpha <= 1.0F);
    }

    REQUIRE(previous_timestamp ==
        std::chrono::seconds{duration_seconds});
    return ScheduleResult{
        .emitted_steps = emitted_steps,
        .clock_steps = clock.total_step_count(),
        .interpolation_alpha = clock.interpolation_alpha(),
    };
}

} // namespace

TEST_CASE(
    "fixed-step clock keeps exact rational 60 Hz phase",
    "[simulation][fixed-step][clock]")
{
    using namespace shark;

    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();

    REQUIRE(clock.ticks_per_second() == 60);
    REQUIRE(clock.fixed_delta_seconds() ==
        Catch::Approx(1.0F / 60.0F));
    REQUIRE(clock.fixed_step_ceiling_duration() ==
        std::chrono::nanoseconds{16'666'667});
    REQUIRE(clock.total_step_count() == 0);
    REQUIRE(clock.interpolation_alpha() == 0.0F);

    const auto partial_result =
        clock.advance(std::chrono::milliseconds{5});
    REQUIRE(partial_result);
    REQUIRE(partial_result.value().step_count == 0);
    REQUIRE(partial_result.value().interpolation_alpha ==
        Catch::Approx(0.3F).margin(0.000001F));

    const auto boundary_result =
        clock.advance(std::chrono::nanoseconds{11'666'667});
    REQUIRE(boundary_result);
    REQUIRE(boundary_result.value().step_count == 1);
    REQUIRE(boundary_result.value().interpolation_alpha ==
        Catch::Approx(0.0F).margin(0.000001F));
    REQUIRE(clock.total_step_count() == 1);

    clock.discard_accumulated_time();
    REQUIRE(clock.interpolation_alpha() == 0.0F);
    const auto two_and_a_half_result =
        clock.advance(std::chrono::nanoseconds{41'666'666});
    REQUIRE(two_and_a_half_result);
    REQUIRE(two_and_a_half_result.value().step_count == 2);
    REQUIRE(two_and_a_half_result.value().interpolation_alpha ==
        Catch::Approx(0.5F).margin(0.000001F));
    REQUIRE(clock.total_step_count() == 3);
}

TEST_CASE(
    "fixed-step scheduling is invariant across render rates",
    "[simulation][fixed-step][clock][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30,
        60,
        120,
        144,
    };

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        const auto result = run_exact_schedule(render_rate, 2);
        REQUIRE(result.emitted_steps == 120);
        REQUIRE(result.clock_steps == 120);
        REQUIRE(result.interpolation_alpha ==
            Catch::Approx(0.0F).margin(0.000001F));
    }
}

TEST_CASE(
    "fixed-step pause single-step and clamp policies are explicit",
    "[simulation][fixed-step][clock][pause]")
{
    using namespace shark;
    using namespace std::chrono_literals;

    auto paused_result = simulation::FixedStepClock::create();
    REQUIRE(paused_result);
    auto paused = std::move(paused_result).value();
    REQUIRE(paused.paused());
    REQUIRE(paused.interpolation_alpha() == 1.0F);

    const auto discarded_result = paused.advance(10s);
    REQUIRE(discarded_result);
    REQUIRE(discarded_result.value().step_count == 0);
    REQUIRE(discarded_result.value().accepted_elapsed == 0ns);
    REQUIRE(discarded_result.value().discarded_elapsed == 10s);
    REQUIRE(paused.total_step_count() == 0);

    REQUIRE(paused.request_single_step());
    const auto duplicate_request = paused.request_single_step();
    REQUIRE_FALSE(duplicate_request);
    REQUIRE(duplicate_request.error().code() ==
        core::ErrorCode::invalid_state);

    const auto single_step_result = paused.advance(10s);
    REQUIRE(single_step_result);
    REQUIRE(single_step_result.value().step_count == 1);
    REQUIRE(single_step_result.value().interpolation_alpha == 1.0F);
    REQUIRE(single_step_result.value().accepted_elapsed == 0ns);
    REQUIRE(single_step_result.value().discarded_elapsed == 10s);
    REQUIRE(paused.total_step_count() == 1);

    const auto remains_paused_result = paused.advance(1s);
    REQUIRE(remains_paused_result);
    REQUIRE(remains_paused_result.value().step_count == 0);
    REQUIRE(paused.total_step_count() == 1);

    paused.set_paused(false);
    REQUIRE_FALSE(paused.paused());
    REQUIRE(paused.interpolation_alpha() == 0.0F);
    const auto running_single_step = paused.request_single_step();
    REQUIRE_FALSE(running_single_step);
    REQUIRE(running_single_step.error().code() ==
        core::ErrorCode::invalid_state);

    const auto partial_result = paused.advance(8ms);
    REQUIRE(partial_result);
    REQUIRE(partial_result.value().step_count == 0);
    REQUIRE(paused.interpolation_alpha() ==
        Catch::Approx(0.48F).margin(0.000001F));
    paused.set_paused(true);
    paused.set_paused(false);
    REQUIRE(paused.interpolation_alpha() == 0.0F);

    auto clamped_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clamped_result);
    auto clamped = std::move(clamped_result).value();
    const auto long_frame_result = clamped.advance(1s);
    REQUIRE(long_frame_result);
    REQUIRE(long_frame_result.value().step_count == 15);
    REQUIRE(long_frame_result.value().accepted_elapsed == 250ms);
    REQUIRE(long_frame_result.value().discarded_elapsed == 750ms);
    REQUIRE(long_frame_result.value().interpolation_alpha == 0.0F);
    REQUIRE(clamped.total_step_count() == 15);
}

TEST_CASE(
    "fixed-step clock rejects invalid configuration and elapsed time",
    "[simulation][fixed-step][clock][validation]")
{
    using namespace shark;
    using namespace std::chrono_literals;

    const std::array invalid_configs{
        simulation::FixedStepClockConfig{
            .ticks_per_second = 0,
        },
        simulation::FixedStepClockConfig{
            .ticks_per_second = 1'001,
        },
        simulation::FixedStepClockConfig{
            .maximum_frame_delta = 0ns,
        },
        simulation::FixedStepClockConfig{
            .maximum_frame_delta = 1s + 1ns,
        },
    };
    for (const auto& config : invalid_configs) {
        const auto result =
            simulation::FixedStepClock::create(config);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            core::ErrorCode::invalid_argument);
    }

    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();
    const auto partial_result = clock.advance(5ms);
    REQUIRE(partial_result);
    const auto step_count_before = clock.total_step_count();
    const auto alpha_before = clock.interpolation_alpha();

    const auto invalid_elapsed = clock.advance(-1ns);
    REQUIRE_FALSE(invalid_elapsed);
    REQUIRE(invalid_elapsed.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(clock.total_step_count() == step_count_before);
    REQUIRE(clock.interpolation_alpha() == alpha_before);

    clock.discard_accumulated_time();
    REQUIRE(clock.total_step_count() == step_count_before);
    REQUIRE(clock.interpolation_alpha() == 0.0F);
}
