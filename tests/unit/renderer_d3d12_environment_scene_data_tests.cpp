#include "environment_scene_data.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

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
        material_sphere_translation_root_parameter == 3);
    STATIC_REQUIRE(
        material_sphere_translation_root_constant_count == 3);
    STATIC_REQUIRE(
        sizeof(MaterialSphereTranslationRootConstants) == 12);
    STATIC_REQUIRE(sizeof(EnvironmentVertex) == sizeof(float) * 6U);

    const auto identity_translation =
        make_material_sphere_translation(material_sphere_center);
    REQUIRE(identity_translation.translation ==
        shark::math::Float3{});
    const auto translated = make_material_sphere_translation(
        {-128.0F, 10.0F, -44.0F});
    REQUIRE(translated.translation ==
        shark::math::Float3{-131.0F, 8.75F, -43.0F});

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
