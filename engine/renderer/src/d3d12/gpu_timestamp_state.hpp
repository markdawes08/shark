#pragma once

#include <shark/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace shark::renderer::d3d12::detail {

inline constexpr std::size_t gpu_timestamp_queries_per_frame = 12;
inline constexpr std::size_t gpu_timestamp_result_bytes_per_frame =
    gpu_timestamp_queries_per_frame * sizeof(std::uint64_t);

enum class GpuTimestampQuery : std::size_t {
    frame_begin = 0,
    terrain_begin,
    terrain_end,
    textured_cube_begin,
    textured_cube_end,
    skybox_begin,
    skybox_end,
    water_begin,
    water_end,
    tone_map_begin,
    tone_map_end,
    frame_end,
};

static_assert(
    static_cast<std::size_t>(GpuTimestampQuery::frame_end) + 1U ==
    gpu_timestamp_queries_per_frame);

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
    std::uint64_t terrain_ticks{};
    std::uint64_t textured_cube_ticks{};
    std::uint64_t water_ticks{};
    std::uint64_t skybox_ticks{};
    std::uint64_t tone_map_ticks{};
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
    [[nodiscard]] std::uint64_t terrain_total_ticks() const noexcept;
    [[nodiscard]] std::uint64_t terrain_min_ticks() const noexcept;
    [[nodiscard]] std::uint64_t terrain_max_ticks() const noexcept;
    [[nodiscard]] std::uint64_t terrain_last_ticks() const noexcept;
    [[nodiscard]] std::uint64_t textured_cube_total_ticks() const noexcept;
    [[nodiscard]] std::uint64_t textured_cube_min_ticks() const noexcept;
    [[nodiscard]] std::uint64_t textured_cube_max_ticks() const noexcept;
    [[nodiscard]] std::uint64_t textured_cube_last_ticks() const noexcept;
    [[nodiscard]] std::uint64_t water_total_ticks() const noexcept;
    [[nodiscard]] std::uint64_t water_min_ticks() const noexcept;
    [[nodiscard]] std::uint64_t water_max_ticks() const noexcept;
    [[nodiscard]] std::uint64_t water_last_ticks() const noexcept;
    [[nodiscard]] std::uint64_t skybox_total_ticks() const noexcept;
    [[nodiscard]] std::uint64_t skybox_min_ticks() const noexcept;
    [[nodiscard]] std::uint64_t skybox_max_ticks() const noexcept;
    [[nodiscard]] std::uint64_t skybox_last_ticks() const noexcept;
    [[nodiscard]] std::uint64_t tone_map_total_ticks() const noexcept;
    [[nodiscard]] std::uint64_t tone_map_min_ticks() const noexcept;
    [[nodiscard]] std::uint64_t tone_map_max_ticks() const noexcept;
    [[nodiscard]] std::uint64_t tone_map_last_ticks() const noexcept;

private:
    std::uint64_t sample_count_{};
    std::uint64_t frame_total_ticks_{};
    std::uint64_t frame_min_ticks_{};
    std::uint64_t frame_max_ticks_{};
    std::uint64_t frame_last_ticks_{};
    std::uint64_t terrain_total_ticks_{};
    std::uint64_t terrain_min_ticks_{};
    std::uint64_t terrain_max_ticks_{};
    std::uint64_t terrain_last_ticks_{};
    std::uint64_t textured_cube_total_ticks_{};
    std::uint64_t textured_cube_min_ticks_{};
    std::uint64_t textured_cube_max_ticks_{};
    std::uint64_t textured_cube_last_ticks_{};
    std::uint64_t water_total_ticks_{};
    std::uint64_t water_min_ticks_{};
    std::uint64_t water_max_ticks_{};
    std::uint64_t water_last_ticks_{};
    std::uint64_t skybox_total_ticks_{};
    std::uint64_t skybox_min_ticks_{};
    std::uint64_t skybox_max_ticks_{};
    std::uint64_t skybox_last_ticks_{};
    std::uint64_t tone_map_total_ticks_{};
    std::uint64_t tone_map_min_ticks_{};
    std::uint64_t tone_map_max_ticks_{};
    std::uint64_t tone_map_last_ticks_{};
};

} // namespace shark::renderer::d3d12::detail
