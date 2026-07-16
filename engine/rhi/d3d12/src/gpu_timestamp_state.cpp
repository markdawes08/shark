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
            "A GPU timing sample requires exactly six timestamps"));
    }

    const auto frame_begin =
        raw_timestamps[index_of(GpuTimestampQuery::frame_begin)];
    const auto textured_cube_begin = raw_timestamps[
        index_of(GpuTimestampQuery::textured_cube_begin)];
    const auto textured_cube_end = raw_timestamps[
        index_of(GpuTimestampQuery::textured_cube_end)];
    const auto skybox_begin =
        raw_timestamps[index_of(GpuTimestampQuery::skybox_begin)];
    const auto skybox_end =
        raw_timestamps[index_of(GpuTimestampQuery::skybox_end)];
    const auto frame_end =
        raw_timestamps[index_of(GpuTimestampQuery::frame_end)];
    if (frame_begin > textured_cube_begin ||
        textured_cube_begin > textured_cube_end ||
        textured_cube_end > skybox_begin ||
        skybox_begin > skybox_end ||
        skybox_end > frame_end) {
        return core::Result<GpuTimingSample>::failure(timestamp_error(
            core::ErrorCode::invalid_state,
            "GPU timestamps must contain ordered textured-cube and "
            "skybox intervals inside the frame interval"));
    }

    const GpuTimingSample sample{
        frame_end - frame_begin,
        textured_cube_end - textured_cube_begin,
        skybox_end - skybox_begin,
    };
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (sample_count_ == maximum ||
        sample.frame_ticks > maximum - frame_total_ticks_ ||
        sample.textured_cube_ticks >
            maximum - textured_cube_total_ticks_ ||
        sample.skybox_ticks > maximum - skybox_total_ticks_) {
        return core::Result<GpuTimingSample>::failure(timestamp_error(
            core::ErrorCode::unavailable,
            "GPU timing accumulation would overflow"));
    }

    if (sample_count_ == 0) {
        frame_min_ticks_ = sample.frame_ticks;
        textured_cube_min_ticks_ = sample.textured_cube_ticks;
        skybox_min_ticks_ = sample.skybox_ticks;
    } else {
        frame_min_ticks_ = std::min(
            frame_min_ticks_,
            sample.frame_ticks);
        textured_cube_min_ticks_ = std::min(
            textured_cube_min_ticks_,
            sample.textured_cube_ticks);
        skybox_min_ticks_ = std::min(
            skybox_min_ticks_,
            sample.skybox_ticks);
    }
    frame_max_ticks_ = std::max(
        frame_max_ticks_,
        sample.frame_ticks);
    textured_cube_max_ticks_ = std::max(
        textured_cube_max_ticks_,
        sample.textured_cube_ticks);
    skybox_max_ticks_ = std::max(
        skybox_max_ticks_,
        sample.skybox_ticks);
    frame_last_ticks_ = sample.frame_ticks;
    textured_cube_last_ticks_ = sample.textured_cube_ticks;
    skybox_last_ticks_ = sample.skybox_ticks;
    frame_total_ticks_ += sample.frame_ticks;
    textured_cube_total_ticks_ += sample.textured_cube_ticks;
    skybox_total_ticks_ += sample.skybox_ticks;
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

std::uint64_t
GpuTimingAccumulator::textured_cube_total_ticks() const noexcept
{
    return textured_cube_total_ticks_;
}

std::uint64_t
GpuTimingAccumulator::textured_cube_min_ticks() const noexcept
{
    return textured_cube_min_ticks_;
}

std::uint64_t
GpuTimingAccumulator::textured_cube_max_ticks() const noexcept
{
    return textured_cube_max_ticks_;
}

std::uint64_t
GpuTimingAccumulator::textured_cube_last_ticks() const noexcept
{
    return textured_cube_last_ticks_;
}

std::uint64_t GpuTimingAccumulator::skybox_total_ticks() const noexcept
{
    return skybox_total_ticks_;
}

std::uint64_t GpuTimingAccumulator::skybox_min_ticks() const noexcept
{
    return skybox_min_ticks_;
}

std::uint64_t GpuTimingAccumulator::skybox_max_ticks() const noexcept
{
    return skybox_max_ticks_;
}

std::uint64_t GpuTimingAccumulator::skybox_last_ticks() const noexcept
{
    return skybox_last_ticks_;
}

} // namespace shark::rhi::d3d12::detail
