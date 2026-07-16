#pragma once

#include <shark/core/result.hpp>

#include <cstddef>
#include <cstdint>

namespace shark::rhi::d3d12::detail {

struct LinearAllocation final {
    std::size_t offset{};
    std::size_t size{};
};

class LinearArenaState final {
public:
    explicit LinearArenaState(std::size_t capacity) noexcept;

    [[nodiscard]] core::Result<LinearAllocation> allocate(
        std::size_t size,
        std::size_t alignment);
    void reset() noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t used() const noexcept;
    [[nodiscard]] std::size_t high_watermark() const noexcept;

private:
    std::size_t capacity_{};
    std::size_t cursor_{};
    std::size_t high_watermark_{};
};

// Tracks the CPU-owned transient slices associated with one swap-chain back
// buffer. begin() deliberately refuses to reset either slice until the
// context's preceding submission fence is complete.
class FrameResourceState final {
public:
    FrameResourceState(
        std::size_t upload_capacity,
        std::size_t descriptor_capacity) noexcept;

    [[nodiscard]] core::Result<bool> begin(
        std::uint64_t completed_fence_value);
    [[nodiscard]] core::Result<bool> retire(
        std::uint64_t completed_fence_value);
    void discard_active_after_queue_drain() noexcept;
    [[nodiscard]] core::Result<LinearAllocation> allocate_upload(
        std::size_t size,
        std::size_t alignment);
    [[nodiscard]] core::Result<LinearAllocation> allocate_descriptors(
        std::size_t count);
    [[nodiscard]] core::Result<void> submit(
        std::uint64_t completion_fence_value);

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::uint64_t completion_fence_value() const noexcept;
    [[nodiscard]] std::uint64_t required_wait_fence_value(
        std::uint64_t completed_fence_value) const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::size_t upload_used() const noexcept;
    [[nodiscard]] std::size_t upload_high_watermark() const noexcept;
    [[nodiscard]] std::size_t descriptor_used() const noexcept;
    [[nodiscard]] std::size_t descriptor_high_watermark() const noexcept;

private:
    LinearArenaState upload_;
    LinearArenaState descriptors_;
    std::uint64_t completion_fence_value_{};
    std::uint64_t last_submitted_fence_value_{};
    std::uint64_t generation_{};
    bool active_{};
};

// Fence value zero is reserved for "never submitted". issue() returns a
// strictly increasing value and fails before unsigned wraparound.
class FenceTimeline final {
public:
    explicit FenceTimeline(std::uint64_t next_value = 1) noexcept;

    [[nodiscard]] core::Result<std::uint64_t> issue();
    [[nodiscard]] std::uint64_t last_issued() const noexcept;

private:
    std::uint64_t next_value_{};
    std::uint64_t last_issued_{};
};

} // namespace shark::rhi::d3d12::detail
