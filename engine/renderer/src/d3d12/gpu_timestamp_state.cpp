#include "gpu_timestamp_state.hpp"

#include <shark/core/error.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace shark::renderer::d3d12::detail {
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
            "A GPU timing sample requires exactly ten timestamps"));
    }

    const auto frame_begin =
        raw_timestamps[index_of(GpuTimestampQuery::frame_begin)];
    const auto terrain_begin =
        raw_timestamps[index_of(GpuTimestampQuery::terrain_begin)];
    const auto terrain_end =
        raw_timestamps[index_of(GpuTimestampQuery::terrain_end)];
    const auto textured_cube_begin = raw_timestamps[
        index_of(GpuTimestampQuery::textured_cube_begin)];
    const auto textured_cube_end = raw_timestamps[
        index_of(GpuTimestampQuery::textured_cube_end)];
    const auto skybox_begin =
        raw_timestamps[index_of(GpuTimestampQuery::skybox_begin)];
    const auto skybox_end =
        raw_timestamps[index_of(GpuTimestampQuery::skybox_end)];
    const auto tone_map_begin =
        raw_timestamps[index_of(GpuTimestampQuery::tone_map_begin)];
    const auto tone_map_end =
        raw_timestamps[index_of(GpuTimestampQuery::tone_map_end)];
    const auto frame_end =
        raw_timestamps[index_of(GpuTimestampQuery::frame_end)];
    if (frame_begin > terrain_begin ||
        terrain_begin > terrain_end ||
        terrain_end > textured_cube_begin ||
        textured_cube_begin > textured_cube_end ||
        textured_cube_end > skybox_begin ||
        skybox_begin > skybox_end ||
        skybox_end > tone_map_begin ||
        tone_map_begin > tone_map_end ||
        tone_map_end > frame_end) {
        return core::Result<GpuTimingSample>::failure(timestamp_error(
            core::ErrorCode::invalid_state,
            "GPU timestamps must contain ordered terrain, textured-cube, "
            "skybox, and tone-map intervals inside the frame interval"));
    }

    const GpuTimingSample sample{
        frame_end - frame_begin,
        terrain_end - terrain_begin,
        textured_cube_end - textured_cube_begin,
        skybox_end - skybox_begin,
        tone_map_end - tone_map_begin,
    };
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (sample_count_ == maximum ||
        sample.frame_ticks > maximum - frame_total_ticks_ ||
        sample.terrain_ticks > maximum - terrain_total_ticks_ ||
        sample.textured_cube_ticks >
            maximum - textured_cube_total_ticks_ ||
        sample.skybox_ticks > maximum - skybox_total_ticks_ ||
        sample.tone_map_ticks > maximum - tone_map_total_ticks_) {
        return core::Result<GpuTimingSample>::failure(timestamp_error(
            core::ErrorCode::unavailable,
            "GPU timing accumulation would overflow"));
    }

    if (sample_count_ == 0) {
        frame_min_ticks_ = sample.frame_ticks;
        terrain_min_ticks_ = sample.terrain_ticks;
        textured_cube_min_ticks_ = sample.textured_cube_ticks;
        skybox_min_ticks_ = sample.skybox_ticks;
        tone_map_min_ticks_ = sample.tone_map_ticks;
    } else {
        frame_min_ticks_ = std::min(
            frame_min_ticks_,
            sample.frame_ticks);
        terrain_min_ticks_ = std::min(
            terrain_min_ticks_,
            sample.terrain_ticks);
        textured_cube_min_ticks_ = std::min(
            textured_cube_min_ticks_,
            sample.textured_cube_ticks);
        skybox_min_ticks_ = std::min(
            skybox_min_ticks_,
            sample.skybox_ticks);
        tone_map_min_ticks_ = std::min(
            tone_map_min_ticks_,
            sample.tone_map_ticks);
    }
    frame_max_ticks_ = std::max(
        frame_max_ticks_,
        sample.frame_ticks);
    terrain_max_ticks_ = std::max(
        terrain_max_ticks_,
        sample.terrain_ticks);
    textured_cube_max_ticks_ = std::max(
        textured_cube_max_ticks_,
        sample.textured_cube_ticks);
    skybox_max_ticks_ = std::max(
        skybox_max_ticks_,
        sample.skybox_ticks);
    tone_map_max_ticks_ = std::max(
        tone_map_max_ticks_,
        sample.tone_map_ticks);
    frame_last_ticks_ = sample.frame_ticks;
    terrain_last_ticks_ = sample.terrain_ticks;
    textured_cube_last_ticks_ = sample.textured_cube_ticks;
    skybox_last_ticks_ = sample.skybox_ticks;
    tone_map_last_ticks_ = sample.tone_map_ticks;
    frame_total_ticks_ += sample.frame_ticks;
    terrain_total_ticks_ += sample.terrain_ticks;
    textured_cube_total_ticks_ += sample.textured_cube_ticks;
    skybox_total_ticks_ += sample.skybox_ticks;
    tone_map_total_ticks_ += sample.tone_map_ticks;
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

std::uint64_t GpuTimingAccumulator::terrain_total_ticks() const noexcept
{
    return terrain_total_ticks_;
}

std::uint64_t GpuTimingAccumulator::terrain_min_ticks() const noexcept
{
    return terrain_min_ticks_;
}

std::uint64_t GpuTimingAccumulator::terrain_max_ticks() const noexcept
{
    return terrain_max_ticks_;
}

std::uint64_t GpuTimingAccumulator::terrain_last_ticks() const noexcept
{
    return terrain_last_ticks_;
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

std::uint64_t GpuTimingAccumulator::tone_map_total_ticks() const noexcept
{
    return tone_map_total_ticks_;
}

std::uint64_t GpuTimingAccumulator::tone_map_min_ticks() const noexcept
{
    return tone_map_min_ticks_;
}

std::uint64_t GpuTimingAccumulator::tone_map_max_ticks() const noexcept
{
    return tone_map_max_ticks_;
}

std::uint64_t GpuTimingAccumulator::tone_map_last_ticks() const noexcept
{
    return tone_map_last_ticks_;
}

} // namespace shark::renderer::d3d12::detail
