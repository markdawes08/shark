#pragma once

#include <cmath>

namespace shark::physics {

struct SphereCollider final {
    float radius{};

    [[nodiscard]] friend bool operator==(
        const SphereCollider&,
        const SphereCollider&) noexcept = default;
};

[[nodiscard]] inline bool is_valid(
    const SphereCollider& collider) noexcept
{
    return std::isfinite(collider.radius) &&
        collider.radius > 0.0F;
}

} // namespace shark::physics
