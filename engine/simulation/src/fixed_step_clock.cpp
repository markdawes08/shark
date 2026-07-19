#include <shark/simulation/fixed_step_clock.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace shark::simulation {
namespace {

inline constexpr std::uint64_t nanoseconds_per_second =
    1'000'000'000ULL;
inline constexpr std::uint32_t maximum_tick_rate_hz = 1'000;
inline constexpr auto maximum_supported_frame_delta =
    std::chrono::seconds{1};

[[nodiscard]] core::Error clock_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] bool valid_config(
    const FixedStepClockConfig& config) noexcept
{
    if (config.ticks_per_second == 0 ||
        config.ticks_per_second > maximum_tick_rate_hz ||
        config.maximum_frame_delta.count() <= 0 ||
        config.maximum_frame_delta > maximum_supported_frame_delta) {
        return false;
    }

    const auto maximum_count =
        static_cast<std::uint64_t>(
            config.maximum_frame_delta.count());
    return maximum_count <=
        std::numeric_limits<std::uint64_t>::max() /
            config.ticks_per_second;
}

} // namespace

core::Result<FixedStepClock> FixedStepClock::create(
    const FixedStepClockConfig config)
{
    if (!valid_config(config)) {
        return core::Result<FixedStepClock>::failure(clock_error(
            core::ErrorCode::invalid_argument,
            "Fixed-step clock requires a 1..1000 Hz rate and a positive "
            "maximum frame delta no greater than one second"));
    }
    return core::Result<FixedStepClock>::success(
        FixedStepClock{config});
}

FixedStepClock::FixedStepClock(
    const FixedStepClockConfig config) noexcept
    : config_{config},
      paused_{config.initially_paused}
{
}

core::Result<FixedStepFrame> FixedStepClock::advance(
    const std::chrono::nanoseconds elapsed)
{
    if (elapsed.count() < 0) {
        return core::Result<FixedStepFrame>::failure(clock_error(
            core::ErrorCode::invalid_argument,
            "Fixed-step elapsed time cannot be negative"));
    }

    if (paused_) {
        const auto step_count =
            static_cast<std::uint32_t>(single_step_pending_);
        if (step_count != 0 &&
            total_step_count_ ==
                std::numeric_limits<std::uint64_t>::max()) {
            return core::Result<FixedStepFrame>::failure(clock_error(
                core::ErrorCode::unavailable,
                "Fixed-step tick counter would overflow"));
        }
        single_step_pending_ = false;
        total_step_count_ += step_count;
        return core::Result<FixedStepFrame>::success(FixedStepFrame{
            .step_count = step_count,
            .interpolation_alpha = 1.0F,
            .accepted_elapsed = std::chrono::nanoseconds{0},
            .discarded_elapsed = elapsed,
        });
    }

    const auto accepted_elapsed =
        std::min(elapsed, config_.maximum_frame_delta);
    const auto discarded_elapsed =
        elapsed - accepted_elapsed;
    const auto accepted_count =
        static_cast<std::uint64_t>(accepted_elapsed.count());
    const auto added_phase =
        accepted_count * config_.ticks_per_second;
    const auto combined_phase =
        phase_numerator_ + added_phase;
    const auto step_count = combined_phase / nanoseconds_per_second;
    const auto next_phase = combined_phase % nanoseconds_per_second;
    if (step_count >
            std::numeric_limits<std::uint32_t>::max() ||
        step_count >
            std::numeric_limits<std::uint64_t>::max() -
                total_step_count_) {
        return core::Result<FixedStepFrame>::failure(clock_error(
            core::ErrorCode::unavailable,
            "Fixed-step tick accounting would overflow"));
    }

    phase_numerator_ = next_phase;
    total_step_count_ += step_count;
    return core::Result<FixedStepFrame>::success(FixedStepFrame{
        .step_count = static_cast<std::uint32_t>(step_count),
        .interpolation_alpha = interpolation_alpha(),
        .accepted_elapsed = accepted_elapsed,
        .discarded_elapsed = discarded_elapsed,
    });
}

void FixedStepClock::set_paused(const bool paused) noexcept
{
    if (paused_ == paused) {
        return;
    }
    paused_ = paused;
    discard_accumulated_time();
    single_step_pending_ = false;
}

bool FixedStepClock::paused() const noexcept
{
    return paused_;
}

core::Result<void> FixedStepClock::request_single_step()
{
    if (!paused_) {
        return core::Result<void>::failure(clock_error(
            core::ErrorCode::invalid_state,
            "A single simulation step can be requested only while paused"));
    }
    if (single_step_pending_) {
        return core::Result<void>::failure(clock_error(
            core::ErrorCode::invalid_state,
            "A single simulation step is already pending"));
    }
    single_step_pending_ = true;
    return core::Result<void>::success();
}

void FixedStepClock::discard_accumulated_time() noexcept
{
    phase_numerator_ = 0;
}

std::uint32_t FixedStepClock::ticks_per_second() const noexcept
{
    return config_.ticks_per_second;
}

float FixedStepClock::fixed_delta_seconds() const noexcept
{
    return 1.0F /
        static_cast<float>(config_.ticks_per_second);
}

std::chrono::nanoseconds
FixedStepClock::fixed_step_ceiling_duration() const noexcept
{
    const auto rate =
        static_cast<std::uint64_t>(config_.ticks_per_second);
    return std::chrono::nanoseconds{
        static_cast<std::chrono::nanoseconds::rep>(
            (nanoseconds_per_second + rate - 1U) / rate)};
}

std::uint64_t FixedStepClock::total_step_count() const noexcept
{
    return total_step_count_;
}

float FixedStepClock::interpolation_alpha() const noexcept
{
    if (paused_) {
        return 1.0F;
    }
    return static_cast<float>(
        static_cast<double>(phase_numerator_) /
        static_cast<double>(nanoseconds_per_second));
}

} // namespace shark::simulation
