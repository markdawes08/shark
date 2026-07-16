#include <shark/world/camera.hpp>

#include <shark/core/error.hpp>
#include <shark/core/math.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <limits>

namespace {

[[nodiscard]] float length(
    const shark::math::Float3 value) noexcept
{
    return std::sqrt(
        value.x * value.x +
        value.y * value.y +
        value.z * value.z);
}

[[nodiscard]] float dot(
    const shark::math::Float3 left,
    const shark::math::Float3 right) noexcept
{
    return left.x * right.x +
        left.y * right.y +
        left.z * right.z;
}

[[nodiscard]] shark::math::Float4 project_view_point(
    const shark::math::Float3 point,
    const shark::math::Matrix4x4& projection) noexcept
{
    return shark::math::transform(
        shark::math::Float4{point.x, point.y, point.z, 1.0F},
        projection);
}

} // namespace

TEST_CASE(
    "the default Shark camera looks down negative Z from four meters",
    "[world][camera]")
{
    using namespace shark;

    const world::Camera camera;
    REQUIRE((
        camera.transform.position ==
        math::Float3{0.0F, 0.0F, 4.0F}));
    REQUIRE(camera.lens.near_plane == Catch::Approx(0.1F));
    REQUIRE(camera.lens.far_plane == Catch::Approx(100.0F));

    const auto basis = world::camera_basis(camera.transform);
    REQUIRE(basis.right.x == Catch::Approx(1.0F));
    REQUIRE(basis.right.y == Catch::Approx(0.0F));
    REQUIRE(basis.right.z == Catch::Approx(0.0F));
    REQUIRE(basis.up.x == Catch::Approx(0.0F));
    REQUIRE(basis.up.y == Catch::Approx(1.0F));
    REQUIRE(basis.up.z == Catch::Approx(0.0F));
    REQUIRE(basis.forward.x == Catch::Approx(0.0F));
    REQUIRE(basis.forward.y == Catch::Approx(0.0F));
    REQUIRE(basis.forward.z == Catch::Approx(-1.0F));

    const auto matrices = world::build_camera_matrices(camera, 16.0F / 9.0F);
    REQUIRE(matrices);
    const auto camera_at_origin = math::transform(
        math::Float4{0.0F, 0.0F, 4.0F, 1.0F},
        matrices.value().view);
    REQUIRE(camera_at_origin.x == Catch::Approx(0.0F));
    REQUIRE(camera_at_origin.y == Catch::Approx(0.0F));
    REQUIRE(camera_at_origin.z == Catch::Approx(0.0F));
    REQUIRE(camera_at_origin.w == Catch::Approx(1.0F));

    const auto point_in_front = math::transform(
        math::Float4{0.0F, 0.0F, 3.0F, 1.0F},
        matrices.value().view);
    REQUIRE(point_in_front.z == Catch::Approx(-1.0F));
}

TEST_CASE(
    "camera yaw pitch and movement preserve an orthonormal basis",
    "[world][camera]")
{
    using namespace shark;

    world::Camera camera;
    world::advance_camera(
        camera,
        world::CameraMotion{
            .yaw_radians = math::half_pi,
            .pitch_radians = math::pi / 6.0F,
        },
        0.0F,
        4.0F);

    const auto basis = world::camera_basis(camera.transform);
    REQUIRE(basis.forward.x == Catch::Approx(
        std::cos(math::pi / 6.0F)).margin(0.00001F));
    REQUIRE(basis.forward.y == Catch::Approx(0.5F).margin(0.00001F));
    REQUIRE(basis.forward.z == Catch::Approx(0.0F).margin(0.00001F));
    REQUIRE(length(basis.right) == Catch::Approx(1.0F));
    REQUIRE(length(basis.up) == Catch::Approx(1.0F));
    REQUIRE(length(basis.forward) == Catch::Approx(1.0F));
    REQUIRE(dot(basis.right, basis.up) ==
        Catch::Approx(0.0F).margin(0.00001F));
    REQUIRE(dot(basis.right, basis.forward) ==
        Catch::Approx(0.0F).margin(0.00001F));
    REQUIRE(dot(basis.up, basis.forward) ==
        Catch::Approx(0.0F).margin(0.00001F));

    world::advance_camera(
        camera,
        world::CameraMotion{.forward = 1.0F},
        0.5F,
        2.0F);
    REQUIRE(camera.transform.position.x ==
        Catch::Approx(basis.forward.x).margin(0.00001F));
    REQUIRE(camera.transform.position.y ==
        Catch::Approx(basis.forward.y).margin(0.00001F));
    REQUIRE(camera.transform.position.z ==
        Catch::Approx(4.0F + basis.forward.z).margin(0.00001F));
}

TEST_CASE(
    "camera pitch clamps below vertical and movement normalizes diagonals",
    "[world][camera]")
{
    using namespace shark;

    world::Camera camera;
    world::advance_camera(
        camera,
        world::CameraMotion{.pitch_radians = math::pi},
        0.0F,
        1.0F);
    REQUIRE(camera.transform.pitch_radians ==
        Catch::Approx(world::maximum_camera_pitch));
    REQUIRE(math::is_finite(world::camera_basis(camera.transform).forward));

    camera = world::Camera{};
    world::advance_camera(
        camera,
        world::CameraMotion{
            .right = 1.0F,
            .forward = 1.0F,
        },
        1.0F,
        4.0F);
    const math::Float3 displacement{
        camera.transform.position.x,
        camera.transform.position.y,
        camera.transform.position.z - 4.0F,
    };
    REQUIRE(length(displacement) ==
        Catch::Approx(4.0F).margin(0.00001F));
}

TEST_CASE(
    "camera motion is deterministic across equivalent fixed steps",
    "[world][camera]")
{
    using namespace shark;

    world::Camera single_step;
    world::advance_camera(
        single_step,
        world::CameraMotion{.forward = 1.0F},
        1.0F,
        3.0F);

    world::Camera fixed_steps;
    for (int step = 0; step < 60; ++step) {
        world::advance_camera(
            fixed_steps,
            world::CameraMotion{.forward = 1.0F},
            1.0F / 60.0F,
            3.0F);
    }

    REQUIRE(fixed_steps.transform.position.x ==
        Catch::Approx(single_step.transform.position.x)
            .margin(0.00001F));
    REQUIRE(fixed_steps.transform.position.y ==
        Catch::Approx(single_step.transform.position.y)
            .margin(0.00001F));
    REQUIRE(fixed_steps.transform.position.z ==
        Catch::Approx(single_step.transform.position.z)
            .margin(0.00001F));
}

TEST_CASE(
    "finite reversed-Z projection maps near to one and far to zero",
    "[world][camera][projection]")
{
    using namespace shark;

    const world::Camera camera;
    const auto matrices = world::build_camera_matrices(camera, 2.0F);
    REQUIRE(matrices);

    const auto near_clip = project_view_point(
        math::Float3{0.0F, 0.0F, -camera.lens.near_plane},
        matrices.value().projection);
    const auto far_clip = project_view_point(
        math::Float3{0.0F, 0.0F, -camera.lens.far_plane},
        matrices.value().projection);
    const auto middle_clip = project_view_point(
        math::Float3{0.0F, 0.0F, -10.0F},
        matrices.value().projection);

    REQUIRE(near_clip.w > 0.0F);
    REQUIRE(far_clip.w > 0.0F);
    REQUIRE(near_clip.z / near_clip.w ==
        Catch::Approx(1.0F).margin(0.00001F));
    REQUIRE(far_clip.z / far_clip.w ==
        Catch::Approx(0.0F).margin(0.00001F));
    REQUIRE(middle_clip.z / middle_clip.w > 0.0F);
    REQUIRE(middle_clip.z / middle_clip.w < 1.0F);

    const auto square = world::build_camera_matrices(camera, 1.0F);
    REQUIRE(square);
    REQUIRE(
        matrices.value().projection.elements[0][0] ==
        Catch::Approx(
            square.value().projection.elements[0][0] * 0.5F));
    REQUIRE(
        matrices.value().projection.elements[1][1] ==
        Catch::Approx(square.value().projection.elements[1][1]));
    REQUIRE(
        matrices.value().sky_view_projection.elements[0][0] ==
        Catch::Approx(
            square.value().sky_view_projection.elements[0][0] * 0.5F));
    REQUIRE(
        matrices.value().sky_view_projection.elements[1][1] ==
        Catch::Approx(
            square.value().sky_view_projection.elements[1][1]));

    const math::Float4 world_point{0.0F, 0.0F, 3.0F, 1.0F};
    const auto sequential = math::transform(
        math::transform(world_point, matrices.value().view),
        matrices.value().projection);
    const auto composed = math::transform(
        world_point,
        matrices.value().view_projection);
    REQUIRE(composed.x == Catch::Approx(sequential.x));
    REQUIRE(composed.y == Catch::Approx(sequential.y));
    REQUIRE(composed.z == Catch::Approx(sequential.z));
    REQUIRE(composed.w == Catch::Approx(sequential.w));
}

TEST_CASE(
    "sky view projection ignores camera translation",
    "[world][camera][skybox]")
{
    using namespace shark;

    world::Camera first;
    first.transform.position = {12.0F, -7.0F, 31.0F};
    first.transform.yaw_radians = math::pi / 5.0F;
    first.transform.pitch_radians = -math::pi / 8.0F;

    auto translated = first;
    translated.transform.position = {-900.0F, 250.0F, 0.125F};

    const auto first_matrices = world::build_camera_matrices(
        first,
        16.0F / 9.0F);
    const auto translated_matrices = world::build_camera_matrices(
        translated,
        16.0F / 9.0F);
    REQUIRE(first_matrices);
    REQUIRE(translated_matrices);

    bool ordinary_view_projection_changed = false;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            const auto first_sky = first_matrices.value()
                .sky_view_projection.elements[row][column];
            const auto translated_sky = translated_matrices.value()
                .sky_view_projection.elements[row][column];
            REQUIRE(translated_sky == Catch::Approx(first_sky));

            if (translated_matrices.value()
                    .view_projection.elements[row][column] !=
                first_matrices.value()
                    .view_projection.elements[row][column]) {
                ordinary_view_projection_changed = true;
            }
        }
    }
    REQUIRE(ordinary_view_projection_changed);
}

TEST_CASE(
    "sky view projection follows camera rotation",
    "[world][camera][skybox]")
{
    using namespace shark;

    const auto projects_forward_to_center = [](
        const world::Camera& camera) {
        const auto matrices = world::build_camera_matrices(camera, 1.6F);
        REQUIRE(matrices);
        const auto forward = world::camera_basis(camera.transform).forward;
        const auto clip = math::transform(
            math::Float4{forward.x, forward.y, forward.z, 1.0F},
            matrices.value().sky_view_projection);
        REQUIRE(clip.w > 0.0F);
        REQUIRE(clip.x / clip.w ==
            Catch::Approx(0.0F).margin(0.00001F));
        REQUIRE(clip.y / clip.w ==
            Catch::Approx(0.0F).margin(0.00001F));
        return matrices.value().sky_view_projection;
    };

    world::Camera default_camera;
    const auto default_sky = projects_forward_to_center(default_camera);

    world::Camera rotated_camera;
    rotated_camera.transform.yaw_radians = math::half_pi;
    rotated_camera.transform.pitch_radians = math::pi / 6.0F;
    const auto rotated_sky = projects_forward_to_center(rotated_camera);

    bool rotation_changed_sky = false;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            if (rotated_sky.elements[row][column] !=
                default_sky.elements[row][column]) {
                rotation_changed_sky = true;
            }
        }
    }
    REQUIRE(rotation_changed_sky);
    REQUIRE(math::is_finite(default_sky));
    REQUIRE(math::is_finite(rotated_sky));
}

TEST_CASE(
    "camera matrix construction rejects invalid perspective state",
    "[world][camera][projection]")
{
    using namespace shark;

    world::Camera camera;
    REQUIRE_FALSE(world::build_camera_matrices(camera, 0.0F));
    REQUIRE_FALSE(world::build_camera_matrices(
        camera,
        std::numeric_limits<float>::infinity()));

    camera.lens.vertical_fov_radians = 0.0F;
    REQUIRE_FALSE(world::build_camera_matrices(camera, 1.0F));
    camera = world::Camera{};
    camera.lens.near_plane = 0.0F;
    REQUIRE_FALSE(world::build_camera_matrices(camera, 1.0F));
    camera = world::Camera{};
    camera.lens.far_plane = camera.lens.near_plane;
    const auto invalid_range =
        world::build_camera_matrices(camera, 1.0F);
    REQUIRE_FALSE(invalid_range);
    REQUIRE(invalid_range.error().code() ==
        core::ErrorCode::invalid_argument);
    camera = world::Camera{};
    camera.transform.yaw_radians =
        std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(world::build_camera_matrices(camera, 1.0F));
}
