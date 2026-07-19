#include <shark/renderer/renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

TEST_CASE(
    "renderer boundary is move-only and owns scene-facing records",
    "[renderer][boundary]")
{
    using namespace shark::renderer;

    STATIC_REQUIRE(!std::is_copy_constructible_v<Renderer>);
    STATIC_REQUIRE(!std::is_copy_assignable_v<Renderer>);
    STATIC_REQUIRE(std::is_move_constructible_v<Renderer>);
    STATIC_REQUIRE(std::is_move_assignable_v<Renderer>);

    STATIC_REQUIRE(std::is_standard_layout_v<RenderExtent>);
    STATIC_REQUIRE(std::is_standard_layout_v<RenderFrameData>);
    STATIC_REQUIRE(std::is_standard_layout_v<RendererStats>);
    STATIC_REQUIRE(std::is_standard_layout_v<Texture2DArrayUploadView>);
    STATIC_REQUIRE(std::is_standard_layout_v<Texture2DUploadView>);
    STATIC_REQUIRE(std::is_standard_layout_v<TerrainMaterialUploadView>);
    STATIC_REQUIRE(std::is_standard_layout_v<TerrainChunkUploadView>);
    STATIC_REQUIRE(
        std::is_standard_layout_v<EnvironmentLightingUploadView>);

    const RendererConfig config;
    REQUIRE((config.extent == RenderExtent{1280, 720}));
    REQUIRE(config.synchronize_to_vertical_refresh);
    REQUIRE(config.native_window == nullptr);
    REQUIRE(config.terrain_materials.albedo.subresources == nullptr);
    REQUIRE(config.terrain_materials.normal.subresources == nullptr);
    REQUIRE(config.terrain_materials.roughness.subresources == nullptr);
    REQUIRE(config.terrain_mesh.chunks == nullptr);
    REQUIRE(config.terrain_mesh.chunk_count == 0);
    REQUIRE(
        config.environment_lighting.radiance.subresources == nullptr);
    REQUIRE(
        config.environment_lighting.brdf_lut.subresources == nullptr);

    const TerrainChunkUploadView chunk;
    REQUIRE(chunk.first_index == 0);
    REQUIRE(chunk.index_count == 0);
    REQUIRE(chunk.coarse_first_index == 0);
    REQUIRE(chunk.coarse_index_count == 0);
    REQUIRE(chunk.maximum_geometric_error == 0.0);

    const RenderFrameData frame;
    REQUIRE(
        frame.terrain_mode == TerrainRenderMode::solid);
    REQUIRE(
        frame.terrain_material_view ==
        TerrainMaterialView::shaded);
    REQUIRE(
        frame.camera_world_position == shark::math::Float3{});
    REQUIRE(
        frame.environment_lighting_mode ==
        EnvironmentLightingMode::image_based);

    STATIC_REQUIRE(
        static_cast<std::uint32_t>(TerrainMaterialView::shaded) == 1);
    STATIC_REQUIRE(
        static_cast<std::uint32_t>(
            TerrainMaterialView::material_weights) == 2);
    STATIC_REQUIRE(
        static_cast<std::uint32_t>(
            TerrainMaterialView::shading_normal) == 3);
    STATIC_REQUIRE(
        static_cast<std::uint32_t>(
            EnvironmentLightingMode::procedural_daylight) == 1);
    STATIC_REQUIRE(
        static_cast<std::uint32_t>(
            EnvironmentLightingMode::image_based) == 2);
    STATIC_REQUIRE(
        static_cast<std::uint8_t>(
            TextureDataFormat::rgba32_float) == 3);
}

TEST_CASE(
    "renderer statistics support exact lifecycle snapshots",
    "[renderer][boundary][statistics]")
{
    shark::renderer::RendererStats baseline;
    auto changed = baseline;

    REQUIRE(changed == baseline);
    changed.presented_frames = 1;
    REQUIRE(changed != baseline);
    changed = baseline;
    changed.render_graph_compilations = 1;
    REQUIRE(changed != baseline);
    changed = baseline;
    changed.terrain_query_marker_draw_calls = 1;
    REQUIRE(changed != baseline);
    changed = baseline;
    changed.terrain_material_texture_array_creations = 3;
    REQUIRE(changed != baseline);
    changed = baseline;
    changed.hdr_scene_color_creations = 1;
    REQUIRE(changed != baseline);
    changed = baseline;
    changed.material_sphere_draw_calls = 1;
    REQUIRE(changed != baseline);
    changed = baseline;
    changed.terrain_chunks_visible = 1;
    REQUIRE(changed != baseline);
    changed = baseline;
    changed.terrain_coarse_draw_calls = 1;
    REQUIRE(changed != baseline);
    changed = baseline;
    changed.terrain_lod0_indices = 384;
    REQUIRE(changed != baseline);
    changed = baseline;
    changed.terrain_maximum_geometric_error = 0.125;
    REQUIRE(changed != baseline);
}
