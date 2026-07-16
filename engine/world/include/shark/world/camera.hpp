#pragma once

#include <shark/core/math.hpp>
#include <shark/core/result.hpp>

namespace shark::world {

inline constexpr float maximum_camera_pitch =
    math::half_pi - 0.001F;

struct CameraTransform final {
    math::Float3 position{0.0F, 0.0F, 4.0F};
    float yaw_radians{};
    float pitch_radians{};
};

struct PerspectiveLens final {
    float vertical_fov_radians{math::pi / 3.0F};
    float near_plane{0.1F};
    float far_plane{100.0F};
};

struct Camera final {
    CameraTransform transform;
    PerspectiveLens lens;
};

struct CameraBasis final {
    math::Float3 right;
    math::Float3 up;
    math::Float3 forward;
};

struct CameraMatrices final {
    math::Matrix4x4 view;
    math::Matrix4x4 projection;
    math::Matrix4x4 view_projection;
    math::Matrix4x4 sky_view_projection;
};

struct CameraMotion final {
    float right{};
    float up{};
    float forward{};
    float yaw_radians{};
    float pitch_radians{};
};

[[nodiscard]] CameraBasis camera_basis(
    const CameraTransform& transform) noexcept;

[[nodiscard]] core::Result<CameraMatrices> build_camera_matrices(
    const Camera& camera,
    float aspect_ratio);

void advance_camera(
    Camera& camera,
    const CameraMotion& motion,
    float delta_seconds,
    float movement_speed) noexcept;

} // namespace shark::world
