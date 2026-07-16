#pragma once

#include <shark/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace shark::rhi::d3d12::detail {

inline constexpr std::size_t gpu_timestamp_queries_per_frame = 4;
inline constexpr std::size_t gpu_timestamp_result_bytes_per_frame =
    gpu_timestamp_queries_per_frame * sizeof(std::uint64_t);

enum class GpuTimestampQuery : std::size_t {
    frame_begin = 0,
    pass_begin,
    pass_end,
    frame_end,
};

struct GpuTimestampSlice final {
    std::uint32_t query_base{};
    std::uint64_t readback_offset_bytes{};
};

[[nodiscard]] constexpr GpuTimestampSlice gpu_timestamp_slice(
    const std::uint32_t context_index) noexcept
{
    return GpuTimestampSlice{
        context_index *
            static_cast<std::uint32_t>(gpu_timestamp_queries_per_frame),
        context_index * gpu_timestamp_result_bytes_per_frame,
    };
}

struct GpuTimingSample final {
    std::uint64_t frame_ticks{};
    std::uint64_t pass_ticks{};
};

class GpuTimingAccumulator final {
public:
    [[nodiscard]] core::Result<GpuTimingSample> consume(
        std::span<const std::uint64_t> raw_timestamps);

    [[nodiscard]] std::uint64_t sample_count() const noexcept;
    [[nodiscard]] std::uint64_t frame_total_ticks() const noexcept;
    [[nodiscard]] std::uint64_t frame_min_ticks() const noexcept;
    [[nodiscard]] std::uint64_t frame_max_ticks() const noexcept;
    [[nodiscard]] std::uint64_t frame_last_ticks() const noexcept;
    [[nodiscard]] std::uint64_t pass_total_ticks() const noexcept;
    [[nodiscard]] std::uint64_t pass_min_ticks() const noexcept;
    [[nodiscard]] std::uint64_t pass_max_ticks() const noexcept;
    [[nodiscard]] std::uint64_t pass_last_ticks() const noexcept;

private:
    std::uint64_t sample_count_{};
    std::uint64_t frame_total_ticks_{};
    std::uint64_t frame_min_ticks_{};
    std::uint64_t frame_max_ticks_{};
    std::uint64_t frame_last_ticks_{};
    std::uint64_t pass_total_ticks_{};
    std::uint64_t pass_min_ticks_{};
    std::uint64_t pass_max_ticks_{};
    std::uint64_t pass_last_ticks_{};
};

} // namespace shark::rhi::d3d12::detail
