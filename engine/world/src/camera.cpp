#include <shark/world/camera.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace shark::world {
namespace {

[[nodiscard]] constexpr math::Float3 add(
    const math::Float3 left,
    const math::Float3 right) noexcept
{
    return math::Float3{
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

[[nodiscard]] constexpr math::Float3 scale(
    const math::Float3 value,
    const float factor) noexcept
{
    return math::Float3{
        value.x * factor,
        value.y * factor,
        value.z * factor,
    };
}

[[nodiscard]] constexpr float dot(
    const math::Float3 left,
    const math::Float3 right) noexcept
{
    return left.x * right.x +
        left.y * right.y +
        left.z * right.z;
}

[[nodiscard]] constexpr math::Float3 cross(
    const math::Float3 left,
    const math::Float3 right) noexcept
{
    return math::Float3{
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] math::Float3 normalized(
    const math::Float3 value) noexcept
{
    const auto length_squared = dot(value, value);
    if (!std::isfinite(length_squared) ||
        length_squared <= 0.0F) {
        return {};
    }

    return scale(value, 1.0F / std::sqrt(length_squared));
}

[[nodiscard]] core::Error camera_error(std::string message)
{
    return core::Error{
        core::ErrorCategory::core,
        core::ErrorCode::invalid_argument,
        std::move(message),
    };
}

[[nodiscard]] bool valid_camera(const Camera& camera) noexcept
{
    return math::is_finite(camera.transform.position) &&
        std::isfinite(camera.transform.yaw_radians) &&
        std::isfinite(camera.transform.pitch_radians) &&
        std::isfinite(camera.lens.vertical_fov_radians) &&
        std::isfinite(camera.lens.near_plane) &&
        std::isfinite(camera.lens.far_plane) &&
        camera.lens.vertical_fov_radians > 0.0F &&
        camera.lens.vertical_fov_radians < math::pi &&
        camera.lens.near_plane > 0.0F &&
        camera.lens.far_plane > camera.lens.near_plane;
}

[[nodiscard]] math::Matrix4x4 build_view_matrix(
    const CameraTransform& transform,
    const CameraBasis basis) noexcept
{
    const auto backward = scale(basis.forward, -1.0F);
    return math::Matrix4x4{{
        {basis.right.x, basis.up.x, backward.x, 0.0F},
        {basis.right.y, basis.up.y, backward.y, 0.0F},
        {basis.right.z, basis.up.z, backward.z, 0.0F},
        {
            -dot(transform.position, basis.right),
            -dot(transform.position, basis.up),
            -dot(transform.position, backward),
            1.0F,
        },
    }};
}

[[nodiscard]] math::Matrix4x4 build_projection_matrix(
    const PerspectiveLens& lens,
    const float aspect_ratio) noexcept
{
    const auto vertical_scale =
        1.0F / std::tan(lens.vertical_fov_radians * 0.5F);
    const auto horizontal_scale = vertical_scale / aspect_ratio;
    const auto range = lens.far_plane - lens.near_plane;
    const auto depth_scale = lens.near_plane / range;
    const auto depth_translation =
        lens.near_plane * lens.far_plane / range;

    return math::Matrix4x4{{
        {horizontal_scale, 0.0F, 0.0F, 0.0F},
        {0.0F, vertical_scale, 0.0F, 0.0F},
        {0.0F, 0.0F, depth_scale, -1.0F},
        {0.0F, 0.0F, depth_translation, 0.0F},
    }};
}

} // namespace

CameraBasis camera_basis(
    const CameraTransform& transform) noexcept
{
    const auto cosine_pitch = std::cos(transform.pitch_radians);
    const auto forward = normalized(math::Float3{
        std::sin(transform.yaw_radians) * cosine_pitch,
        std::sin(transform.pitch_radians),
        -std::cos(transform.yaw_radians) * cosine_pitch,
    });
    constexpr math::Float3 world_up{0.0F, 1.0F, 0.0F};
    const auto right = normalized(cross(forward, world_up));
    const auto up = normalized(cross(right, forward));
    return CameraBasis{right, up, forward};
}

core::Result<CameraMatrices> build_camera_matrices(
    const Camera& camera,
    const float aspect_ratio)
{
    if (!std::isfinite(aspect_ratio) || aspect_ratio <= 0.0F) {
        return core::Result<CameraMatrices>::failure(camera_error(
            "Camera aspect ratio must be finite and greater than zero"));
    }
    if (!valid_camera(camera)) {
        return core::Result<CameraMatrices>::failure(camera_error(
            "Camera transform and finite perspective lens are invalid"));
    }
    if (std::abs(camera.transform.pitch_radians) >
        maximum_camera_pitch) {
        return core::Result<CameraMatrices>::failure(camera_error(
            "Camera pitch exceeds the supported free-look limit"));
    }

    const auto basis = camera_basis(camera.transform);
    const auto view = build_view_matrix(camera.transform, basis);
    const auto projection = build_projection_matrix(
        camera.lens,
        aspect_ratio);
    const auto view_projection = math::multiply(view, projection);
    auto sky_view = view;
    sky_view.elements[3][0] = 0.0F;
    sky_view.elements[3][1] = 0.0F;
    sky_view.elements[3][2] = 0.0F;
    const auto sky_view_projection = math::multiply(
        sky_view,
        projection);
    if (!math::is_finite(view) ||
        !math::is_finite(projection) ||
        !math::is_finite(view_projection) ||
        !math::is_finite(sky_view_projection)) {
        return core::Result<CameraMatrices>::failure(camera_error(
            "Camera matrix construction produced non-finite values"));
    }

    return core::Result<CameraMatrices>::success(CameraMatrices{
        view,
        projection,
        view_projection,
        sky_view_projection,
    });
}

void advance_camera(
    Camera& camera,
    const CameraMotion& motion,
    const float delta_seconds,
    const float movement_speed) noexcept
{
    if (std::isfinite(motion.yaw_radians)) {
        camera.transform.yaw_radians = std::remainder(
            camera.transform.yaw_radians + motion.yaw_radians,
            math::two_pi);
    }
    if (std::isfinite(motion.pitch_radians)) {
        camera.transform.pitch_radians = std::clamp(
            camera.transform.pitch_radians + motion.pitch_radians,
            -maximum_camera_pitch,
            maximum_camera_pitch);
    }

    if (!std::isfinite(delta_seconds) ||
        !std::isfinite(movement_speed) ||
        delta_seconds <= 0.0F ||
        movement_speed <= 0.0F) {
        return;
    }

    const auto basis = camera_basis(camera.transform);
    constexpr math::Float3 world_up{0.0F, 1.0F, 0.0F};
    auto direction = add(
        scale(basis.right, motion.right),
        scale(world_up, motion.up));
    direction = add(direction, scale(basis.forward, motion.forward));
    direction = normalized(direction);
    if (direction == math::Float3{}) {
        return;
    }

    camera.transform.position = add(
        camera.transform.position,
        scale(direction, movement_speed * delta_seconds));
}

} // namespace shark::world
