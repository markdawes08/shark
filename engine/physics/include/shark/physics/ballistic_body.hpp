#pragma once

#include <shark/physics/rigid_body.hpp>

namespace shark::physics {

inline constexpr math::Float3 standard_gravity{
    0.0F,
    -9.81F,
    0.0F,
};

using BallisticBodyState = RigidBodyState;

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
