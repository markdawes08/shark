#include "environment_scene_data.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace {

void require_float3(
    const shark::math::Float3 actual,
    const shark::math::Float3 expected,
    const float margin = 0.00001F)
{
    REQUIRE(actual.x == Catch::Approx(expected.x).margin(margin));
    REQUIRE(actual.y == Catch::Approx(expected.y).margin(margin));
    REQUIRE(actual.z == Catch::Approx(expected.z).margin(margin));
}

void require_unit(const shark::math::Float3 value)
{
    const auto length_squared =
        value.x * value.x +
        value.y * value.y +
        value.z * value.z;
    REQUIRE(length_squared ==
        Catch::Approx(1.0F).margin(0.00001F));
}

} // namespace

TEST_CASE(
    "material sphere proof geometry is bounded and smooth shaded",
    "[renderer][d3d12][environment][sphere]")
{
    using namespace shark::renderer::d3d12::detail;

    STATIC_REQUIRE(material_sphere_slices == 24);
    STATIC_REQUIRE(material_sphere_rings == 11);
    STATIC_REQUIRE(material_sphere_vertex_count == 266);
    STATIC_REQUIRE(material_sphere_index_count == 1'584);
    STATIC_REQUIRE(
        material_sphere_transform_root_parameter == 3);
    STATIC_REQUIRE(
        material_sphere_transform_root_constant_count == 9);
    STATIC_REQUIRE(
        sizeof(MaterialSphereTransformRootConstants) == 36);
    STATIC_REQUIRE(sizeof(EnvironmentVertex) == sizeof(float) * 6U);

    const shark::math::Quaternion identity_orientation{};
    const auto identity_transform = make_material_sphere_transform(
        identity_orientation,
        material_sphere_center);
    REQUIRE(identity_transform.orientation.x == 0.0F);
    REQUIRE(identity_transform.orientation.y == 0.0F);
    REQUIRE(identity_transform.orientation.z == 0.0F);
    REQUIRE(identity_transform.orientation.w == 1.0F);
    REQUIRE(identity_transform.world_position == material_sphere_center);
    REQUIRE(identity_transform.radius == material_sphere_radius);
    REQUIRE(identity_transform.half_segment_length == 0.0F);

    const shark::math::Quaternion orientation{
        0.0F,
        0.70710677F,
        0.0F,
        0.70710677F,
    };
    const auto transformed = make_material_sphere_transform(
        orientation,
        {-128.0F, 10.0F, -44.0F});
    REQUIRE(transformed.orientation.x == orientation.x);
    REQUIRE(transformed.orientation.y == orientation.y);
    REQUIRE(transformed.orientation.z == orientation.z);
    REQUIRE(transformed.orientation.w == orientation.w);
    REQUIRE(transformed.world_position ==
        shark::math::Float3{-128.0F, 10.0F, -44.0F});
    REQUIRE(transformed.radius == material_sphere_radius);
    REQUIRE(transformed.half_segment_length == 0.0F);

    const auto mesh = make_material_sphere_mesh();
    REQUIRE(mesh.vertices.size() == material_sphere_vertex_count);
    REQUIRE(mesh.indices.size() == material_sphere_index_count);

    for (const auto& vertex : mesh.vertices) {
        REQUIRE(shark::math::is_finite(vertex.position));
        REQUIRE(shark::math::is_finite(vertex.normal));
        const auto normal_length_squared =
            vertex.normal.x * vertex.normal.x +
            vertex.normal.y * vertex.normal.y +
            vertex.normal.z * vertex.normal.z;
        REQUIRE(normal_length_squared == Catch::Approx(1.0F).epsilon(
            0.00001F));
        const auto offset_x =
            vertex.position.x - material_sphere_center.x;
        const auto offset_y =
            vertex.position.y - material_sphere_center.y;
        const auto offset_z =
            vertex.position.z - material_sphere_center.z;
        const auto radius_squared =
            offset_x * offset_x +
            offset_y * offset_y +
            offset_z * offset_z;
        REQUIRE(radius_squared == Catch::Approx(
            material_sphere_radius * material_sphere_radius).epsilon(
                0.00001F));
    }
    for (const auto index : mesh.indices) {
        REQUIRE(index < mesh.vertices.size());
    }
}

TEST_CASE(
    "placeholder avatar proxy validation is bounded and enable-gated",
    "[renderer][d3d12][environment][avatar][validation]")
{
    using namespace shark;
    using namespace renderer;
    using namespace renderer::d3d12::detail;

    STATIC_REQUIRE(placeholder_avatar_part_count == 6);
    STATIC_REQUIRE(
        minimum_placeholder_avatar_body_pitch == -1.25F);
    STATIC_REQUIRE(
        maximum_placeholder_avatar_body_pitch == 0.25F);
    STATIC_REQUIRE(
        maximum_placeholder_avatar_body_vertical_offset == 0.40F);
    STATIC_REQUIRE(
        maximum_placeholder_avatar_torso_pitch == 0.20F);
    STATIC_REQUIRE(maximum_placeholder_avatar_arm_pitch == 1.0F);
    STATIC_REQUIRE(maximum_placeholder_avatar_leg_pitch == 0.90F);

    PlaceholderAvatarProxy proxy{
        .world_position = {2.0F, 3.0F, -4.0F},
        .orientation = {},
        .pose = {
            .body_pitch_radians =
                minimum_placeholder_avatar_body_pitch,
            .body_vertical_offset =
                maximum_placeholder_avatar_body_vertical_offset,
            .torso_pitch_radians =
                maximum_placeholder_avatar_torso_pitch,
            .left_arm_pitch_radians =
                -maximum_placeholder_avatar_arm_pitch,
            .right_arm_pitch_radians =
                maximum_placeholder_avatar_arm_pitch,
            .left_leg_pitch_radians =
                -maximum_placeholder_avatar_leg_pitch,
            .right_leg_pitch_radians =
                maximum_placeholder_avatar_leg_pitch,
        },
        .enabled = true,
    };
    REQUIRE(valid_placeholder_avatar_pose(proxy.pose));
    REQUIRE(valid_placeholder_avatar_proxy(proxy));
    for (const auto& part : make_placeholder_avatar_parts(proxy)) {
        REQUIRE(valid_environment_proxy_transform(part));
    }

    proxy.pose.body_pitch_radians =
        maximum_placeholder_avatar_body_pitch;
    REQUIRE(valid_placeholder_avatar_proxy(proxy));

    const auto require_invalid = [](const PlaceholderAvatarProxy value) {
        REQUIRE_FALSE(valid_placeholder_avatar_proxy(value));
        auto disabled = value;
        disabled.enabled = false;
        REQUIRE(valid_placeholder_avatar_proxy(disabled));
    };

    auto invalid = proxy;
    invalid.world_position.x =
        std::numeric_limits<float>::quiet_NaN();
    require_invalid(invalid);

    invalid = proxy;
    invalid.orientation = {0.0F, 0.0F, 0.0F, 2.0F};
    require_invalid(invalid);

    using PoseMember = float PlaceholderAvatarPose::*;
    constexpr std::array<PoseMember, 7> members{{
        &PlaceholderAvatarPose::body_pitch_radians,
        &PlaceholderAvatarPose::body_vertical_offset,
        &PlaceholderAvatarPose::torso_pitch_radians,
        &PlaceholderAvatarPose::left_arm_pitch_radians,
        &PlaceholderAvatarPose::right_arm_pitch_radians,
        &PlaceholderAvatarPose::left_leg_pitch_radians,
        &PlaceholderAvatarPose::right_leg_pitch_radians,
    }};
    constexpr std::array<float, members.size()> minimums{{
        minimum_placeholder_avatar_body_pitch,
        0.0F,
        -maximum_placeholder_avatar_torso_pitch,
        -maximum_placeholder_avatar_arm_pitch,
        -maximum_placeholder_avatar_arm_pitch,
        -maximum_placeholder_avatar_leg_pitch,
        -maximum_placeholder_avatar_leg_pitch,
    }};
    constexpr std::array<float, members.size()> maximums{{
        maximum_placeholder_avatar_body_pitch,
        maximum_placeholder_avatar_body_vertical_offset,
        maximum_placeholder_avatar_torso_pitch,
        maximum_placeholder_avatar_arm_pitch,
        maximum_placeholder_avatar_arm_pitch,
        maximum_placeholder_avatar_leg_pitch,
        maximum_placeholder_avatar_leg_pitch,
    }};
    for (std::size_t index = 0; index < members.size(); ++index) {
        invalid = proxy;
        invalid.pose.*members[index] = std::nextafter(
            minimums[index],
            -std::numeric_limits<float>::infinity());
        require_invalid(invalid);

        invalid = proxy;
        invalid.pose.*members[index] = std::nextafter(
            maximums[index],
            std::numeric_limits<float>::infinity());
        require_invalid(invalid);

        invalid = proxy;
        invalid.pose.*members[index] =
            std::numeric_limits<float>::quiet_NaN();
        require_invalid(invalid);
    }
}

TEST_CASE(
    "placeholder avatar expands to the fixed authored six-part envelope",
    "[renderer][d3d12][environment][avatar][parts]")
{
    using namespace shark;
    using namespace renderer;
    using namespace renderer::d3d12::detail;

    const PlaceholderAvatarProxy proxy{
        .world_position = {},
        .orientation = {},
        .pose = {},
        .enabled = true,
    };
    REQUIRE(valid_placeholder_avatar_proxy(proxy));
    const auto parts = make_placeholder_avatar_parts(proxy);
    REQUIRE(parts.size() == placeholder_avatar_part_count);

    require_float3(
        parts[0].world_position,
        placeholder_avatar_torso_center);
    REQUIRE(parts[0].radius == placeholder_avatar_torso_radius);
    REQUIRE(
        parts[0].half_segment_length ==
        placeholder_avatar_torso_half_segment_length);

    require_float3(
        parts[1].world_position,
        placeholder_avatar_head_center);
    REQUIRE(parts[1].radius == placeholder_avatar_head_radius);
    REQUIRE(parts[1].half_segment_length == 0.0F);

    require_float3(
        parts[2].world_position,
        placeholder_avatar_left_arm_center);
    require_float3(
        parts[3].world_position,
        placeholder_avatar_right_arm_center);
    REQUIRE(parts[2].radius == placeholder_avatar_arm_radius);
    REQUIRE(parts[3].radius == placeholder_avatar_arm_radius);
    REQUIRE(
        parts[2].half_segment_length ==
        placeholder_avatar_arm_half_segment_length);
    REQUIRE(
        parts[3].half_segment_length ==
        placeholder_avatar_arm_half_segment_length);

    require_float3(
        parts[4].world_position,
        placeholder_avatar_left_leg_center);
    require_float3(
        parts[5].world_position,
        placeholder_avatar_right_leg_center);
    REQUIRE(parts[4].radius == placeholder_avatar_leg_radius);
    REQUIRE(parts[5].radius == placeholder_avatar_leg_radius);
    REQUIRE(
        parts[4].half_segment_length ==
        placeholder_avatar_leg_half_segment_length);
    REQUIRE(
        parts[5].half_segment_length ==
        placeholder_avatar_leg_half_segment_length);

    for (const auto& part : parts) {
        REQUIRE(part.orientation == math::Quaternion{});
        REQUIRE(valid_environment_proxy_transform(part));
        REQUIRE(part.world_position.x - part.radius >= -1.0F);
        REQUIRE(part.world_position.x + part.radius <= 1.0F);
        REQUIRE(
            part.world_position.y -
                part.half_segment_length -
                part.radius >=
            -1.0F);
        REQUIRE(
            part.world_position.y +
                part.half_segment_length +
                part.radius <=
            1.0F);
        REQUIRE(part.world_position.z - part.radius >= -1.0F);
        REQUIRE(part.world_position.z + part.radius <= 1.0F);
    }
    REQUIRE(
        parts[1].world_position.y + parts[1].radius == 0.99F);
    REQUIRE(
        parts[4].world_position.y -
            parts[4].half_segment_length -
            parts[4].radius ==
        -0.93F);
    REQUIRE(
        parts[2].world_position.x - parts[2].radius == -0.44F);
    REQUIRE(
        parts[3].world_position.x + parts[3].radius == 0.44F);
}

TEST_CASE(
    "placeholder avatar hierarchy composes bounded finite root and joint poses",
    "[renderer][d3d12][environment][avatar][hierarchy]")
{
    using namespace shark;
    using namespace renderer;
    using namespace renderer::d3d12::detail;

    constexpr float sine = 0.70710677F;
    PlaceholderAvatarProxy proxy{
        .world_position = {10.0F, 20.0F, -30.0F},
        .orientation = {0.0F, sine, 0.0F, sine},
        .pose = {
            .body_pitch_radians = -1.0F,
            .body_vertical_offset = 0.35F,
            .torso_pitch_radians = 0.15F,
            .left_arm_pitch_radians = -0.75F,
            .right_arm_pitch_radians = 0.50F,
            .left_leg_pitch_radians = 0.60F,
            .right_leg_pitch_radians = -0.40F,
        },
        .enabled = true,
    };
    REQUIRE(valid_placeholder_avatar_proxy(proxy));
    const auto posed = make_placeholder_avatar_parts(proxy);
    const auto rest = make_placeholder_avatar_parts(
        PlaceholderAvatarProxy{.enabled = true});

    REQUIRE(posed.size() == rest.size());
    for (const auto& part : posed) {
        REQUIRE(valid_environment_proxy_transform(part));
        REQUIRE(math::is_finite(part.world_position));
        REQUIRE(math::is_unit(part.orientation));
    }
    REQUIRE(posed[0].world_position != rest[0].world_position);
    REQUIRE(posed[1].world_position != posed[0].world_position);
    REQUIRE(posed[2].world_position != posed[3].world_position);
    REQUIRE(posed[4].world_position != posed[5].world_position);
    REQUIRE(posed[0].orientation != proxy.orientation);
    REQUIRE(posed[1].orientation == posed[0].orientation);
    REQUIRE(posed[2].orientation != posed[3].orientation);
    REQUIRE(posed[4].orientation != posed[5].orientation);
}

TEST_CASE(
    "environment proxy deformation preserves spheres and forms capsules",
    "[renderer][d3d12][environment][capsule]")
{
    using namespace shark::renderer::d3d12::detail;

    const auto sphere = deform_environment_proxy(
        {0.6F, 0.8F, 0.0F},
        1.0F,
        0.0F);
    REQUIRE_FALSE(sphere.capsule);
    require_float3(
        sphere.local_position,
        {0.6F, 0.8F, 0.0F});
    require_float3(
        sphere.local_normal,
        {0.6F, 0.8F, 0.0F});

    constexpr float radius = 0.5F;
    constexpr float half_segment = 0.75F;
    const auto top = deform_environment_proxy(
        {0.0F, 1.0F, 0.0F},
        radius,
        half_segment);
    REQUIRE(top.capsule);
    require_float3(top.local_position, {0.0F, 1.25F, 0.0F});
    require_float3(top.local_normal, {0.0F, 1.0F, 0.0F});

    constexpr float seam_x = 0.8660254F;
    const auto upper_seam = deform_environment_proxy(
        {seam_x, capsule_parameter_seam, 0.0F},
        radius,
        half_segment);
    require_float3(
        upper_seam.local_position,
        {radius, half_segment, 0.0F});
    require_float3(
        upper_seam.local_normal,
        {1.0F, 0.0F, 0.0F});

    constexpr float quarter_x = 0.96824586F;
    const auto cylinder = deform_environment_proxy(
        {quarter_x, 0.25F, 0.0F},
        radius,
        half_segment);
    require_float3(
        cylinder.local_position,
        {radius, 0.375F, 0.0F});
    require_float3(
        cylinder.local_normal,
        {1.0F, 0.0F, 0.0F});

    const auto lower_seam = deform_environment_proxy(
        {seam_x, -capsule_parameter_seam, 0.0F},
        radius,
        half_segment);
    require_float3(
        lower_seam.local_position,
        {radius, -half_segment, 0.0F});
    require_float3(
        lower_seam.local_normal,
        {1.0F, 0.0F, 0.0F});

    const auto bottom = deform_environment_proxy(
        {0.0F, -1.0F, 0.0F},
        radius,
        half_segment);
    require_float3(
        bottom.local_position,
        {0.0F, -1.25F, 0.0F});
    require_float3(
        bottom.local_normal,
        {0.0F, -1.0F, 0.0F});

    const auto upper_cap = deform_environment_proxy(
        {0.6F, 0.8F, 0.0F},
        radius,
        half_segment);
    constexpr float inverse_sqrt_two = 0.70710677F;
    require_float3(
        upper_cap.local_normal,
        {inverse_sqrt_two, inverse_sqrt_two, 0.0F});
    require_float3(
        upper_cap.local_position,
        {
            radius * inverse_sqrt_two,
            half_segment + radius * inverse_sqrt_two,
            0.0F,
        });

    for (const auto& sample :
         {top, upper_seam, cylinder, lower_seam, bottom, upper_cap}) {
        REQUIRE(shark::math::is_finite(sample.local_position));
        REQUIRE(shark::math::is_finite(sample.local_normal));
        require_unit(sample.local_normal);
        const auto radial_squared =
            sample.local_position.x * sample.local_position.x +
            sample.local_position.z * sample.local_position.z;
        REQUIRE(
            radial_squared <=
            radius * radius + 0.00001F);
        REQUIRE(
            std::abs(sample.local_position.y) <=
            half_segment + radius + 0.00001F);
    }
}
