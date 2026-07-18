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

    const RendererConfig config;
    REQUIRE((config.extent == RenderExtent{1280, 720}));
    REQUIRE(config.synchronize_to_vertical_refresh);
    REQUIRE(config.native_window == nullptr);
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
}
