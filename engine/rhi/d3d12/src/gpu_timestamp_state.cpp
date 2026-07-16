#include "gpu_timestamp_state.hpp"

#include <shark/core/error.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace shark::rhi::d3d12::detail {
namespace {

[[nodiscard]] core::Error timestamp_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::graphics,
        code,
        std::move(message),
    };
}

[[nodiscard]] constexpr std::size_t index_of(
    const GpuTimestampQuery query) noexcept
{
    return static_cast<std::size_t>(query);
}

} // namespace

core::Result<GpuTimingSample> GpuTimingAccumulator::consume(
    const std::span<const std::uint64_t> raw_timestamps)
{
    if (raw_timestamps.size() != gpu_timestamp_queries_per_frame) {
        return core::Result<GpuTimingSample>::failure(timestamp_error(
            core::ErrorCode::invalid_argument,
            "A GPU timing sample requires exactly four timestamps"));
    }

    const auto frame_begin =
        raw_timestamps[index_of(GpuTimestampQuery::frame_begin)];
    const auto pass_begin =
        raw_timestamps[index_of(GpuTimestampQuery::pass_begin)];
    const auto pass_end =
        raw_timestamps[index_of(GpuTimestampQuery::pass_end)];
    const auto frame_end =
        raw_timestamps[index_of(GpuTimestampQuery::frame_end)];
    if (frame_begin > pass_begin ||
        pass_begin > pass_end ||
        pass_end > frame_end) {
        return core::Result<GpuTimingSample>::failure(timestamp_error(
            core::ErrorCode::invalid_state,
            "GPU timestamps must contain a nested pass interval inside "
            "the frame interval"));
    }

    const GpuTimingSample sample{
        frame_end - frame_begin,
        pass_end - pass_begin,
    };
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (sample_count_ == maximum ||
        sample.frame_ticks > maximum - frame_total_ticks_ ||
        sample.pass_ticks > maximum - pass_total_ticks_) {
        return core::Result<GpuTimingSample>::failure(timestamp_error(
            core::ErrorCode::unavailable,
            "GPU timing accumulation would overflow"));
    }

    if (sample_count_ == 0) {
        frame_min_ticks_ = sample.frame_ticks;
        pass_min_ticks_ = sample.pass_ticks;
    } else {
        frame_min_ticks_ = std::min(
            frame_min_ticks_,
            sample.frame_ticks);
        pass_min_ticks_ = std::min(
            pass_min_ticks_,
            sample.pass_ticks);
    }
    frame_max_ticks_ = std::max(
        frame_max_ticks_,
        sample.frame_ticks);
    pass_max_ticks_ = std::max(
        pass_max_ticks_,
        sample.pass_ticks);
    frame_last_ticks_ = sample.frame_ticks;
    pass_last_ticks_ = sample.pass_ticks;
    frame_total_ticks_ += sample.frame_ticks;
    pass_total_ticks_ += sample.pass_ticks;
    ++sample_count_;
    return core::Result<GpuTimingSample>::success(sample);
}

std::uint64_t GpuTimingAccumulator::sample_count() const noexcept
{
    return sample_count_;
}

std::uint64_t GpuTimingAccumulator::frame_total_ticks() const noexcept
{
    return frame_total_ticks_;
}

std::uint64_t GpuTimingAccumulator::frame_min_ticks() const noexcept
{
    return frame_min_ticks_;
}

std::uint64_t GpuTimingAccumulator::frame_max_ticks() const noexcept
{
    return frame_max_ticks_;
}

std::uint64_t GpuTimingAccumulator::frame_last_ticks() const noexcept
{
    return frame_last_ticks_;
}

std::uint64_t GpuTimingAccumulator::pass_total_ticks() const noexcept
{
    return pass_total_ticks_;
}

std::uint64_t GpuTimingAccumulator::pass_min_ticks() const noexcept
{
    return pass_min_ticks_;
}

std::uint64_t GpuTimingAccumulator::pass_max_ticks() const noexcept
{
    return pass_max_ticks_;
}

std::uint64_t GpuTimingAccumulator::pass_last_ticks() const noexcept
{
    return pass_last_ticks_;
}

} // namespace shark::rhi::d3d12::detail
