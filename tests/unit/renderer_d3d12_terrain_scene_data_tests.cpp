#include "terrain_scene_data.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string_view>

TEST_CASE(
    "terrain input and diagnostic raster contracts match D3D12",
    "[renderer][d3d12][gpu][terrain][contract]")
{
    using namespace shark::renderer::d3d12::detail;

    STATIC_REQUIRE(terrain_vertex_stride == 24);
    STATIC_REQUIRE(terrain_input_elements.size() == 2);
    STATIC_REQUIRE(terrain_input_layout.NumElements == 2);

    const auto& position = terrain_input_elements[0];
    REQUIRE(std::string_view{position.SemanticName} == "POSITION");
    REQUIRE(position.Format == DXGI_FORMAT_R32G32B32_FLOAT);
    REQUIRE(position.AlignedByteOffset == 0);

    const auto& normal = terrain_input_elements[1];
    REQUIRE(std::string_view{normal.SemanticName} == "NORMAL");
    REQUIRE(normal.Format == DXGI_FORMAT_R32G32B32_FLOAT);
    REQUIRE(normal.AlignedByteOffset == sizeof(float) * 3U);

    STATIC_REQUIRE(terrain_index_format == DXGI_FORMAT_R16_UINT);
    STATIC_REQUIRE(terrain_solid_fill_mode == D3D12_FILL_MODE_SOLID);
    STATIC_REQUIRE(
        terrain_wireframe_fill_mode == D3D12_FILL_MODE_WIREFRAME);
    STATIC_REQUIRE(
        terrain_topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    STATIC_REQUIRE(
        terrain_bounds_topology == D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    STATIC_REQUIRE(
        terrain_query_marker_topology ==
        D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    STATIC_REQUIRE(
        terrain_depth_write_mask == D3D12_DEPTH_WRITE_MASK_ALL);
    STATIC_REQUIRE(
        terrain_bounds_depth_write_mask ==
        D3D12_DEPTH_WRITE_MASK_ZERO);
    STATIC_REQUIRE(
        terrain_depth_comparison ==
        D3D12_COMPARISON_FUNC_GREATER_EQUAL);
    STATIC_REQUIRE(terrain_query_marker_vertex_count == 6);
    STATIC_REQUIRE(terrain_query_marker_index_count == 6);
    STATIC_REQUIRE(terrain_chunk_bounds_vertex_count == 8);
    STATIC_REQUIRE(terrain_chunk_bounds_index_count == 24);
    STATIC_REQUIRE(terrain_maximum_chunk_count == 4'096);
}

TEST_CASE(
    "terrain material bindings and fixture blend remain bounded",
    "[renderer][d3d12][gpu][terrain][material][contract]")
{
    using namespace shark::renderer;
    using namespace shark::renderer::d3d12::detail;

    STATIC_REQUIRE(terrain_material_layer_count == 2);
    STATIC_REQUIRE(terrain_material_texture_count == 3);
    STATIC_REQUIRE(terrain_material_maximum_dimension == 256);
    STATIC_REQUIRE(terrain_material_root_constant_count == 8);
    STATIC_REQUIRE(terrain_camera_root_parameter == 0);
    STATIC_REQUIRE(terrain_material_constants_root_parameter == 1);
    STATIC_REQUIRE(terrain_material_table_root_parameter == 2);
    STATIC_REQUIRE(terrain_material_albedo_descriptor == 0);
    STATIC_REQUIRE(terrain_material_normal_descriptor == 1);
    STATIC_REQUIRE(terrain_material_roughness_descriptor == 2);
    STATIC_REQUIRE(
        terrain_environment_irradiance_descriptor == 3);
    STATIC_REQUIRE(
        terrain_environment_specular_descriptor == 4);
    STATIC_REQUIRE(terrain_environment_brdf_descriptor == 5);
    STATIC_REQUIRE(terrain_shader_texture_count == 6);
    STATIC_REQUIRE(
        terrain_material_albedo_format ==
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    STATIC_REQUIRE(
        terrain_material_normal_format ==
        DXGI_FORMAT_R8G8B8A8_UNORM);
    STATIC_REQUIRE(
        terrain_material_roughness_format ==
        DXGI_FORMAT_R8G8B8A8_UNORM);
    STATIC_REQUIRE(
        terrain_material_repetitions_per_meter == 0.5F);
    STATIC_REQUIRE(terrain_material_normal_strength == 0.65F);
    STATIC_REQUIRE(terrain_material_slope_blend_start == 0.05F);
    STATIC_REQUIRE(terrain_material_slope_blend_end == 0.18F);
    STATIC_REQUIRE(terrain_material_height_blend_start == -1.5F);
    STATIC_REQUIRE(terrain_material_height_blend_end == -0.4F);
    STATIC_REQUIRE(
        terrain_material_height_rock_contribution == 0.4F);
    STATIC_REQUIRE(sizeof(TerrainMaterialRootConstants) == 32);
    STATIC_REQUIRE(
        offsetof(
            TerrainMaterialRootConstants,
            camera_world_position) == 0);
    STATIC_REQUIRE(
        offsetof(TerrainMaterialRootConstants, material_view) == 12);
    STATIC_REQUIRE(
        offsetof(
            TerrainMaterialRootConstants,
            environment_lighting_mode) == 16);
    STATIC_REQUIRE(
        offsetof(TerrainMaterialRootConstants, specular_max_lod) == 20);

    STATIC_REQUIRE(
        valid_terrain_material_view(TerrainMaterialView::shaded));
    STATIC_REQUIRE(
        valid_terrain_material_view(
            TerrainMaterialView::material_weights));
    STATIC_REQUIRE(
        valid_terrain_material_view(
            TerrainMaterialView::shading_normal));
    STATIC_REQUIRE_FALSE(valid_terrain_material_view(
        static_cast<TerrainMaterialView>(0)));

    REQUIRE(
        terrain_material_rock_weight(1.0F, -3.0F) ==
        Catch::Approx(0.0F));
    REQUIRE(
        terrain_material_rock_weight(0.70F, -3.0F) ==
        Catch::Approx(1.0F));
    REQUIRE(
        terrain_material_rock_weight(1.0F, -0.4F) ==
        Catch::Approx(0.4F));
}
