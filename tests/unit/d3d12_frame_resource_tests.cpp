#include "frame_resource_state.hpp"

#include <shark/core/error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

TEST_CASE(
    "frame linear arenas align exactly and preserve state on exhaustion",
    "[gpu][frame-resources]")
{
    using shark::rhi::d3d12::detail::LinearArenaState;

    LinearArenaState arena{32};
    const auto first = arena.allocate(3, 1);
    REQUIRE(first);
    REQUIRE(first.value().offset == 0);
    REQUIRE(first.value().size == 3);

    const auto aligned = arena.allocate(4, 8);
    REQUIRE(aligned);
    REQUIRE(aligned.value().offset == 8);
    REQUIRE(arena.used() == 12);

    const auto exhausted = arena.allocate(21, 1);
    REQUIRE_FALSE(exhausted);
    REQUIRE(exhausted.error().code() ==
        shark::core::ErrorCode::unavailable);
    REQUIRE(arena.used() == 12);

    const auto exact_fit = arena.allocate(20, 1);
    REQUIRE(exact_fit);
    REQUIRE(exact_fit.value().offset == 12);
    REQUIRE(arena.used() == arena.capacity());
    REQUIRE(arena.high_watermark() == arena.capacity());
}

TEST_CASE(
    "frame linear arenas reject invalid and overflowing requests",
    "[gpu][frame-resources]")
{
    using shark::rhi::d3d12::detail::LinearArenaState;

    LinearArenaState arena{64};
    REQUIRE_FALSE(arena.allocate(0, 1));
    REQUIRE_FALSE(arena.allocate(1, 0));
    REQUIRE_FALSE(arena.allocate(1, 3));
    REQUIRE(arena.used() == 0);

    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    LinearArenaState overflow_arena{maximum};
    const auto prefix = overflow_arena.allocate(maximum - 3, 1);
    REQUIRE(prefix);
    const auto overflow = overflow_arena.allocate(1, 8);
    REQUIRE_FALSE(overflow);
    REQUIRE(overflow_arena.used() == maximum - 3);
}

TEST_CASE(
    "frame transient storage resets only after its fence retires",
    "[gpu][frame-resources][fence]")
{
    using shark::rhi::d3d12::detail::FrameResourceState;

    FrameResourceState frame{1024, 4, 4};
    const auto first_begin = frame.begin(0);
    REQUIRE(first_begin);
    REQUIRE_FALSE(first_begin.value());
    REQUIRE(frame.generation() == 1);
    REQUIRE(frame.allocate_upload(256, 256));
    REQUIRE(frame.allocate_descriptors(2));
    REQUIRE(frame.allocate_timestamps(4));
    REQUIRE(frame.submit(5));
    REQUIRE(frame.completion_fence_value() == 5);
    REQUIRE(frame.required_wait_fence_value(0) == 5);
    REQUIRE(frame.required_wait_fence_value(4) == 5);
    REQUIRE(frame.required_wait_fence_value(5) == 0);
    REQUIRE(frame.required_wait_fence_value(9) == 0);

    const auto early_begin = frame.begin(4);
    REQUIRE_FALSE(early_begin);
    REQUIRE(frame.upload_used() == 256);
    REQUIRE(frame.descriptor_used() == 2);
    REQUIRE(frame.timestamp_used() == 4);
    REQUIRE(frame.generation() == 1);

    const auto retired_begin = frame.begin(5);
    REQUIRE(retired_begin);
    REQUIRE(retired_begin.value());
    REQUIRE(frame.completion_fence_value() == 0);
    REQUIRE(frame.upload_used() == 0);
    REQUIRE(frame.descriptor_used() == 0);
    REQUIRE(frame.timestamp_used() == 0);
    REQUIRE(frame.generation() == 2);
    REQUIRE(frame.active());
}

TEST_CASE(
    "frame contexts reject invalid lifecycle transitions without mutation",
    "[gpu][frame-resources][fence]")
{
    using shark::rhi::d3d12::detail::FrameResourceState;

    FrameResourceState frame{512, 2, 4};
    REQUIRE_FALSE(frame.allocate_upload(1, 1));
    REQUIRE_FALSE(frame.allocate_descriptors(1));
    REQUIRE_FALSE(frame.allocate_timestamps(1));
    REQUIRE_FALSE(frame.submit(1));
    REQUIRE(frame.generation() == 0);
    REQUIRE(frame.upload_used() == 0);
    REQUIRE(frame.descriptor_used() == 0);
    REQUIRE(frame.timestamp_used() == 0);

    REQUIRE(frame.begin(0));
    REQUIRE(frame.allocate_upload(64, 16));
    REQUIRE(frame.allocate_descriptors(1));
    REQUIRE(frame.allocate_timestamps(4));
    REQUIRE_FALSE(frame.begin(0));
    REQUIRE_FALSE(frame.retire(0));
    REQUIRE(frame.active());
    REQUIRE(frame.generation() == 1);
    REQUIRE(frame.upload_used() == 64);
    REQUIRE(frame.descriptor_used() == 1);
    REQUIRE(frame.timestamp_used() == 4);

    frame.discard_active_after_queue_drain();
    REQUIRE_FALSE(frame.active());
    REQUIRE(frame.completion_fence_value() == 0);
    REQUIRE(frame.upload_used() == 0);
    REQUIRE(frame.descriptor_used() == 0);
    REQUIRE(frame.timestamp_used() == 0);
}

TEST_CASE(
    "frame descriptor allocation is bounded and retirement is idempotent",
    "[gpu][frame-resources][descriptors]")
{
    using shark::rhi::d3d12::detail::FrameResourceState;

    FrameResourceState frame{256, 3, 4};
    REQUIRE(frame.begin(0));
    const auto all_descriptors = frame.allocate_descriptors(3);
    REQUIRE(all_descriptors);
    REQUIRE(all_descriptors.value().offset == 0);
    REQUIRE_FALSE(frame.allocate_descriptors(1));
    REQUIRE(frame.descriptor_used() == 3);
    REQUIRE(frame.submit(11));

    const auto too_early = frame.retire(10);
    REQUIRE_FALSE(too_early);
    REQUIRE(frame.completion_fence_value() == 11);

    const auto retired = frame.retire(11);
    REQUIRE(retired);
    REQUIRE(retired.value());
    REQUIRE(frame.completion_fence_value() == 0);
    REQUIRE(frame.descriptor_used() == 0);

    const auto already_retired = frame.retire(11);
    REQUIRE(already_retired);
    REQUIRE_FALSE(already_retired.value());
}

TEST_CASE(
    "frame completion values and the direct fence timeline are monotonic",
    "[gpu][frame-resources][fence]")
{
    using shark::rhi::d3d12::detail::FenceTimeline;
    using shark::rhi::d3d12::detail::FrameResourceState;

    FenceTimeline timeline{7};
    const auto seven = timeline.issue();
    const auto eight = timeline.issue();
    REQUIRE(seven);
    REQUIRE(eight);
    REQUIRE(seven.value() == 7);
    REQUIRE(eight.value() == 8);
    REQUIRE(timeline.last_issued() == 8);

    FrameResourceState frame{256, 1, 4};
    REQUIRE(frame.begin(0));
    REQUIRE(frame.submit(seven.value()));
    REQUIRE(frame.begin(seven.value()));
    REQUIRE_FALSE(frame.submit(seven.value()));
    REQUIRE(frame.submit(eight.value()));

    FenceTimeline zero{0};
    REQUIRE_FALSE(zero.issue());
    FenceTimeline exhausted{
        std::numeric_limits<std::uint64_t>::max()};
    REQUIRE_FALSE(exhausted.issue());
}

TEST_CASE(
    "frame timestamp allocation is fixed capacity and fence retired",
    "[gpu][frame-resources][timestamps]")
{
    using shark::rhi::d3d12::detail::FrameResourceState;

    FrameResourceState frame{256, 1, 4};
    REQUIRE(frame.begin(0));

    const auto timestamps = frame.allocate_timestamps(4);
    REQUIRE(timestamps);
    REQUIRE(timestamps.value().offset == 0);
    REQUIRE(timestamps.value().size == 4);
    REQUIRE(frame.timestamp_used() == 4);
    REQUIRE(frame.timestamp_high_watermark() == 4);
    REQUIRE_FALSE(frame.allocate_timestamps(1));

    REQUIRE(frame.submit(23));
    REQUIRE_FALSE(frame.begin(22));
    REQUIRE(frame.timestamp_used() == 4);

    const auto reused = frame.begin(23);
    REQUIRE(reused);
    REQUIRE(reused.value());
    REQUIRE(frame.timestamp_used() == 0);
    REQUIRE(frame.timestamp_high_watermark() == 4);
}
