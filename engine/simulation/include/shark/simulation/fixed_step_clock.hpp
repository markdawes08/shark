#pragma once

#include <shark/core/result.hpp>

#include <chrono>
#include <cstdint>

namespace shark::simulation {

inline constexpr std::uint32_t fixed_simulation_rate_hz = 60;

struct FixedStepClockConfig final {
    std::uint32_t ticks_per_second{fixed_simulation_rate_hz};
    std::chrono::nanoseconds maximum_frame_delta{
        std::chrono::milliseconds{250}};
    bool initially_paused{true};
};

struct FixedStepFrame final {
    std::uint32_t step_count{};
    float interpolation_alpha{};
    std::chrono::nanoseconds accepted_elapsed{};
    std::chrono::nanoseconds discarded_elapsed{};
};

class FixedStepClock final {
public:
    [[nodiscard]] static core::Result<FixedStepClock> create(
        FixedStepClockConfig config = {});

    [[nodiscard]] core::Result<FixedStepFrame> advance(
        std::chrono::nanoseconds elapsed);

    void set_paused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;

    [[nodiscard]] core::Result<void> request_single_step();
    void discard_accumulated_time() noexcept;

    [[nodiscard]] std::uint32_t ticks_per_second() const noexcept;
    [[nodiscard]] float fixed_delta_seconds() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds
        fixed_step_ceiling_duration() const noexcept;
    [[nodiscard]] std::uint64_t total_step_count() const noexcept;
    [[nodiscard]] float interpolation_alpha() const noexcept;

private:
    explicit FixedStepClock(FixedStepClockConfig config) noexcept;

    FixedStepClockConfig config_;
    std::uint64_t phase_numerator_{};
    std::uint64_t total_step_count_{};
    bool paused_{};
    bool single_step_pending_{};
};

} // namespace shark::simulation
