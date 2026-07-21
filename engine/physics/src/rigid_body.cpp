#include <shark/physics/rigid_body.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace shark::physics {
namespace {

inline constexpr float maximum_fixed_delta_seconds = 0.25F;
inline constexpr double slerp_linear_threshold = 0.9995;

struct DoubleQuaternion final {
    double x{};
    double y{};
    double z{};
    double w{1.0};
};

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

[[nodiscard]] bool positive_representable_float(
    const double value) noexcept
{
    return representable_float(value) &&
        value > 0.0 &&
        static_cast<float>(value) > 0.0F;
}

[[nodiscard]] DoubleQuaternion to_double(
    const math::Quaternion value) noexcept
{
    return {
        value.x,
        value.y,
        value.z,
        value.w,
    };
}

[[nodiscard]] DoubleQuaternion multiply(
    const DoubleQuaternion left,
    const DoubleQuaternion right) noexcept
{
    return {
        left.w * right.x + left.x * right.w +
            left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z +
            left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y -
            left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x -
            left.y * right.y - left.z * right.z,
    };
}

[[nodiscard]] core::Result<math::Quaternion>
normalize_quaternion(const DoubleQuaternion value)
{
    const auto length_squared =
        value.x * value.x +
        value.y * value.y +
        value.z * value.z +
        value.w * value.w;
    if (!std::isfinite(length_squared) ||
        length_squared <= 0.0) {
        return core::Result<math::Quaternion>::failure(
            physics_error(
                core::ErrorCode::unavailable,
                "Quaternion normalization exceeded finite range"));
    }

    const auto inverse_length =
        1.0 / std::sqrt(length_squared);
    const std::array<double, 4> normalized{
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
        value.w * inverse_length,
    };
    for (const auto component : normalized) {
        if (!representable_float(component)) {
            return core::Result<math::Quaternion>::failure(
                physics_error(
                    core::ErrorCode::unavailable,
                    "Quaternion normalization exceeded finite range"));
        }
    }

    const math::Quaternion result{
        static_cast<float>(normalized[0]),
        static_cast<float>(normalized[1]),
        static_cast<float>(normalized[2]),
        static_cast<float>(normalized[3]),
    };
    if (!math::is_unit(result)) {
        return core::Result<math::Quaternion>::failure(
            physics_error(
                core::ErrorCode::unavailable,
                "Quaternion normalization lost unit length"));
    }
    return core::Result<math::Quaternion>::success(result);
}

[[nodiscard]] core::Result<math::Float3> advance_componentwise(
    const math::Float3 value,
    const math::Float3 rate,
    const double scale)
{
    const std::array<double, 3> advanced{
        static_cast<double>(value.x) +
            static_cast<double>(rate.x) * scale,
        static_cast<double>(value.y) +
            static_cast<double>(rate.y) * scale,
        static_cast<double>(value.z) +
            static_cast<double>(rate.z) * scale,
    };
    for (const auto component : advanced) {
        if (!representable_float(component)) {
            return core::Result<math::Float3>::failure(
                physics_error(
                    core::ErrorCode::unavailable,
                    "Angular integration exceeded finite float range"));
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
            (static_cast<double>(second.x) - first.x) * alpha,
        static_cast<double>(first.y) +
            (static_cast<double>(second.y) - first.y) * alpha,
        static_cast<double>(first.z) +
            (static_cast<double>(second.z) - first.z) * alpha,
    };
    for (const auto component : interpolated) {
        if (!representable_float(component)) {
            return core::Result<math::Float3>::failure(
                physics_error(
                    core::ErrorCode::unavailable,
                    "Rigid-body interpolation exceeded finite range"));
        }
    }
    return core::Result<math::Float3>::success({
        static_cast<float>(interpolated[0]),
        static_cast<float>(interpolated[1]),
        static_cast<float>(interpolated[2]),
    });
}

[[nodiscard]] core::Result<math::Quaternion>
interpolate_orientation(
    const math::Quaternion first,
    const math::Quaternion second,
    const double alpha)
{
    auto first_result = normalize_quaternion(to_double(first));
    if (!first_result) {
        return core::Result<math::Quaternion>::failure(
            std::move(first_result).error());
    }
    auto second_result = normalize_quaternion(to_double(second));
    if (!second_result) {
        return core::Result<math::Quaternion>::failure(
            std::move(second_result).error());
    }

    auto left = to_double(first_result.value());
    auto right = to_double(second_result.value());
    auto dot =
        left.x * right.x +
        left.y * right.y +
        left.z * right.z +
        left.w * right.w;
    if (dot < 0.0) {
        right.x = -right.x;
        right.y = -right.y;
        right.z = -right.z;
        right.w = -right.w;
        dot = -dot;
    }
    dot = std::clamp(dot, 0.0, 1.0);

    double first_weight = 1.0 - alpha;
    double second_weight = alpha;
    if (dot < slerp_linear_threshold) {
        const auto angle = std::acos(dot);
        const auto sine = std::sin(angle);
        first_weight =
            std::sin((1.0 - alpha) * angle) / sine;
        second_weight = std::sin(alpha * angle) / sine;
    }

    return normalize_quaternion({
        left.x * first_weight + right.x * second_weight,
        left.y * first_weight + right.y * second_weight,
        left.z * first_weight + right.z * second_weight,
        left.w * first_weight + right.w * second_weight,
    });
}

} // namespace

bool is_valid(const RigidBodyState& state) noexcept
{
    return math::is_finite(state.position) &&
        math::is_unit(state.orientation) &&
        math::is_finite(state.linear_velocity) &&
        math::is_finite(state.angular_velocity);
}

bool is_valid(
    const SolidSphereMassProperties& properties) noexcept
{
    if (!std::isfinite(properties.mass) ||
        properties.mass <= 0.0F ||
        !std::isfinite(properties.inverse_mass) ||
        properties.inverse_mass <= 0.0F ||
        !std::isfinite(properties.radius) ||
        properties.radius <= 0.0F ||
        !std::isfinite(properties.moment_of_inertia) ||
        properties.moment_of_inertia <= 0.0F ||
        !std::isfinite(properties.inverse_moment_of_inertia) ||
        properties.inverse_moment_of_inertia <= 0.0F) {
        return false;
    }

    const auto inverse_mass =
        1.0 / static_cast<double>(properties.mass);
    const auto moment_of_inertia =
        0.4 * static_cast<double>(properties.mass) *
        static_cast<double>(properties.radius) *
        properties.radius;
    const auto inverse_moment_of_inertia =
        1.0 / moment_of_inertia;
    return positive_representable_float(inverse_mass) &&
        positive_representable_float(moment_of_inertia) &&
        positive_representable_float(inverse_moment_of_inertia) &&
        properties.inverse_mass ==
            static_cast<float>(inverse_mass) &&
        properties.moment_of_inertia ==
            static_cast<float>(moment_of_inertia) &&
        properties.inverse_moment_of_inertia ==
            static_cast<float>(inverse_moment_of_inertia);
}

core::Result<SolidSphereMassProperties>
make_solid_sphere_mass_properties(
    const float mass,
    const float radius)
{
    if (!std::isfinite(mass) ||
        mass <= 0.0F ||
        !std::isfinite(radius) ||
        radius <= 0.0F) {
        return core::Result<
            SolidSphereMassProperties>::failure(
                physics_error(
                    core::ErrorCode::invalid_argument,
                    "Solid sphere requires finite positive mass and radius"));
    }

    const auto inverse_mass = 1.0 / static_cast<double>(mass);
    const auto moment_of_inertia =
        0.4 * static_cast<double>(mass) *
        static_cast<double>(radius) * radius;
    const auto inverse_moment_of_inertia =
        1.0 / moment_of_inertia;
    if (!positive_representable_float(inverse_mass) ||
        !positive_representable_float(moment_of_inertia) ||
        !positive_representable_float(inverse_moment_of_inertia)) {
        return core::Result<
            SolidSphereMassProperties>::failure(
                physics_error(
                    core::ErrorCode::unavailable,
                    "Solid-sphere mass properties exceeded finite float range"));
    }

    return core::Result<SolidSphereMassProperties>::success({
        .mass = mass,
        .inverse_mass = static_cast<float>(inverse_mass),
        .radius = radius,
        .moment_of_inertia =
            static_cast<float>(moment_of_inertia),
        .inverse_moment_of_inertia =
            static_cast<float>(inverse_moment_of_inertia),
    });
}

core::Result<void> advance_rigid_body_angular_motion(
    RigidBodyState& state,
    const SolidSphereMassProperties& properties,
    const math::Float3 torque,
    const float fixed_delta_seconds)
{
    if (!is_valid(state) ||
        !is_valid(properties) ||
        !math::is_finite(torque) ||
        !std::isfinite(fixed_delta_seconds) ||
        fixed_delta_seconds <= 0.0F ||
        fixed_delta_seconds > maximum_fixed_delta_seconds) {
        return core::Result<void>::failure(
            physics_error(
                core::ErrorCode::invalid_argument,
                "Angular integration requires valid finite inputs "
                "and a fixed delta in (0, 0.25] seconds"));
    }

    const math::Float3 angular_acceleration{
        torque.x * properties.inverse_moment_of_inertia,
        torque.y * properties.inverse_moment_of_inertia,
        torque.z * properties.inverse_moment_of_inertia,
    };
    if (!math::is_finite(angular_acceleration)) {
        return core::Result<void>::failure(
            physics_error(
                core::ErrorCode::unavailable,
                "Angular acceleration exceeded finite float range"));
    }
    auto velocity_result = advance_componentwise(
        state.angular_velocity,
        angular_acceleration,
        fixed_delta_seconds);
    if (!velocity_result) {
        return core::Result<void>::failure(
            std::move(velocity_result).error());
    }

    const auto angular_velocity = velocity_result.value();
    const auto speed_squared =
        static_cast<double>(angular_velocity.x) * angular_velocity.x +
        static_cast<double>(angular_velocity.y) * angular_velocity.y +
        static_cast<double>(angular_velocity.z) * angular_velocity.z;
    const auto speed = std::sqrt(speed_squared);
    DoubleQuaternion increment;
    if (speed > 0.0) {
        const auto half_angle =
            0.5 * speed * fixed_delta_seconds;
        const auto sine = std::sin(half_angle);
        const auto scale = sine / speed;
        increment = {
            angular_velocity.x * scale,
            angular_velocity.y * scale,
            angular_velocity.z * scale,
            std::cos(half_angle),
        };
    }
    auto orientation_result = normalize_quaternion(
        multiply(increment, to_double(state.orientation)));
    if (!orientation_result) {
        return core::Result<void>::failure(
            std::move(orientation_result).error());
    }

    auto candidate = state;
    candidate.orientation = orientation_result.value();
    candidate.angular_velocity = angular_velocity;
    if (!is_valid(candidate)) {
        return core::Result<void>::failure(
            physics_error(
                core::ErrorCode::unavailable,
                "Angular integration produced an invalid state"));
    }
    state = candidate;
    return core::Result<void>::success();
}

core::Result<RigidBodyState> interpolate_rigid_body(
    const RigidBodyState& previous,
    const RigidBodyState& current,
    const float alpha)
{
    if (!is_valid(previous) ||
        !is_valid(current) ||
        !std::isfinite(alpha) ||
        alpha < 0.0F ||
        alpha > 1.0F) {
        return core::Result<RigidBodyState>::failure(
            physics_error(
                core::ErrorCode::invalid_argument,
                "Rigid-body interpolation requires valid states "
                "and alpha in [0, 1]"));
    }

    auto position_result = interpolate_componentwise(
        previous.position,
        current.position,
        alpha);
    if (!position_result) {
        return core::Result<RigidBodyState>::failure(
            std::move(position_result).error());
    }
    auto linear_velocity_result = interpolate_componentwise(
        previous.linear_velocity,
        current.linear_velocity,
        alpha);
    if (!linear_velocity_result) {
        return core::Result<RigidBodyState>::failure(
            std::move(linear_velocity_result).error());
    }
    auto angular_velocity_result = interpolate_componentwise(
        previous.angular_velocity,
        current.angular_velocity,
        alpha);
    if (!angular_velocity_result) {
        return core::Result<RigidBodyState>::failure(
            std::move(angular_velocity_result).error());
    }
    auto orientation_result = interpolate_orientation(
        previous.orientation,
        current.orientation,
        alpha);
    if (!orientation_result) {
        return core::Result<RigidBodyState>::failure(
            std::move(orientation_result).error());
    }

    return core::Result<RigidBodyState>::success({
        .position = position_result.value(),
        .orientation = orientation_result.value(),
        .linear_velocity = linear_velocity_result.value(),
        .angular_velocity = angular_velocity_result.value(),
    });
}

} // namespace shark::physics
