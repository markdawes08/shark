#include "environment_scene_data.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
    "debug capsule proxy validation is bounded and enable-gated",
    "[renderer][d3d12][environment][capsule]")
{
    using namespace shark;
    using namespace renderer;
    using namespace renderer::d3d12::detail;

    DebugCapsuleProxy proxy{
        .world_position = {2.0F, 3.0F, -4.0F},
        .orientation = {},
        .radius = 0.5F,
        .half_segment_length = 0.75F,
        .enabled = true,
    };
    REQUIRE(valid_debug_capsule_proxy(proxy));

    const auto transform = make_debug_capsule_transform(proxy);
    REQUIRE(transform.orientation == proxy.orientation);
    REQUIRE(transform.world_position == proxy.world_position);
    REQUIRE(transform.radius == proxy.radius);
    REQUIRE(
        transform.half_segment_length ==
        proxy.half_segment_length);

    proxy.radius = maximum_debug_capsule_radius;
    proxy.half_segment_length =
        maximum_debug_capsule_half_segment_length;
    REQUIRE(valid_debug_capsule_proxy(proxy));

    const auto require_invalid = [](const DebugCapsuleProxy value) {
        REQUIRE_FALSE(valid_debug_capsule_proxy(value));
        auto disabled = value;
        disabled.enabled = false;
        REQUIRE(valid_debug_capsule_proxy(disabled));
    };

    auto invalid = proxy;
    invalid.world_position.x =
        std::numeric_limits<float>::quiet_NaN();
    require_invalid(invalid);

    invalid = proxy;
    invalid.orientation = {0.0F, 0.0F, 0.0F, 2.0F};
    require_invalid(invalid);

    invalid = proxy;
    invalid.radius = 0.0F;
    require_invalid(invalid);
    invalid.radius = -1.0F;
    require_invalid(invalid);
    invalid.radius =
        std::nextafter(
            maximum_debug_capsule_radius,
            std::numeric_limits<float>::infinity());
    require_invalid(invalid);
    invalid.radius = std::numeric_limits<float>::infinity();
    require_invalid(invalid);

    invalid = proxy;
    invalid.half_segment_length = 0.0F;
    require_invalid(invalid);
    invalid.half_segment_length = -1.0F;
    require_invalid(invalid);
    invalid.half_segment_length =
        std::nextafter(
            maximum_debug_capsule_half_segment_length,
            std::numeric_limits<float>::infinity());
    require_invalid(invalid);
    invalid.half_segment_length =
        std::numeric_limits<float>::infinity();
    require_invalid(invalid);
}

TEST_CASE(
    "environment proxy deformation preserves spheres and forms a capsule",
    "[renderer][d3d12][environment][capsule]")
{
    using namespace shark::renderer::d3d12::detail;

    const auto sphere = deform_environment_proxy(
        {0.6F, 0.8F, 0.0F},
        1.0F,
        0.0F);
    REQUIRE_FALSE(sphere.debug_capsule);
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
    REQUIRE(top.debug_capsule);
    require_float3(top.local_position, {0.0F, 1.25F, 0.0F});
    require_float3(top.local_normal, {0.0F, 1.0F, 0.0F});

    constexpr float seam_x = 0.8660254F;
    const auto upper_seam = deform_environment_proxy(
        {seam_x, debug_capsule_parameter_seam, 0.0F},
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
        {seam_x, -debug_capsule_parameter_seam, 0.0F},
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
