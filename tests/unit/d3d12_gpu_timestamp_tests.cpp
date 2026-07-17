#include "gpu_timestamp_state.hpp"

#include <shark/core/error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <limits>

TEST_CASE(
    "GPU timestamp slices partition query and readback storage exactly",
    "[gpu][timestamps]")
{
    using shark::rhi::d3d12::detail::gpu_timestamp_slice;

    const auto first = gpu_timestamp_slice(0);
    const auto second = gpu_timestamp_slice(1);
    const auto third = gpu_timestamp_slice(2);
    REQUIRE(first.query_base == 0);
    REQUIRE(second.query_base == 8);
    REQUIRE(third.query_base == 16);
    REQUIRE(first.readback_offset_bytes == 0);
    REQUIRE(second.readback_offset_bytes == 64);
    REQUIRE(third.readback_offset_bytes == 128);
}

TEST_CASE(
    "GPU timing accumulation accepts ordered and zero length intervals",
    "[gpu][timestamps]")
{
    using shark::rhi::d3d12::detail::GpuTimingAccumulator;

    GpuTimingAccumulator accumulator;
    constexpr std::array<std::uint64_t, 8> first{
        100,
        110,
        120,
        130,
        160,
        170,
        190,
        200,
    };
    const auto first_result = accumulator.consume(first);
    REQUIRE(first_result);
    REQUIRE(first_result.value().frame_ticks == 100);
    REQUIRE(first_result.value().terrain_ticks == 10);
    REQUIRE(first_result.value().textured_cube_ticks == 30);
    REQUIRE(first_result.value().skybox_ticks == 20);

    constexpr std::array<std::uint64_t, 8> zero{
        300,
        300,
        300,
        300,
        300,
        300,
        300,
        300,
    };
    const auto zero_result = accumulator.consume(zero);
    REQUIRE(zero_result);
    REQUIRE(zero_result.value().frame_ticks == 0);
    REQUIRE(zero_result.value().terrain_ticks == 0);
    REQUIRE(zero_result.value().textured_cube_ticks == 0);
    REQUIRE(zero_result.value().skybox_ticks == 0);

    REQUIRE(accumulator.sample_count() == 2);
    REQUIRE(accumulator.frame_total_ticks() == 100);
    REQUIRE(accumulator.frame_min_ticks() == 0);
    REQUIRE(accumulator.frame_max_ticks() == 100);
    REQUIRE(accumulator.frame_last_ticks() == 0);
    REQUIRE(accumulator.terrain_total_ticks() == 10);
    REQUIRE(accumulator.terrain_min_ticks() == 0);
    REQUIRE(accumulator.terrain_max_ticks() == 10);
    REQUIRE(accumulator.terrain_last_ticks() == 0);
    REQUIRE(accumulator.textured_cube_total_ticks() == 30);
    REQUIRE(accumulator.textured_cube_min_ticks() == 0);
    REQUIRE(accumulator.textured_cube_max_ticks() == 30);
    REQUIRE(accumulator.textured_cube_last_ticks() == 0);
    REQUIRE(accumulator.skybox_total_ticks() == 20);
    REQUIRE(accumulator.skybox_min_ticks() == 0);
    REQUIRE(accumulator.skybox_max_ticks() == 20);
    REQUIRE(accumulator.skybox_last_ticks() == 0);
}

TEST_CASE(
    "GPU timing accumulation rejects incomplete and reversed samples",
    "[gpu][timestamps]")
{
    using shark::rhi::d3d12::detail::GpuTimingAccumulator;

    GpuTimingAccumulator accumulator;
    constexpr std::array<std::uint64_t, 7> incomplete{
        1,
        2,
        3,
        4,
        5,
        6,
        7,
    };
    const auto incomplete_result = accumulator.consume(incomplete);
    REQUIRE_FALSE(incomplete_result);
    REQUIRE(incomplete_result.error().code() ==
        shark::core::ErrorCode::invalid_argument);

    constexpr std::array<std::array<std::uint64_t, 8>, 7> invalid{
        std::array<std::uint64_t, 8>{2, 1, 3, 3, 3, 3, 3, 4},
        std::array<std::uint64_t, 8>{1, 3, 2, 3, 3, 3, 3, 4},
        std::array<std::uint64_t, 8>{1, 2, 4, 3, 4, 4, 4, 5},
        std::array<std::uint64_t, 8>{1, 2, 3, 5, 4, 5, 5, 6},
        std::array<std::uint64_t, 8>{1, 2, 3, 4, 6, 5, 6, 7},
        std::array<std::uint64_t, 8>{1, 2, 3, 4, 5, 7, 6, 8},
        std::array<std::uint64_t, 8>{1, 2, 3, 4, 5, 6, 8, 7},
    };
    for (const auto& sample : invalid) {
        const auto result = accumulator.consume(sample);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code() ==
            shark::core::ErrorCode::invalid_state);
    }
    REQUIRE(accumulator.sample_count() == 0);
}

TEST_CASE(
    "GPU timing accumulation preserves state when totals would overflow",
    "[gpu][timestamps]")
{
    using shark::rhi::d3d12::detail::GpuTimingAccumulator;

    GpuTimingAccumulator accumulator;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    constexpr std::array<std::uint64_t, 8> maximum_sample{
        0,
        0,
        maximum,
        maximum,
        maximum,
        maximum,
        maximum,
        maximum,
    };
    REQUIRE(accumulator.consume(maximum_sample));
    REQUIRE(accumulator.sample_count() == 1);
    REQUIRE(accumulator.frame_total_ticks() == maximum);
    REQUIRE(accumulator.terrain_total_ticks() == maximum);
    REQUIRE(accumulator.textured_cube_total_ticks() == 0);
    REQUIRE(accumulator.skybox_total_ticks() == 0);

    constexpr std::array<std::uint64_t, 8> one_tick{
        10,
        10,
        11,
        11,
        11,
        11,
        11,
        11,
    };
    const auto overflow = accumulator.consume(one_tick);
    REQUIRE_FALSE(overflow);
    REQUIRE(overflow.error().code() ==
        shark::core::ErrorCode::unavailable);
    REQUIRE(accumulator.sample_count() == 1);
    REQUIRE(accumulator.frame_total_ticks() == maximum);
    REQUIRE(accumulator.terrain_total_ticks() == maximum);
    REQUIRE(accumulator.textured_cube_total_ticks() == 0);
    REQUIRE(accumulator.skybox_total_ticks() == 0);
}
