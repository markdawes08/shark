#include "frame_pipeline.hpp"

#include <shark/core/error.hpp>
#include <shark/core/result.hpp>
#include <shark/render_graph/render_graph.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class RecordingTransitionRecorder final
    : public shark::render_graph::TransitionRecorder {
public:
    [[nodiscard]] shark::core::Result<void> transition(
        const shark::render_graph::ResourceTransition&
            transition) override
    {
        transitions.push_back(transition);
        return shark::core::Result<void>::success();
    }

    std::vector<shark::render_graph::ResourceTransition> transitions;
};

[[nodiscard]] shark::core::Error callback_error(
    const std::string_view pass_name)
{
    return shark::core::Error{
        shark::core::ErrorCategory::graphics,
        shark::core::ErrorCode::invalid_state,
        std::string{pass_name} + " received unexpected frame resources",
    };
}

[[nodiscard]] shark::renderer::detail::FramePipelineCallbacks
valid_no_op_callbacks()
{
    using namespace shark;
    using namespace renderer::detail;

    return FramePipelineCallbacks{
        .terrain = [](
            const render_graph::PassContext&,
            const TerrainPassResources&) {
            return core::Result<void>::success();
        },
        .textured_cube = [](
            const render_graph::PassContext&,
            const TexturedCubePassResources&) {
            return core::Result<void>::success();
        },
        .skybox = [](
            const render_graph::PassContext&,
            const SkyboxPassResources&) {
            return core::Result<void>::success();
        },
        .tone_map = [](
            const render_graph::PassContext&,
            const ToneMapPassResources&) {
            return core::Result<void>::success();
        },
    };
}

void require_access(
    const shark::render_graph::ResourceAccess& access,
    const shark::render_graph::ExternalResourceId external,
    const shark::render_graph::ResourceState state,
    const shark::render_graph::AccessMode mode)
{
    REQUIRE(access.external == external);
    REQUIRE(access.state == state);
    REQUIRE(access.mode == mode);
}

} // namespace

TEST_CASE(
    "renderer frame pipeline composes the exact HDR environment contract",
    "[renderer][frame-pipeline][render-graph]")
{
    using namespace shark;
    using namespace render_graph;
    using namespace renderer::detail;

    REQUIRE(frame_back_buffer_external_id.value == 1);
    REQUIRE(frame_depth_buffer_external_id.value == 2);
    REQUIRE(frame_checker_texture_external_id.value == 3);
    REQUIRE(frame_cube_vertex_buffer_external_id.value == 4);
    REQUIRE(frame_cube_index_buffer_external_id.value == 5);
    REQUIRE(frame_terrain_vertex_buffer_external_id.value == 6);
    REQUIRE(frame_terrain_index_buffer_external_id.value == 7);
    REQUIRE(frame_terrain_albedo_layers_external_id.value == 8);
    REQUIRE(frame_terrain_normal_layers_external_id.value == 9);
    REQUIRE(frame_terrain_roughness_layers_external_id.value == 10);
    REQUIRE(frame_scene_color_external_id.value == 11);
    REQUIRE(frame_environment_radiance_external_id.value == 12);
    REQUIRE(frame_environment_irradiance_external_id.value == 13);
    REQUIRE(
        frame_environment_prefiltered_specular_external_id.value == 14);
    REQUIRE(frame_environment_brdf_lut_external_id.value == 15);

    std::vector<std::string> execution_order;
    FramePipelineCallbacks callbacks{
        .terrain = [&execution_order](
            const PassContext& context,
            const TerrainPassResources& resources) {
            const auto color = context.write(resources.scene_color);
            const auto depth = context.write(resources.depth_buffer);
            const auto vertices = context.read(resources.vertex_buffer);
            const auto indices = context.read(resources.index_buffer);
            const auto albedo = context.read(resources.albedo_layers);
            const auto normal = context.read(resources.normal_layers);
            const auto roughness = context.read(resources.roughness_layers);
            const auto irradiance =
                context.read(resources.environment_irradiance);
            const auto prefiltered_specular =
                context.read(resources.environment_prefiltered_specular);
            const auto brdf_lut =
                context.read(resources.environment_brdf_lut);
            if (context.pass_name() != "Terrain" ||
                !color || color.value() !=
                    frame_scene_color_external_id ||
                !depth || depth.value() !=
                    frame_depth_buffer_external_id ||
                !vertices || vertices.value() !=
                    frame_terrain_vertex_buffer_external_id ||
                !indices || indices.value() !=
                    frame_terrain_index_buffer_external_id ||
                !albedo || albedo.value() !=
                    frame_terrain_albedo_layers_external_id ||
                !normal || normal.value() !=
                    frame_terrain_normal_layers_external_id ||
                !roughness || roughness.value() !=
                    frame_terrain_roughness_layers_external_id ||
                !irradiance || irradiance.value() !=
                    frame_environment_irradiance_external_id ||
                !prefiltered_specular ||
                    prefiltered_specular.value() !=
                        frame_environment_prefiltered_specular_external_id ||
                !brdf_lut || brdf_lut.value() !=
                    frame_environment_brdf_lut_external_id) {
                return core::Result<void>::failure(
                    callback_error("Terrain"));
            }
            execution_order.emplace_back("Terrain");
            return core::Result<void>::success();
        },
        .textured_cube = [&execution_order](
            const PassContext& context,
            const TexturedCubePassResources& resources) {
            const auto color = context.write(resources.scene_color);
            const auto depth = context.write(resources.depth_buffer);
            const auto texture =
                context.read(resources.checker_texture);
            const auto vertices = context.read(resources.vertex_buffer);
            const auto indices = context.read(resources.index_buffer);
            if (context.pass_name() != "TexturedCube" ||
                !color || color.value() !=
                    frame_scene_color_external_id ||
                !depth || depth.value() !=
                    frame_depth_buffer_external_id ||
                !texture || texture.value() !=
                    frame_checker_texture_external_id ||
                !vertices || vertices.value() !=
                    frame_cube_vertex_buffer_external_id ||
                !indices || indices.value() !=
                    frame_cube_index_buffer_external_id) {
                return core::Result<void>::failure(
                    callback_error("TexturedCube"));
            }
            execution_order.emplace_back("TexturedCube");
            return core::Result<void>::success();
        },
        .skybox = [&execution_order](
            const PassContext& context,
            const SkyboxPassResources& resources) {
            const auto color = context.write(resources.scene_color);
            const auto depth = context.read(resources.depth_buffer);
            const auto vertices = context.read(resources.vertex_buffer);
            const auto indices = context.read(resources.index_buffer);
            const auto radiance =
                context.read(resources.environment_radiance);
            if (context.pass_name() != "Skybox" ||
                !color || color.value() !=
                    frame_scene_color_external_id ||
                !depth || depth.value() !=
                    frame_depth_buffer_external_id ||
                !vertices || vertices.value() !=
                    frame_cube_vertex_buffer_external_id ||
                !indices || indices.value() !=
                    frame_cube_index_buffer_external_id ||
                !radiance || radiance.value() !=
                    frame_environment_radiance_external_id) {
                return core::Result<void>::failure(
                    callback_error("Skybox"));
            }
            execution_order.emplace_back("Skybox");
            return core::Result<void>::success();
        },
        .tone_map = [&execution_order](
            const PassContext& context,
            const ToneMapPassResources& resources) {
            const auto color = context.write(resources.back_buffer);
            const auto scene = context.read(resources.scene_color);
            if (context.pass_name() != "ToneMap" ||
                !color || color.value() !=
                    frame_back_buffer_external_id ||
                !scene || scene.value() !=
                    frame_scene_color_external_id) {
                return core::Result<void>::failure(
                    callback_error("ToneMap"));
            }
            execution_order.emplace_back("ToneMap");
            return core::Result<void>::success();
        },
    };

    auto graph_result = compose_frame_pipeline(std::move(callbacks));
    REQUIRE(graph_result);
    auto graph = std::move(graph_result).value();

    REQUIRE((graph.stats() == CompiledGraphStats{
        .imported_resource_count = 15,
        .pass_count = 4,
        .dependency_count = 3,
        .transition_count = 6,
        .elided_transition_count = 31,
    }));

    const auto passes = graph.passes();
    REQUIRE(passes.size() == 4);
    REQUIRE(passes[0].name == "Terrain");
    REQUIRE(passes[1].name == "TexturedCube");
    REQUIRE(passes[2].name == "Skybox");
    REQUIRE(passes[3].name == "ToneMap");
    REQUIRE(passes[0].dependencies.empty());
    REQUIRE((passes[1].dependencies ==
        std::vector<PassHandle>{passes[0].handle}));
    REQUIRE((passes[2].dependencies ==
        std::vector<PassHandle>{passes[1].handle}));
    REQUIRE((passes[3].dependencies ==
        std::vector<PassHandle>{passes[2].handle}));

    REQUIRE(passes[0].accesses.size() == 10);
    require_access(
        passes[0].accesses[0],
        frame_scene_color_external_id,
        ResourceState::render_target,
        AccessMode::write);
    require_access(
        passes[0].accesses[1],
        frame_depth_buffer_external_id,
        ResourceState::depth_write,
        AccessMode::write);
    require_access(
        passes[0].accesses[2],
        frame_terrain_vertex_buffer_external_id,
        ResourceState::vertex_buffer,
        AccessMode::read);
    require_access(
        passes[0].accesses[3],
        frame_terrain_index_buffer_external_id,
        ResourceState::index_buffer,
        AccessMode::read);
    require_access(
        passes[0].accesses[4],
        frame_terrain_albedo_layers_external_id,
        ResourceState::pixel_shader_read,
        AccessMode::read);
    require_access(
        passes[0].accesses[5],
        frame_terrain_normal_layers_external_id,
        ResourceState::pixel_shader_read,
        AccessMode::read);
    require_access(
        passes[0].accesses[6],
        frame_terrain_roughness_layers_external_id,
        ResourceState::pixel_shader_read,
        AccessMode::read);
    require_access(
        passes[0].accesses[7],
        frame_environment_irradiance_external_id,
        ResourceState::pixel_shader_read,
        AccessMode::read);
    require_access(
        passes[0].accesses[8],
        frame_environment_prefiltered_specular_external_id,
        ResourceState::pixel_shader_read,
        AccessMode::read);
    require_access(
        passes[0].accesses[9],
        frame_environment_brdf_lut_external_id,
        ResourceState::pixel_shader_read,
        AccessMode::read);

    REQUIRE(passes[1].accesses.size() == 5);
    require_access(
        passes[1].accesses[0],
        frame_scene_color_external_id,
        ResourceState::render_target,
        AccessMode::write);
    require_access(
        passes[1].accesses[1],
        frame_depth_buffer_external_id,
        ResourceState::depth_write,
        AccessMode::write);
    require_access(
        passes[1].accesses[2],
        frame_checker_texture_external_id,
        ResourceState::pixel_shader_read,
        AccessMode::read);
    require_access(
        passes[1].accesses[3],
        frame_cube_vertex_buffer_external_id,
        ResourceState::vertex_buffer,
        AccessMode::read);
    require_access(
        passes[1].accesses[4],
        frame_cube_index_buffer_external_id,
        ResourceState::index_buffer,
        AccessMode::read);

    REQUIRE(passes[2].accesses.size() == 5);
    require_access(
        passes[2].accesses[0],
        frame_scene_color_external_id,
        ResourceState::render_target,
        AccessMode::write);
    require_access(
        passes[2].accesses[1],
        frame_depth_buffer_external_id,
        ResourceState::depth_read,
        AccessMode::read);
    require_access(
        passes[2].accesses[2],
        frame_cube_vertex_buffer_external_id,
        ResourceState::vertex_buffer,
        AccessMode::read);
    require_access(
        passes[2].accesses[3],
        frame_cube_index_buffer_external_id,
        ResourceState::index_buffer,
        AccessMode::read);
    require_access(
        passes[2].accesses[4],
        frame_environment_radiance_external_id,
        ResourceState::pixel_shader_read,
        AccessMode::read);

    REQUIRE(passes[3].accesses.size() == 2);
    require_access(
        passes[3].accesses[0],
        frame_back_buffer_external_id,
        ResourceState::render_target,
        AccessMode::write);
    require_access(
        passes[3].accesses[1],
        frame_scene_color_external_id,
        ResourceState::pixel_shader_read,
        AccessMode::read);

    REQUIRE(passes[0].transitions.size() == 1);
    REQUIRE((passes[0].transitions[0] == ResourceTransition{
        passes[0].accesses[0].resource,
        frame_scene_color_external_id,
        ResourceState::pixel_shader_read,
        ResourceState::render_target,
    }));
    REQUIRE(passes[1].transitions.empty());
    REQUIRE(passes[2].transitions.size() == 1);
    REQUIRE((passes[2].transitions[0] == ResourceTransition{
        passes[2].accesses[1].resource,
        frame_depth_buffer_external_id,
        ResourceState::depth_write,
        ResourceState::depth_read,
    }));
    REQUIRE(passes[3].transitions.size() == 2);
    REQUIRE((passes[3].transitions[0] == ResourceTransition{
        passes[3].accesses[0].resource,
        frame_back_buffer_external_id,
        ResourceState::present,
        ResourceState::render_target,
    }));
    REQUIRE((passes[3].transitions[1] == ResourceTransition{
        passes[3].accesses[1].resource,
        frame_scene_color_external_id,
        ResourceState::render_target,
        ResourceState::pixel_shader_read,
    }));

    const auto final_transitions = graph.final_transitions();
    REQUIRE(final_transitions.size() == 2);
    REQUIRE((final_transitions[0] == ResourceTransition{
        passes[3].accesses[0].resource,
        frame_back_buffer_external_id,
        ResourceState::render_target,
        ResourceState::present,
    }));
    REQUIRE((final_transitions[1] == ResourceTransition{
        passes[2].accesses[1].resource,
        frame_depth_buffer_external_id,
        ResourceState::depth_read,
        ResourceState::depth_write,
    }));

    RecordingTransitionRecorder recorder;
    const auto execution_result = graph.execute(recorder);
    REQUIRE(execution_result);
    REQUIRE((execution_result.value() == ExecutionStats{
        .passes_executed = 4,
        .transitions_recorded = 6,
    }));
    REQUIRE((execution_order == std::vector<std::string>{
        "Terrain",
        "TexturedCube",
        "Skybox",
        "ToneMap",
    }));
    REQUIRE(recorder.transitions.size() == 6);
    REQUIRE(recorder.transitions[0] == passes[0].transitions[0]);
    REQUIRE(recorder.transitions[1] == passes[2].transitions[0]);
    REQUIRE(recorder.transitions[2] == passes[3].transitions[0]);
    REQUIRE(recorder.transitions[3] == passes[3].transitions[1]);
    REQUIRE(recorder.transitions[4] == final_transitions[0]);
    REQUIRE(recorder.transitions[5] == final_transitions[1]);
}

TEST_CASE(
    "renderer frame pipeline rejects each missing semantic callback",
    "[renderer][frame-pipeline][validation]")
{
    using namespace shark;
    using namespace renderer::detail;

    auto missing_terrain = valid_no_op_callbacks();
    missing_terrain.terrain = {};
    const auto terrain_result =
        compose_frame_pipeline(std::move(missing_terrain));
    REQUIRE_FALSE(terrain_result);
    REQUIRE(terrain_result.error().category() ==
        core::ErrorCategory::graphics);
    REQUIRE(terrain_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(terrain_result.error().message().find("Terrain") !=
        std::string_view::npos);

    auto missing_cube = valid_no_op_callbacks();
    missing_cube.textured_cube = {};
    const auto cube_result =
        compose_frame_pipeline(std::move(missing_cube));
    REQUIRE_FALSE(cube_result);
    REQUIRE(cube_result.error().category() ==
        core::ErrorCategory::graphics);
    REQUIRE(cube_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(cube_result.error().message().find("TexturedCube") !=
        std::string_view::npos);

    auto missing_skybox = valid_no_op_callbacks();
    missing_skybox.skybox = {};
    const auto skybox_result =
        compose_frame_pipeline(std::move(missing_skybox));
    REQUIRE_FALSE(skybox_result);
    REQUIRE(skybox_result.error().category() ==
        core::ErrorCategory::graphics);
    REQUIRE(skybox_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(skybox_result.error().message().find("Skybox") !=
        std::string_view::npos);

    auto missing_tone_map = valid_no_op_callbacks();
    missing_tone_map.tone_map = {};
    const auto tone_map_result =
        compose_frame_pipeline(std::move(missing_tone_map));
    REQUIRE_FALSE(tone_map_result);
    REQUIRE(tone_map_result.error().category() ==
        core::ErrorCategory::graphics);
    REQUIRE(tone_map_result.error().code() ==
        core::ErrorCode::invalid_argument);
    REQUIRE(tone_map_result.error().message().find("ToneMap") !=
        std::string_view::npos);
}
