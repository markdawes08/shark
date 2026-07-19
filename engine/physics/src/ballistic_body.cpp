#include <shark/physics/ballistic_body.hpp>

#include <shark/core/error.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::physics {
namespace {

inline constexpr float maximum_fixed_delta_seconds = 0.25F;

[[nodiscard]] core::Error physics_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] bool representable_float(
    const double value) noexcept
{
    return std::isfinite(value) &&
        std::abs(value) <=
            static_cast<double>(
                std::numeric_limits<float>::max());
}

[[nodiscard]] core::Result<math::Float3> advance_componentwise(
    const math::Float3 value,
    const math::Float3 rate,
    const double delta_seconds)
{
    const std::array<double, 3> advanced{
        static_cast<double>(value.x) +
            static_cast<double>(rate.x) * delta_seconds,
        static_cast<double>(value.y) +
            static_cast<double>(rate.y) * delta_seconds,
        static_cast<double>(value.z) +
            static_cast<double>(rate.z) * delta_seconds,
    };
    for (const auto component : advanced) {
        if (!representable_float(component)) {
            return core::Result<math::Float3>::failure(
                physics_error(
                    core::ErrorCode::unavailable,
                    "Ballistic integration exceeded finite float range"));
        }
    }
    return core::Result<math::Float3>::success({
        static_cast<float>(advanced[0]),
        static_cast<float>(advanced[1]),
        static_cast<float>(advanced[2]),
    });
}

[[nodiscard]] core::Result<math::Float3> interpolate_componentwise(
    const math::Float3 first,
    const math::Float3 second,
    const double alpha)
{
    const std::array<double, 3> interpolated{
        static_cast<double>(first.x) +
            (static_cast<double>(second.x) -
             static_cast<double>(first.x)) * alpha,
        static_cast<double>(first.y) +
            (static_cast<double>(second.y) -
             static_cast<double>(first.y)) * alpha,
        static_cast<double>(first.z) +
            (static_cast<double>(second.z) -
             static_cast<double>(first.z)) * alpha,
    };
    for (const auto component : interpolated) {
        if (!representable_float(component)) {
            return core::Result<math::Float3>::failure(
                physics_error(
                    core::ErrorCode::unavailable,
                    "Ballistic interpolation exceeded finite float range"));
        }
    }
    return core::Result<math::Float3>::success({
        static_cast<float>(interpolated[0]),
        static_cast<float>(interpolated[1]),
        static_cast<float>(interpolated[2]),
    });
}

} // namespace

bool is_valid(const BallisticBodyState& state) noexcept
{
    return math::is_finite(state.position) &&
        math::is_finite(state.linear_velocity);
}

core::Result<void> advance_ballistic_body(
    BallisticBodyState& state,
    const math::Float3 acceleration,
    const float fixed_delta_seconds)
{
    if (!is_valid(state) ||
        !math::is_finite(acceleration) ||
        !std::isfinite(fixed_delta_seconds) ||
        fixed_delta_seconds <= 0.0F ||
        fixed_delta_seconds > maximum_fixed_delta_seconds) {
        return core::Result<void>::failure(physics_error(
            core::ErrorCode::invalid_argument,
            "Ballistic integration requires finite state/acceleration "
            "and a fixed delta in (0, 0.25] seconds"));
    }

    auto velocity_result = advance_componentwise(
        state.linear_velocity,
        acceleration,
        fixed_delta_seconds);
    if (!velocity_result) {
        return core::Result<void>::failure(
            std::move(velocity_result).error());
    }
    auto position_result = advance_componentwise(
        state.position,
        velocity_result.value(),
        fixed_delta_seconds);
    if (!position_result) {
        return core::Result<void>::failure(
            std::move(position_result).error());
    }

    state = BallisticBodyState{
        .position = position_result.value(),
        .linear_velocity = velocity_result.value(),
    };
    return core::Result<void>::success();
}

core::Result<BallisticBodyState> interpolate_ballistic_body(
    const BallisticBodyState& previous,
    const BallisticBodyState& current,
    const float alpha)
{
    if (!is_valid(previous) ||
        !is_valid(current) ||
        !std::isfinite(alpha) ||
        alpha < 0.0F ||
        alpha > 1.0F) {
        return core::Result<BallisticBodyState>::failure(
            physics_error(
                core::ErrorCode::invalid_argument,
                "Ballistic interpolation requires finite states and "
                "alpha in [0, 1]"));
    }

    auto position_result = interpolate_componentwise(
        previous.position,
        current.position,
        alpha);
    if (!position_result) {
        return core::Result<BallisticBodyState>::failure(
            std::move(position_result).error());
    }
    auto velocity_result = interpolate_componentwise(
        previous.linear_velocity,
        current.linear_velocity,
        alpha);
    if (!velocity_result) {
        return core::Result<BallisticBodyState>::failure(
            std::move(velocity_result).error());
    }
    return core::Result<BallisticBodyState>::success(
        BallisticBodyState{
            .position = position_result.value(),
            .linear_velocity = velocity_result.value(),
        });
}

} // namespace shark::physics
