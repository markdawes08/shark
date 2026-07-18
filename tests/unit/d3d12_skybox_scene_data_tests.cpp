#include "skybox_scene_data.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE(
    "skybox scene data reuses the cube at untouched reversed-Z depth",
    "[gpu][skybox]")
{
    using namespace shark::rhi::d3d12::detail;

    REQUIRE(skybox_clip_depth == 0.0F);
    REQUIRE(skybox_clip_depth == cube_depth_clear_value);
    REQUIRE(skybox_depth_comparison ==
        D3D12_COMPARISON_FUNC_GREATER_EQUAL);
    REQUIRE(skybox_depth_comparison == cube_depth_comparison);
    REQUIRE(skybox_depth_write_mask == D3D12_DEPTH_WRITE_MASK_ZERO);

    REQUIRE(skybox_index_count == 36);
    REQUIRE(skybox_indices.size() == cube_indices.size());
    REQUIRE(skybox_indices.data() == cube_indices.data());
}
