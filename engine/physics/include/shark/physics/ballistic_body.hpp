#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>

namespace shark::physics {

inline constexpr math::Float3 standard_gravity{
    0.0F,
    -9.81F,
    0.0F,
};

struct BallisticBodyState final {
    math::Float3 position{};
    math::Float3 linear_velocity{};

    [[nodiscard]] friend bool operator==(
        const BallisticBodyState&,
        const BallisticBodyState&) noexcept = default;
};

[[nodiscard]] bool is_valid(
    const BallisticBodyState& state) noexcept;

[[nodiscard]] core::Result<void> advance_ballistic_body(
    BallisticBodyState& state,
    math::Float3 acceleration,
    float fixed_delta_seconds);

[[nodiscard]] core::Result<BallisticBodyState>
interpolate_ballistic_body(
    const BallisticBodyState& previous,
    const BallisticBodyState& current,
    float alpha);

} // namespace shark::physics
