#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace shark::physics {

inline constexpr std::size_t broad_phase_proxy_capacity = 64;
inline constexpr std::size_t broad_phase_candidate_capacity =
    broad_phase_proxy_capacity * (broad_phase_proxy_capacity - 1U) / 2U;

struct BroadPhaseBounds final {
    math::Float3 minimum{};
    math::Float3 maximum{};

    [[nodiscard]] friend bool operator==(
        const BroadPhaseBounds&,
        const BroadPhaseBounds&) noexcept = default;
};

struct BroadPhaseProxy final {
    // The nonzero body ID must change when a caller reuses a body slot for a
    // different lifetime. The current body index is only an execution slot.
    std::uint64_t body_id{};
    std::size_t body_index{};
    BroadPhaseBounds bounds{};

    [[nodiscard]] friend bool operator==(
        const BroadPhaseProxy&,
        const BroadPhaseProxy&) noexcept = default;
};

struct BroadPhaseCandidate final {
    // Endpoints and the complete candidate prefix are lexicographically
    // ordered by stable body ID, independent of proxy input order.
    std::uint64_t first_body_id{};
    std::uint64_t second_body_id{};
    std::size_t first_body_index{};
    std::size_t second_body_index{};

    [[nodiscard]] friend bool operator==(
        const BroadPhaseCandidate&,
        const BroadPhaseCandidate&) noexcept = default;
};

struct BroadPhaseStep final {
    std::array<
        BroadPhaseCandidate,
        broad_phase_candidate_capacity>
        candidates{};
    std::size_t proxy_count{};
    std::size_t possible_pair_count{};
    // Pairs whose closed X intervals overlap and therefore receive Y/Z tests.
    std::size_t x_overlap_pair_count{};
    std::size_t candidate_count{};

    [[nodiscard]] friend bool operator==(
        const BroadPhaseStep&,
        const BroadPhaseStep&) noexcept = default;
};

static_assert(std::is_standard_layout_v<BroadPhaseBounds>);
static_assert(std::is_trivially_copyable_v<BroadPhaseBounds>);
static_assert(std::is_standard_layout_v<BroadPhaseProxy>);
static_assert(std::is_trivially_copyable_v<BroadPhaseProxy>);
static_assert(std::is_standard_layout_v<BroadPhaseCandidate>);
static_assert(std::is_trivially_copyable_v<BroadPhaseCandidate>);
static_assert(std::is_standard_layout_v<BroadPhaseStep>);
static_assert(std::is_trivially_copyable_v<BroadPhaseStep>);

// Rebuilds a bounded sweep-and-prune candidate list without allocation or
// persistent hidden state. The X sweep is fixed for reproducibility. Bounds
// are closed: face, edge, and point touching all remain candidates. Every
// input is validated before generation, and inputs are never mutated.
[[nodiscard]] core::Result<BroadPhaseStep>
generate_broad_phase_candidates(
    std::span<const BroadPhaseProxy> proxies);

} // namespace shark::physics
