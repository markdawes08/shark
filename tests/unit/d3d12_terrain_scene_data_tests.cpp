#include "terrain_scene_data.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

TEST_CASE(
    "terrain input and diagnostic raster contracts match D3D12",
    "[gpu][terrain][contract]")
{
    using namespace shark::rhi::d3d12::detail;

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
        terrain_depth_write_mask == D3D12_DEPTH_WRITE_MASK_ALL);
    STATIC_REQUIRE(
        terrain_bounds_depth_write_mask ==
        D3D12_DEPTH_WRITE_MASK_ZERO);
    STATIC_REQUIRE(
        terrain_depth_comparison ==
        D3D12_COMPARISON_FUNC_GREATER_EQUAL);
}
