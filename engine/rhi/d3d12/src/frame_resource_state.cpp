#include "frame_resource_state.hpp"

#include <shark/core/error.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace shark::rhi::d3d12::detail {
namespace {

[[nodiscard]] core::Error frame_resource_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::graphics,
        code,
        std::move(message),
    };
}

[[nodiscard]] bool is_power_of_two(const std::size_t value) noexcept
{
    return value != 0 && (value & (value - 1)) == 0;
}

} // namespace

LinearArenaState::LinearArenaState(const std::size_t capacity) noexcept
    : capacity_(capacity)
{
}

core::Result<LinearAllocation> LinearArenaState::allocate(
    const std::size_t size,
    const std::size_t alignment)
{
    if (size == 0) {
        return core::Result<LinearAllocation>::failure(
            frame_resource_error(
                core::ErrorCode::invalid_argument,
                "A transient allocation must contain at least one unit"));
    }
    if (!is_power_of_two(alignment)) {
        return core::Result<LinearAllocation>::failure(
            frame_resource_error(
                core::ErrorCode::invalid_argument,
                "Transient allocation alignment must be a nonzero power "
                "of two"));
    }

    const auto alignment_mask = alignment - 1;
    if (cursor_ >
        std::numeric_limits<std::size_t>::max() - alignment_mask) {
        return core::Result<LinearAllocation>::failure(
            frame_resource_error(
                core::ErrorCode::unavailable,
                "Transient allocation alignment would overflow"));
    }
    const auto aligned_cursor =
        (cursor_ + alignment_mask) & ~alignment_mask;
    if (aligned_cursor > capacity_ ||
        size > capacity_ - aligned_cursor) {
        return core::Result<LinearAllocation>::failure(
            frame_resource_error(
                core::ErrorCode::unavailable,
                "Transient arena capacity exhausted"));
    }

    const LinearAllocation allocation{aligned_cursor, size};
    cursor_ = aligned_cursor + size;
    high_watermark_ = std::max(high_watermark_, cursor_);
    return core::Result<LinearAllocation>::success(allocation);
}

void LinearArenaState::reset() noexcept
{
    cursor_ = 0;
}

std::size_t LinearArenaState::capacity() const noexcept
{
    return capacity_;
}

std::size_t LinearArenaState::used() const noexcept
{
    return cursor_;
}

std::size_t LinearArenaState::high_watermark() const noexcept
{
    return high_watermark_;
}

FrameResourceState::FrameResourceState(
    const std::size_t upload_capacity,
    const std::size_t descriptor_capacity) noexcept
    : upload_(upload_capacity)
    , descriptors_(descriptor_capacity)
{
}

core::Result<bool> FrameResourceState::begin(
    const std::uint64_t completed_fence_value)
{
    if (active_) {
        return core::Result<bool>::failure(frame_resource_error(
            core::ErrorCode::invalid_state,
            "A frame context cannot begin while already active"));
    }
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        return core::Result<bool>::failure(frame_resource_error(
            core::ErrorCode::unavailable,
            "The frame-context generation counter is exhausted"));
    }

    auto retire_result = retire(completed_fence_value);
    if (!retire_result) {
        return core::Result<bool>::failure(
            std::move(retire_result).error());
    }

    upload_.reset();
    descriptors_.reset();
    ++generation_;
    active_ = true;
    return core::Result<bool>::success(retire_result.value());
}

core::Result<bool> FrameResourceState::retire(
    const std::uint64_t completed_fence_value)
{
    if (active_) {
        return core::Result<bool>::failure(frame_resource_error(
            core::ErrorCode::invalid_state,
            "An active frame context cannot be retired"));
    }
    if (completion_fence_value_ == 0) {
        return core::Result<bool>::success(false);
    }
    if (completed_fence_value < completion_fence_value_) {
        return core::Result<bool>::failure(frame_resource_error(
            core::ErrorCode::invalid_state,
            "A frame context cannot be reset before its completion fence"));
    }

    completion_fence_value_ = 0;
    upload_.reset();
    descriptors_.reset();
    return core::Result<bool>::success(true);
}

void FrameResourceState::discard_active_after_queue_drain() noexcept
{
    active_ = false;
    completion_fence_value_ = 0;
    upload_.reset();
    descriptors_.reset();
}

core::Result<LinearAllocation> FrameResourceState::allocate_upload(
    const std::size_t size,
    const std::size_t alignment)
{
    if (!active_) {
        return core::Result<LinearAllocation>::failure(
            frame_resource_error(
                core::ErrorCode::invalid_state,
                "Upload storage can only be allocated by an active frame"));
    }
    return upload_.allocate(size, alignment);
}

core::Result<LinearAllocation> FrameResourceState::allocate_descriptors(
    const std::size_t count)
{
    if (!active_) {
        return core::Result<LinearAllocation>::failure(
            frame_resource_error(
                core::ErrorCode::invalid_state,
                "Descriptors can only be allocated by an active frame"));
    }
    return descriptors_.allocate(count, 1);
}

core::Result<void> FrameResourceState::submit(
    const std::uint64_t completion_fence_value)
{
    if (!active_) {
        return core::Result<void>::failure(frame_resource_error(
            core::ErrorCode::invalid_state,
            "An inactive frame context cannot be submitted"));
    }
    if (completion_fence_value == 0 ||
        completion_fence_value <= last_submitted_fence_value_) {
        return core::Result<void>::failure(frame_resource_error(
            core::ErrorCode::invalid_argument,
            "Frame completion fence values must be nonzero and monotonic"));
    }

    completion_fence_value_ = completion_fence_value;
    last_submitted_fence_value_ = completion_fence_value;
    active_ = false;
    return core::Result<void>::success();
}

bool FrameResourceState::active() const noexcept
{
    return active_;
}

std::uint64_t FrameResourceState::completion_fence_value() const noexcept
{
    return completion_fence_value_;
}

std::uint64_t FrameResourceState::required_wait_fence_value(
    const std::uint64_t completed_fence_value) const noexcept
{
    return completion_fence_value_ != 0 &&
        completed_fence_value < completion_fence_value_
        ? completion_fence_value_
        : 0;
}

std::uint64_t FrameResourceState::generation() const noexcept
{
    return generation_;
}

std::size_t FrameResourceState::upload_used() const noexcept
{
    return upload_.used();
}

std::size_t FrameResourceState::upload_high_watermark() const noexcept
{
    return upload_.high_watermark();
}

std::size_t FrameResourceState::descriptor_used() const noexcept
{
    return descriptors_.used();
}

std::size_t FrameResourceState::descriptor_high_watermark() const noexcept
{
    return descriptors_.high_watermark();
}

FenceTimeline::FenceTimeline(const std::uint64_t next_value) noexcept
    : next_value_(next_value)
    , last_issued_(next_value == 0 ? 0 : next_value - 1)
{
}

core::Result<std::uint64_t> FenceTimeline::issue()
{
    if (next_value_ == 0 ||
        next_value_ == std::numeric_limits<std::uint64_t>::max()) {
        return core::Result<std::uint64_t>::failure(frame_resource_error(
            core::ErrorCode::unavailable,
            "The direct-queue fence timeline is exhausted"));
    }

    const auto value = next_value_;
    ++next_value_;
    last_issued_ = value;
    return core::Result<std::uint64_t>::success(value);
}

std::uint64_t FenceTimeline::last_issued() const noexcept
{
    return last_issued_;
}

} // namespace shark::rhi::d3d12::detail
