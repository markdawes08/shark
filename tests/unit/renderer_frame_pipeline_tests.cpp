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
    "renderer frame pipeline composes the exact terrain cube sky contract",
    "[renderer][frame-pipeline][render-graph]")
{
    using namespace shark;
    using namespace render_graph;
    using namespace renderer::detail;

    std::vector<std::string> execution_order;
    FramePipelineCallbacks callbacks{
        .terrain = [&execution_order](
            const PassContext& context,
            const TerrainPassResources& resources) {
            const auto color = context.write(resources.back_buffer);
            const auto depth = context.write(resources.depth_buffer);
            const auto vertices = context.read(resources.vertex_buffer);
            const auto indices = context.read(resources.index_buffer);
            const auto albedo = context.read(resources.albedo_layers);
            const auto normal = context.read(resources.normal_layers);
            const auto roughness = context.read(resources.roughness_layers);
            if (context.pass_name() != "Terrain" ||
                !color || color.value() !=
                    frame_back_buffer_external_id ||
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
                    frame_terrain_roughness_layers_external_id) {
                return core::Result<void>::failure(
                    callback_error("Terrain"));
            }
            execution_order.emplace_back("Terrain");
            return core::Result<void>::success();
        },
        .textured_cube = [&execution_order](
            const PassContext& context,
            const TexturedCubePassResources& resources) {
            const auto color = context.write(resources.back_buffer);
            const auto depth = context.write(resources.depth_buffer);
            const auto texture =
                context.read(resources.checker_texture);
            const auto vertices = context.read(resources.vertex_buffer);
            const auto indices = context.read(resources.index_buffer);
            if (context.pass_name() != "TexturedCube" ||
                !color || color.value() !=
                    frame_back_buffer_external_id ||
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
            const auto color = context.write(resources.back_buffer);
            const auto depth = context.read(resources.depth_buffer);
            const auto vertices = context.read(resources.vertex_buffer);
            const auto indices = context.read(resources.index_buffer);
            if (context.pass_name() != "Skybox" ||
                !color || color.value() !=
                    frame_back_buffer_external_id ||
                !depth || depth.value() !=
                    frame_depth_buffer_external_id ||
                !vertices || vertices.value() !=
                    frame_cube_vertex_buffer_external_id ||
                !indices || indices.value() !=
                    frame_cube_index_buffer_external_id) {
                return core::Result<void>::failure(
                    callback_error("Skybox"));
            }
            execution_order.emplace_back("Skybox");
            return core::Result<void>::success();
        },
    };

    auto graph_result = compose_frame_pipeline(std::move(callbacks));
    REQUIRE(graph_result);
    auto graph = std::move(graph_result).value();

    REQUIRE((graph.stats() == CompiledGraphStats{
        .imported_resource_count = 10,
        .pass_count = 3,
        .dependency_count = 2,
        .transition_count = 4,
        .elided_transition_count = 22,
    }));

    const auto passes = graph.passes();
    REQUIRE(passes.size() == 3);
    REQUIRE(passes[0].name == "Terrain");
    REQUIRE(passes[1].name == "TexturedCube");
    REQUIRE(passes[2].name == "Skybox");
    REQUIRE(passes[0].dependencies.empty());
    REQUIRE((passes[1].dependencies ==
        std::vector<PassHandle>{passes[0].handle}));
    REQUIRE((passes[2].dependencies ==
        std::vector<PassHandle>{passes[1].handle}));

    REQUIRE(passes[0].accesses.size() == 7);
    require_access(
        passes[0].accesses[0],
        frame_back_buffer_external_id,
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

    REQUIRE(passes[1].accesses.size() == 5);
    require_access(
        passes[1].accesses[0],
        frame_back_buffer_external_id,
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

    REQUIRE(passes[2].accesses.size() == 4);
    require_access(
        passes[2].accesses[0],
        frame_back_buffer_external_id,
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

    REQUIRE(passes[0].transitions.size() == 1);
    REQUIRE((passes[0].transitions[0] == ResourceTransition{
        passes[0].accesses[0].resource,
        frame_back_buffer_external_id,
        ResourceState::present,
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

    const auto final_transitions = graph.final_transitions();
    REQUIRE(final_transitions.size() == 2);
    REQUIRE((final_transitions[0] == ResourceTransition{
        passes[0].accesses[0].resource,
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
        .passes_executed = 3,
        .transitions_recorded = 4,
    }));
    REQUIRE((execution_order == std::vector<std::string>{
        "Terrain",
        "TexturedCube",
        "Skybox",
    }));
    REQUIRE(recorder.transitions.size() == 4);
    REQUIRE(recorder.transitions[0] == passes[0].transitions[0]);
    REQUIRE(recorder.transitions[1] == passes[2].transitions[0]);
    REQUIRE(recorder.transitions[2] == final_transitions[0]);
    REQUIRE(recorder.transitions[3] == final_transitions[1]);
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
}
