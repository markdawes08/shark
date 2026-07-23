#include <shark/physics/broad_phase.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace shark::physics {
namespace {

[[nodiscard]] core::Error broad_phase_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] bool valid_bounds(
    const BroadPhaseBounds& bounds) noexcept
{
    return math::is_finite(bounds.minimum) &&
        math::is_finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool proxy_less(
    const BroadPhaseProxy& left,
    const BroadPhaseProxy& right) noexcept
{
    if (left.bounds.minimum.x < right.bounds.minimum.x) {
        return true;
    }
    if (right.bounds.minimum.x < left.bounds.minimum.x) {
        return false;
    }
    return left.body_id < right.body_id;
}

[[nodiscard]] bool candidate_less(
    const BroadPhaseCandidate& left,
    const BroadPhaseCandidate& right) noexcept
{
    if (left.first_body_id != right.first_body_id) {
        return left.first_body_id < right.first_body_id;
    }
    return left.second_body_id < right.second_body_id;
}

[[nodiscard]] bool overlaps_remaining_axes(
    const BroadPhaseBounds& first,
    const BroadPhaseBounds& second) noexcept
{
    return first.minimum.y <= second.maximum.y &&
        second.minimum.y <= first.maximum.y &&
        first.minimum.z <= second.maximum.z &&
        second.minimum.z <= first.maximum.z;
}

void sort_proxies(
    std::array<BroadPhaseProxy, broad_phase_proxy_capacity>& proxies,
    const std::size_t proxy_count) noexcept
{
    for (std::size_t index = 1; index < proxy_count; ++index) {
        const auto value = proxies[index];
        auto insertion = index;
        while (insertion > 0U &&
               proxy_less(value, proxies[insertion - 1U])) {
            proxies[insertion] = proxies[insertion - 1U];
            --insertion;
        }
        proxies[insertion] = value;
    }
}

void sort_candidates(BroadPhaseStep& step) noexcept
{
    std::sort(
        step.candidates.begin(),
        step.candidates.begin() + step.candidate_count,
        candidate_less);
}

} // namespace

core::Result<BroadPhaseStep> generate_broad_phase_candidates(
    const std::span<const BroadPhaseProxy> proxies)
{
    if (proxies.size() > broad_phase_proxy_capacity) {
        return core::Result<BroadPhaseStep>::failure(
            broad_phase_error(
                core::ErrorCode::invalid_argument,
                "Broad phase supports at most 64 active proxies"));
    }

    for (std::size_t index = 0; index < proxies.size(); ++index) {
        const auto& proxy = proxies[index];
        if (proxy.body_id == 0U ||
            proxy.body_index ==
                std::numeric_limits<std::size_t>::max() ||
            !valid_bounds(proxy.bounds)) {
            return core::Result<BroadPhaseStep>::failure(
                broad_phase_error(
                    core::ErrorCode::invalid_argument,
                    "Broad-phase proxies require a nonzero body ID, "
                    "a dynamic body index, and finite ordered bounds"));
        }
        for (std::size_t previous = 0;
             previous < index;
             ++previous) {
            if (proxies[previous].body_id == proxy.body_id ||
                proxies[previous].body_index == proxy.body_index) {
                return core::Result<BroadPhaseStep>::failure(
                    broad_phase_error(
                        core::ErrorCode::invalid_argument,
                        "Broad-phase body IDs and body indices must be "
                        "unique within the active proxy set"));
            }
        }
    }

    std::array<BroadPhaseProxy, broad_phase_proxy_capacity>
        sorted_proxies{};
    for (std::size_t index = 0; index < proxies.size(); ++index) {
        sorted_proxies[index] = proxies[index];
    }
    sort_proxies(sorted_proxies, proxies.size());

    const auto proxy_count = proxies.size();
    BroadPhaseStep step{
        .proxy_count = proxy_count,
        .possible_pair_count =
            proxy_count < 2U
            ? 0U
            : proxy_count * (proxy_count - 1U) / 2U,
    };
    for (std::size_t first_index = 0;
         first_index < proxies.size();
         ++first_index) {
        const auto& first = sorted_proxies[first_index];
        for (std::size_t second_index = first_index + 1U;
             second_index < proxies.size();
             ++second_index) {
            const auto& second = sorted_proxies[second_index];
            if (second.bounds.minimum.x >
                first.bounds.maximum.x) {
                break;
            }
            ++step.x_overlap_pair_count;
            if (!overlaps_remaining_axes(
                    first.bounds,
                    second.bounds)) {
                continue;
            }
            if (step.candidate_count >=
                broad_phase_candidate_capacity) {
                return core::Result<BroadPhaseStep>::failure(
                    broad_phase_error(
                        core::ErrorCode::invalid_state,
                        "Broad-phase candidate generation exceeded its "
                        "complete pair capacity"));
            }

            const auto first_is_canonical =
                first.body_id < second.body_id;
            const auto& canonical_first =
                first_is_canonical ? first : second;
            const auto& canonical_second =
                first_is_canonical ? second : first;
            step.candidates[step.candidate_count] =
                BroadPhaseCandidate{
                    .first_body_id = canonical_first.body_id,
                    .second_body_id = canonical_second.body_id,
                    .first_body_index = canonical_first.body_index,
                    .second_body_index = canonical_second.body_index,
                };
            ++step.candidate_count;
        }
    }
    sort_candidates(step);

    return core::Result<BroadPhaseStep>::success(std::move(step));
}

} // namespace shark::physics
