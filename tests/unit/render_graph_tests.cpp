#include <shark/render_graph/render_graph.hpp>

#include <shark/core/error.hpp>
#include <shark/core/result.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] shark::render_graph::PassCallback no_op_pass()
{
    return [](const shark::render_graph::PassContext&) {
        return shark::core::Result<void>::success();
    };
}

class RecordingTransitionRecorder final
    : public shark::render_graph::TransitionRecorder {
public:
    [[nodiscard]] shark::core::Result<void> transition(
        const shark::render_graph::ResourceTransition&
            transition) override
    {
        if (fail_at_.has_value() &&
            transitions.size() == *fail_at_) {
            return shark::core::Result<void>::failure(
                shark::core::Error{
                    shark::core::ErrorCategory::graphics,
                    shark::core::ErrorCode::unavailable,
                    "synthetic transition failure",
                });
        }
        transitions.push_back(transition);
        return shark::core::Result<void>::success();
    }

    void fail_at(const std::size_t transition_index) noexcept
    {
        fail_at_ = transition_index;
    }

    std::vector<shark::render_graph::ResourceTransition> transitions;

private:
    std::optional<std::size_t> fail_at_;
};

[[nodiscard]] const shark::render_graph::CompiledPassInfo& find_pass(
    const shark::render_graph::CompiledGraph& graph,
    const std::string_view name)
{
    const auto passes = graph.passes();
    const auto found = std::find_if(
        passes.begin(),
        passes.end(),
        [name](const shark::render_graph::CompiledPassInfo& pass) {
            return pass.name == name;
        });
    REQUIRE(found != passes.end());
    return *found;
}

[[nodiscard]] shark::render_graph::CompiledGraph compile_graph(
    shark::render_graph::GraphBuilder builder)
{
    auto compiled = std::move(builder).compile();
    REQUIRE(compiled);
    return std::move(compiled).value();
}

} // namespace

TEST_CASE(
    "render graph declarations reject ambiguous or invalid input",
    "[render-graph][validation]")
{
    using namespace shark;
    using namespace render_graph;

    GraphBuilder builder;
    REQUIRE_FALSE(ResourceHandle{}.valid());
    REQUIRE_FALSE(PassHandle{}.valid());
    REQUIRE_FALSE(builder.import_resource(
        "",
        ExternalResourceId{0},
        ResourceState::present,
        ResourceState::present));

    const auto back_buffer_result = builder.import_resource(
        "back-buffer",
        ExternalResourceId{0},
        ResourceState::present,
        ResourceState::present);
    REQUIRE(back_buffer_result);
    const auto back_buffer = back_buffer_result.value();
    REQUIRE(back_buffer.valid());
    REQUIRE_FALSE(builder.import_resource(
        "back-buffer",
        ExternalResourceId{1},
        ResourceState::present,
        ResourceState::present));
    REQUIRE_FALSE(builder.import_resource(
        "alias",
        ExternalResourceId{0},
        ResourceState::present,
        ResourceState::present));
    REQUIRE_FALSE(builder.import_resource(
        "invalid-state",
        ExternalResourceId{2},
        static_cast<ResourceState>(255),
        ResourceState::present));

    REQUIRE_FALSE(builder.add_pass("", no_op_pass()));
    REQUIRE_FALSE(builder.add_pass("missing-callback", {}));
    const auto cube_result = builder.add_pass("cube", no_op_pass());
    REQUIRE(cube_result);
    const auto cube = cube_result.value();
    REQUIRE(cube.valid());
    REQUIRE_FALSE(builder.add_pass("cube", no_op_pass()));

    REQUIRE_FALSE(builder.read(
        cube,
        back_buffer,
        ResourceState::render_target));
    REQUIRE(builder.write(
        cube,
        back_buffer,
        ResourceState::render_target));
    REQUIRE_FALSE(builder.write(
        cube,
        back_buffer,
        ResourceState::render_target));
    REQUIRE_FALSE(builder.write(
        PassHandle{},
        back_buffer,
        ResourceState::render_target));
    REQUIRE_FALSE(builder.write(
        cube,
        ResourceHandle{},
        ResourceState::render_target));
    REQUIRE_FALSE(builder.add_dependency(cube, cube));

    const auto depth_result = builder.import_resource(
        "depth",
        ExternalResourceId{3},
        ResourceState::depth_write,
        ResourceState::depth_write);
    const auto depth_reader =
        builder.add_pass("depth-reader", no_op_pass());
    const auto invalid_depth_writer =
        builder.add_pass("invalid-depth-writer", no_op_pass());
    REQUIRE(depth_result);
    REQUIRE(depth_reader);
    REQUIRE(invalid_depth_writer);
    REQUIRE(builder.read(
        depth_reader.value(),
        depth_result.value(),
        ResourceState::depth_read));
    REQUIRE_FALSE(builder.write(
        invalid_depth_writer.value(),
        depth_result.value(),
        ResourceState::depth_read));
    REQUIRE_FALSE(builder.read(
        invalid_depth_writer.value(),
        depth_result.value(),
        ResourceState::depth_write));

    const auto texture_result = builder.import_resource(
        "pixel-texture",
        ExternalResourceId{4},
        ResourceState::pixel_shader_read,
        ResourceState::pixel_shader_read);
    REQUIRE(texture_result);
    REQUIRE(builder.read(
        depth_reader.value(),
        texture_result.value(),
        ResourceState::pixel_shader_read));
    REQUIRE_FALSE(builder.write(
        invalid_depth_writer.value(),
        texture_result.value(),
        ResourceState::pixel_shader_read));

    const auto vertex_buffer_result = builder.import_resource(
        "vertex-buffer",
        ExternalResourceId{5},
        ResourceState::vertex_buffer,
        ResourceState::vertex_buffer);
    const auto index_buffer_result = builder.import_resource(
        "index-buffer",
        ExternalResourceId{6},
        ResourceState::index_buffer,
        ResourceState::index_buffer);
    REQUIRE(vertex_buffer_result);
    REQUIRE(index_buffer_result);
    REQUIRE(builder.read(
        depth_reader.value(),
        vertex_buffer_result.value(),
        ResourceState::vertex_buffer));
    REQUIRE(builder.read(
        depth_reader.value(),
        index_buffer_result.value(),
        ResourceState::index_buffer));
    REQUIRE_FALSE(builder.write(
        invalid_depth_writer.value(),
        vertex_buffer_result.value(),
        ResourceState::vertex_buffer));
    REQUIRE_FALSE(builder.write(
        invalid_depth_writer.value(),
        index_buffer_result.value(),
        ResourceState::index_buffer));

    GraphBuilder foreign_builder;
    const auto foreign_resource = foreign_builder.import_resource(
        "foreign",
        ExternalResourceId{8},
        ResourceState::present,
        ResourceState::present);
    const auto foreign_pass =
        foreign_builder.add_pass("foreign-pass", no_op_pass());
    REQUIRE(foreign_resource);
    REQUIRE(foreign_pass);
    REQUIRE_FALSE(builder.write(
        cube,
        foreign_resource.value(),
        ResourceState::render_target));
    REQUIRE_FALSE(builder.write(
        foreign_pass.value(),
        back_buffer,
        ResourceState::render_target));
    REQUIRE_FALSE(builder.add_dependency(
        cube,
        foreign_pass.value()));
}

TEST_CASE(
    "render graph builder moves transfer ownership and compilation consumes",
    "[render-graph][validation][lifecycle]")
{
    using namespace shark;
    using namespace render_graph;

    GraphBuilder source;
    const auto resource = source.import_resource(
        "back-buffer",
        ExternalResourceId{1},
        ResourceState::present,
        ResourceState::present);
    const auto pass = source.add_pass("cube", no_op_pass());
    REQUIRE(resource);
    REQUIRE(pass);

    GraphBuilder destination{std::move(source)};
    REQUIRE(destination.write(
        pass.value(),
        resource.value(),
        ResourceState::render_target));

    const auto moved_from_resource = source.import_resource(
        "new-back-buffer",
        ExternalResourceId{2},
        ResourceState::present,
        ResourceState::present);
    const auto moved_from_pass =
        source.add_pass("new-cube", no_op_pass());
    REQUIRE(moved_from_resource);
    REQUIRE(moved_from_pass);
    REQUIRE_FALSE(destination.write(
        pass.value(),
        moved_from_resource.value(),
        ResourceState::render_target));
    REQUIRE_FALSE(source.write(
        moved_from_pass.value(),
        resource.value(),
        ResourceState::render_target));

    GraphBuilder assigned;
    const auto replaced_resource = assigned.import_resource(
        "replaced",
        ExternalResourceId{7},
        ResourceState::present,
        ResourceState::present);
    const auto replaced_pass =
        assigned.add_pass("replaced-pass", no_op_pass());
    REQUIRE(replaced_resource);
    REQUIRE(replaced_pass);
    assigned = std::move(source);
    REQUIRE(assigned.write(
        moved_from_pass.value(),
        moved_from_resource.value(),
        ResourceState::render_target));
    REQUIRE_FALSE(assigned.write(
        replaced_pass.value(),
        replaced_resource.value(),
        ResourceState::render_target));

    const auto reassigned_source_resource = source.import_resource(
        "reassigned-source",
        ExternalResourceId{8},
        ResourceState::present,
        ResourceState::present);
    REQUIRE(reassigned_source_resource);
    REQUIRE_FALSE(assigned.write(
        moved_from_pass.value(),
        reassigned_source_resource.value(),
        ResourceState::render_target));

    auto compiled = std::move(destination).compile();
    REQUIRE(compiled);
    REQUIRE_FALSE(destination.import_resource(
        "after-compile",
        ExternalResourceId{3},
        ResourceState::present,
        ResourceState::present));
    REQUIRE_FALSE(destination.add_pass(
        "after-compile",
        no_op_pass()));
    REQUIRE_FALSE(destination.write(
        pass.value(),
        resource.value(),
        ResourceState::render_target));
    REQUIRE_FALSE(destination.add_dependency(
        pass.value(),
        pass.value()));
    REQUIRE_FALSE(std::move(destination).compile());
}

TEST_CASE(
    "independent render passes compile and execute in declaration order",
    "[render-graph][ordering]")
{
    using namespace shark;
    using namespace render_graph;

    std::vector<std::string> execution_order;
    GraphBuilder builder;
    REQUIRE(builder.add_pass(
        "alpha",
        [&execution_order](const PassContext&) {
            execution_order.emplace_back("alpha");
            return core::Result<void>::success();
        }));
    REQUIRE(builder.add_pass(
        "beta",
        [&execution_order](const PassContext&) {
            execution_order.emplace_back("beta");
            return core::Result<void>::success();
        }));
    REQUIRE(builder.add_pass(
        "gamma",
        [&execution_order](const PassContext&) {
            execution_order.emplace_back("gamma");
            return core::Result<void>::success();
        }));

    auto graph = compile_graph(std::move(builder));
    REQUIRE(graph.passes().size() == 3);
    REQUIRE(graph.passes()[0].name == "alpha");
    REQUIRE(graph.passes()[1].name == "beta");
    REQUIRE(graph.passes()[2].name == "gamma");
    REQUIRE((graph.stats() == CompiledGraphStats{
        .imported_resource_count = 0,
        .pass_count = 3,
        .dependency_count = 0,
        .transition_count = 0,
        .elided_transition_count = 0,
    }));

    RecordingTransitionRecorder recorder;
    const auto execution = graph.execute(recorder);
    REQUIRE(execution);
    REQUIRE((execution.value() == ExecutionStats{
        .passes_executed = 3,
        .transitions_recorded = 0,
    }));
    REQUIRE((execution_order ==
        std::vector<std::string>{"alpha", "beta", "gamma"}));
}

TEST_CASE(
    "explicit dependencies use a stable topological order",
    "[render-graph][ordering][dependencies]")
{
    using namespace shark;
    using namespace render_graph;

    GraphBuilder builder;
    const auto alpha = builder.add_pass("alpha", no_op_pass());
    const auto beta = builder.add_pass("beta", no_op_pass());
    const auto gamma = builder.add_pass("gamma", no_op_pass());
    REQUIRE(alpha);
    REQUIRE(beta);
    REQUIRE(gamma);
    REQUIRE(builder.add_dependency(gamma.value(), alpha.value()));
    REQUIRE(builder.add_dependency(gamma.value(), alpha.value()));

    auto graph = compile_graph(std::move(builder));
    REQUIRE(graph.passes()[0].name == "beta");
    REQUIRE(graph.passes()[1].name == "gamma");
    REQUIRE(graph.passes()[2].name == "alpha");
    const auto& compiled_alpha = find_pass(graph, "alpha");
    REQUIRE((compiled_alpha.dependencies ==
        std::vector<PassHandle>{gamma.value()}));
    REQUIRE(graph.stats().dependency_count == 1);
}

TEST_CASE(
    "render graph derives exact RAW WAR and WAW dependencies",
    "[render-graph][hazards][dependencies]")
{
    using namespace shark;
    using namespace render_graph;

    GraphBuilder builder;
    const auto resource = builder.import_resource(
        "shared",
        ExternalResourceId{4},
        ResourceState::common,
        ResourceState::common);
    const auto write_zero =
        builder.add_pass("write-zero", no_op_pass());
    const auto read_zero =
        builder.add_pass("read-zero", no_op_pass());
    const auto read_one =
        builder.add_pass("read-one", no_op_pass());
    const auto write_one =
        builder.add_pass("write-one", no_op_pass());
    REQUIRE(resource);
    REQUIRE(write_zero);
    REQUIRE(read_zero);
    REQUIRE(read_one);
    REQUIRE(write_one);
    REQUIRE(builder.write(
        write_zero.value(),
        resource.value(),
        ResourceState::render_target));
    REQUIRE(builder.read(
        read_zero.value(),
        resource.value(),
        ResourceState::shader_read));
    REQUIRE(builder.read(
        read_one.value(),
        resource.value(),
        ResourceState::shader_read));
    REQUIRE(builder.write(
        write_one.value(),
        resource.value(),
        ResourceState::copy_destination));

    auto graph = compile_graph(std::move(builder));
    REQUIRE(find_pass(graph, "write-zero").dependencies.empty());
    REQUIRE((find_pass(graph, "read-zero").dependencies ==
        std::vector<PassHandle>{write_zero.value()}));
    REQUIRE((find_pass(graph, "read-one").dependencies ==
        std::vector<PassHandle>{write_zero.value()}));
    REQUIRE((find_pass(graph, "write-one").dependencies ==
        std::vector<PassHandle>{
            write_zero.value(),
            read_zero.value(),
            read_one.value(),
        }));
    REQUIRE(graph.stats().dependency_count == 5);
}

TEST_CASE(
    "read-only render passes create no false resource dependency",
    "[render-graph][hazards]")
{
    using namespace shark;
    using namespace render_graph;

    GraphBuilder builder;
    const auto texture = builder.import_resource(
        "texture",
        ExternalResourceId{2},
        ResourceState::shader_read,
        ResourceState::shader_read);
    const auto first = builder.add_pass("first", no_op_pass());
    const auto second = builder.add_pass("second", no_op_pass());
    REQUIRE(texture);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(builder.read(
        first.value(),
        texture.value(),
        ResourceState::shader_read));
    REQUIRE(builder.read(
        second.value(),
        texture.value(),
        ResourceState::shader_read));

    auto graph = compile_graph(std::move(builder));
    REQUIRE(find_pass(graph, "first").dependencies.empty());
    REQUIRE(find_pass(graph, "second").dependencies.empty());
    REQUIRE(graph.stats().transition_count == 0);
    REQUIRE(graph.stats().elided_transition_count == 3);
}

TEST_CASE(
    "render graph compilation rejects dependency cycles",
    "[render-graph][dependencies][cycle]")
{
    using namespace shark;
    using namespace render_graph;

    GraphBuilder builder;
    const auto resource = builder.import_resource(
        "shared",
        ExternalResourceId{1},
        ResourceState::common,
        ResourceState::shader_read);
    const auto writer = builder.add_pass("writer", no_op_pass());
    const auto reader = builder.add_pass("reader", no_op_pass());
    REQUIRE(resource);
    REQUIRE(writer);
    REQUIRE(reader);
    REQUIRE(builder.write(
        writer.value(),
        resource.value(),
        ResourceState::copy_destination));
    REQUIRE(builder.read(
        reader.value(),
        resource.value(),
        ResourceState::shader_read));
    REQUIRE(builder.add_dependency(reader.value(), writer.value()));

    const auto compiled = std::move(builder).compile();
    REQUIRE_FALSE(compiled);
    REQUIRE(compiled.error().category() ==
        core::ErrorCategory::graphics);
    REQUIRE(compiled.error().code() ==
        core::ErrorCode::invalid_state);
    REQUIRE(compiled.error().message().find("writer") !=
        std::string_view::npos);
    REQUIRE(compiled.error().message().find("reader") !=
        std::string_view::npos);
}

TEST_CASE(
    "imported attachments compile exact pre-pass and final transitions",
    "[render-graph][transitions][execution]")
{
    using namespace shark;
    using namespace render_graph;

    bool callback_validated = false;
    GraphBuilder builder;
    const auto back_buffer = builder.import_resource(
        "back-buffer",
        ExternalResourceId{7},
        ResourceState::present,
        ResourceState::present);
    const auto depth = builder.import_resource(
        "depth",
        ExternalResourceId{9},
        ResourceState::depth_write,
        ResourceState::depth_write);
    REQUIRE(back_buffer);
    REQUIRE(depth);
    const auto cube = builder.add_pass(
        "cube",
        [back_buffer = back_buffer.value(),
         depth = depth.value(),
         &callback_validated](const PassContext& context) {
            const auto color = context.write(back_buffer);
            const auto depth_target = context.write(depth);
            if (!color || !depth_target) {
                return core::Result<void>::failure(core::Error{
                    core::ErrorCategory::graphics,
                    core::ErrorCode::invalid_state,
                    "declared cube resources were unavailable",
                });
            }
            callback_validated =
                color.value() == ExternalResourceId{7} &&
                depth_target.value() == ExternalResourceId{9};
            return core::Result<void>::success();
        });
    REQUIRE(cube);
    REQUIRE(builder.write(
        cube.value(),
        back_buffer.value(),
        ResourceState::render_target));
    REQUIRE(builder.write(
        cube.value(),
        depth.value(),
        ResourceState::depth_write));

    auto graph = compile_graph(std::move(builder));
    const auto& compiled_cube = find_pass(graph, "cube");
    REQUIRE(compiled_cube.transitions.size() == 1);
    REQUIRE((compiled_cube.transitions.front() == ResourceTransition{
        back_buffer.value(),
        ExternalResourceId{7},
        ResourceState::present,
        ResourceState::render_target,
    }));
    REQUIRE(graph.final_transitions().size() == 1);
    REQUIRE((graph.final_transitions().front() == ResourceTransition{
        back_buffer.value(),
        ExternalResourceId{7},
        ResourceState::render_target,
        ResourceState::present,
    }));
    REQUIRE((graph.stats() == CompiledGraphStats{
        .imported_resource_count = 2,
        .pass_count = 1,
        .dependency_count = 0,
        .transition_count = 2,
        .elided_transition_count = 2,
    }));

    RecordingTransitionRecorder recorder;
    const auto execution = graph.execute(recorder);
    REQUIRE(execution);
    REQUIRE(callback_validated);
    REQUIRE((execution.value() == ExecutionStats{
        .passes_executed = 1,
        .transitions_recorded = 2,
    }));
    REQUIRE(recorder.transitions.size() == 2);
    REQUIRE(recorder.transitions[0] ==
        compiled_cube.transitions[0]);
    REQUIRE(recorder.transitions[1] ==
        graph.final_transitions()[0]);
}

TEST_CASE(
    "terrain cube then skybox graph compiles exact opaque ordering",
    "[render-graph][transitions][dependencies][terrain][skybox]")
{
    using namespace shark;
    using namespace render_graph;

    std::vector<std::string> execution_order;
    GraphBuilder builder;
    const auto back_buffer = builder.import_resource(
        "BackBuffer",
        ExternalResourceId{1},
        ResourceState::present,
        ResourceState::present);
    const auto depth = builder.import_resource(
        "Depth",
        ExternalResourceId{2},
        ResourceState::depth_write,
        ResourceState::depth_write);
    const auto checker = builder.import_resource(
        "Checker",
        ExternalResourceId{3},
        ResourceState::pixel_shader_read,
        ResourceState::pixel_shader_read);
    const auto cube_vertex_buffer = builder.import_resource(
        "CubeVertexBuffer",
        ExternalResourceId{4},
        ResourceState::vertex_buffer,
        ResourceState::vertex_buffer);
    const auto cube_index_buffer = builder.import_resource(
        "CubeIndexBuffer",
        ExternalResourceId{5},
        ResourceState::index_buffer,
        ResourceState::index_buffer);
    const auto terrain_vertex_buffer = builder.import_resource(
        "TerrainVertexBuffer",
        ExternalResourceId{6},
        ResourceState::vertex_buffer,
        ResourceState::vertex_buffer);
    const auto terrain_index_buffer = builder.import_resource(
        "TerrainIndexBuffer",
        ExternalResourceId{7},
        ResourceState::index_buffer,
        ResourceState::index_buffer);
    REQUIRE(back_buffer);
    REQUIRE(depth);
    REQUIRE(checker);
    REQUIRE(cube_vertex_buffer);
    REQUIRE(cube_index_buffer);
    REQUIRE(terrain_vertex_buffer);
    REQUIRE(terrain_index_buffer);

    const auto terrain = builder.add_pass(
        "Terrain",
        [back_buffer = back_buffer.value(),
         depth = depth.value(),
         terrain_vertex_buffer = terrain_vertex_buffer.value(),
         terrain_index_buffer = terrain_index_buffer.value(),
         &execution_order](const PassContext& context) {
            const auto color = context.write(back_buffer);
            const auto depth_target = context.write(depth);
            const auto vertices = context.read(
                terrain_vertex_buffer);
            const auto indices = context.read(
                terrain_index_buffer);
            if (!color || !depth_target || !vertices || !indices) {
                return core::Result<void>::failure(core::Error{
                    core::ErrorCategory::graphics,
                    core::ErrorCode::invalid_state,
                    "declared terrain resources were unavailable",
                });
            }
            execution_order.emplace_back("Terrain");
            return core::Result<void>::success();
        });
    const auto cube = builder.add_pass(
        "TexturedCube",
        [back_buffer = back_buffer.value(),
         depth = depth.value(),
         checker = checker.value(),
         cube_vertex_buffer = cube_vertex_buffer.value(),
         cube_index_buffer = cube_index_buffer.value(),
         &execution_order](const PassContext& context) {
            const auto color = context.write(back_buffer);
            const auto depth_target = context.write(depth);
            const auto texture = context.read(checker);
            const auto vertices = context.read(cube_vertex_buffer);
            const auto indices = context.read(cube_index_buffer);
            if (!color || !depth_target || !texture ||
                !vertices || !indices) {
                return core::Result<void>::failure(core::Error{
                    core::ErrorCategory::graphics,
                    core::ErrorCode::invalid_state,
                    "declared cube resources were unavailable",
                });
            }
            execution_order.emplace_back("TexturedCube");
            return core::Result<void>::success();
        });
    const auto skybox = builder.add_pass(
        "Skybox",
        [back_buffer = back_buffer.value(),
         depth = depth.value(),
         cube_vertex_buffer = cube_vertex_buffer.value(),
         cube_index_buffer = cube_index_buffer.value(),
         &execution_order](const PassContext& context) {
            const auto color = context.write(back_buffer);
            const auto depth_target = context.read(depth);
            const auto vertices = context.read(cube_vertex_buffer);
            const auto indices = context.read(cube_index_buffer);
            if (!color || !depth_target || !vertices || !indices) {
                return core::Result<void>::failure(core::Error{
                    core::ErrorCategory::graphics,
                    core::ErrorCode::invalid_state,
                    "declared skybox resources were unavailable",
                });
            }
            execution_order.emplace_back("Skybox");
            return core::Result<void>::success();
        });
    REQUIRE(terrain);
    REQUIRE(cube);
    REQUIRE(skybox);
    REQUIRE(builder.write(
        terrain.value(),
        back_buffer.value(),
        ResourceState::render_target));
    REQUIRE(builder.write(
        terrain.value(),
        depth.value(),
        ResourceState::depth_write));
    REQUIRE(builder.read(
        terrain.value(),
        terrain_vertex_buffer.value(),
        ResourceState::vertex_buffer));
    REQUIRE(builder.read(
        terrain.value(),
        terrain_index_buffer.value(),
        ResourceState::index_buffer));
    REQUIRE(builder.write(
        cube.value(),
        back_buffer.value(),
        ResourceState::render_target));
    REQUIRE(builder.write(
        cube.value(),
        depth.value(),
        ResourceState::depth_write));
    REQUIRE(builder.read(
        cube.value(),
        checker.value(),
        ResourceState::pixel_shader_read));
    REQUIRE(builder.read(
        cube.value(),
        cube_vertex_buffer.value(),
        ResourceState::vertex_buffer));
    REQUIRE(builder.read(
        cube.value(),
        cube_index_buffer.value(),
        ResourceState::index_buffer));
    REQUIRE(builder.write(
        skybox.value(),
        back_buffer.value(),
        ResourceState::render_target));
    REQUIRE(builder.read(
        skybox.value(),
        depth.value(),
        ResourceState::depth_read));
    REQUIRE(builder.read(
        skybox.value(),
        cube_vertex_buffer.value(),
        ResourceState::vertex_buffer));
    REQUIRE(builder.read(
        skybox.value(),
        cube_index_buffer.value(),
        ResourceState::index_buffer));

    auto graph = compile_graph(std::move(builder));
    REQUIRE(graph.passes().size() == 3);
    REQUIRE(graph.passes()[0].name == "Terrain");
    REQUIRE(graph.passes()[1].name == "TexturedCube");
    REQUIRE(graph.passes()[2].name == "Skybox");

    const auto& compiled_terrain = find_pass(graph, "Terrain");
    const auto& compiled_cube = find_pass(graph, "TexturedCube");
    const auto& compiled_skybox = find_pass(graph, "Skybox");
    REQUIRE(compiled_terrain.dependencies.empty());
    REQUIRE((compiled_cube.dependencies ==
        std::vector<PassHandle>{terrain.value()}));
    REQUIRE((compiled_skybox.dependencies ==
        std::vector<PassHandle>{cube.value()}));
    REQUIRE(compiled_terrain.accesses.size() == 4);
    REQUIRE(
        compiled_terrain.accesses[2].resource ==
        terrain_vertex_buffer.value());
    REQUIRE(
        compiled_terrain.accesses[2].state ==
        ResourceState::vertex_buffer);
    REQUIRE(
        compiled_terrain.accesses[2].mode == AccessMode::read);
    REQUIRE(
        compiled_terrain.accesses[3].resource ==
        terrain_index_buffer.value());
    REQUIRE(
        compiled_terrain.accesses[3].state ==
        ResourceState::index_buffer);
    REQUIRE(
        compiled_terrain.accesses[3].mode == AccessMode::read);
    REQUIRE(compiled_cube.accesses.size() == 5);
    REQUIRE(
        compiled_cube.accesses[3].resource ==
        cube_vertex_buffer.value());
    REQUIRE(
        compiled_cube.accesses[3].state ==
        ResourceState::vertex_buffer);
    REQUIRE(compiled_cube.accesses[3].mode == AccessMode::read);
    REQUIRE(
        compiled_cube.accesses[4].resource ==
        cube_index_buffer.value());
    REQUIRE(
        compiled_cube.accesses[4].state ==
        ResourceState::index_buffer);
    REQUIRE(compiled_cube.accesses[4].mode == AccessMode::read);
    REQUIRE(compiled_skybox.accesses.size() == 4);
    REQUIRE(
        compiled_skybox.accesses[2].resource ==
        cube_vertex_buffer.value());
    REQUIRE(
        compiled_skybox.accesses[2].state ==
        ResourceState::vertex_buffer);
    REQUIRE(compiled_skybox.accesses[2].mode == AccessMode::read);
    REQUIRE(
        compiled_skybox.accesses[3].resource ==
        cube_index_buffer.value());
    REQUIRE(
        compiled_skybox.accesses[3].state ==
        ResourceState::index_buffer);
    REQUIRE(compiled_skybox.accesses[3].mode == AccessMode::read);
    REQUIRE(compiled_terrain.transitions.size() == 1);
    REQUIRE((compiled_terrain.transitions[0] == ResourceTransition{
        back_buffer.value(),
        ExternalResourceId{1},
        ResourceState::present,
        ResourceState::render_target,
    }));
    REQUIRE(compiled_cube.transitions.empty());
    REQUIRE(compiled_skybox.transitions.size() == 1);
    REQUIRE((compiled_skybox.transitions[0] == ResourceTransition{
        depth.value(),
        ExternalResourceId{2},
        ResourceState::depth_write,
        ResourceState::depth_read,
    }));
    REQUIRE(graph.final_transitions().size() == 2);
    REQUIRE((graph.final_transitions()[0] == ResourceTransition{
        back_buffer.value(),
        ExternalResourceId{1},
        ResourceState::render_target,
        ResourceState::present,
    }));
    REQUIRE((graph.final_transitions()[1] == ResourceTransition{
        depth.value(),
        ExternalResourceId{2},
        ResourceState::depth_read,
        ResourceState::depth_write,
    }));
    REQUIRE((graph.stats() == CompiledGraphStats{
        .imported_resource_count = 7,
        .pass_count = 3,
        .dependency_count = 2,
        .transition_count = 4,
        .elided_transition_count = 16,
    }));

    RecordingTransitionRecorder recorder;
    const auto execution = graph.execute(recorder);
    REQUIRE(execution);
    REQUIRE((execution.value() == ExecutionStats{
        .passes_executed = 3,
        .transitions_recorded = 4,
    }));
    REQUIRE((execution_order ==
        std::vector<std::string>{
            "Terrain",
            "TexturedCube",
            "Skybox",
        }));
    REQUIRE(recorder.transitions.size() == 4);
    REQUIRE(recorder.transitions[0] ==
        compiled_terrain.transitions[0]);
    REQUIRE(recorder.transitions[1] ==
        compiled_skybox.transitions[0]);
    REQUIRE(recorder.transitions[2] ==
        graph.final_transitions()[0]);
    REQUIRE(recorder.transitions[3] ==
        graph.final_transitions()[1]);
}

TEST_CASE(
    "same-state accesses elide redundant render graph transitions",
    "[render-graph][transitions]")
{
    using namespace shark;
    using namespace render_graph;

    GraphBuilder builder;
    const auto texture = builder.import_resource(
        "texture",
        ExternalResourceId{3},
        ResourceState::common,
        ResourceState::shader_read);
    const auto upload = builder.add_pass("upload", no_op_pass());
    const auto sample = builder.add_pass("sample", no_op_pass());
    const auto sample_again =
        builder.add_pass("sample-again", no_op_pass());
    REQUIRE(texture);
    REQUIRE(upload);
    REQUIRE(sample);
    REQUIRE(sample_again);
    REQUIRE(builder.write(
        upload.value(),
        texture.value(),
        ResourceState::copy_destination));
    REQUIRE(builder.read(
        sample.value(),
        texture.value(),
        ResourceState::shader_read));
    REQUIRE(builder.read(
        sample_again.value(),
        texture.value(),
        ResourceState::shader_read));

    auto graph = compile_graph(std::move(builder));
    REQUIRE(find_pass(graph, "upload").transitions.size() == 1);
    REQUIRE(find_pass(graph, "sample").transitions.size() == 1);
    REQUIRE(find_pass(graph, "sample-again").transitions.empty());
    REQUIRE(graph.final_transitions().empty());
    REQUIRE(graph.stats().transition_count == 2);
    REQUIRE(graph.stats().elided_transition_count == 2);
}

TEST_CASE(
    "common and present imports elide their shared legacy state",
    "[render-graph][transitions][legacy]")
{
    using namespace shark;
    using namespace render_graph;

    GraphBuilder builder;
    REQUIRE(builder.import_resource(
        "present-alias",
        ExternalResourceId{6},
        ResourceState::common,
        ResourceState::present));

    auto graph = compile_graph(std::move(builder));
    REQUIRE(graph.final_transitions().empty());
    REQUIRE(graph.stats().transition_count == 0);
    REQUIRE(graph.stats().elided_transition_count == 1);
}

TEST_CASE(
    "pass contexts reject undeclared and wrong-mode resource access",
    "[render-graph][execution][validation]")
{
    using namespace shark;
    using namespace render_graph;

    bool correct_access = false;
    core::ErrorCode wrong_mode_code = core::ErrorCode::unknown;
    core::ErrorCode undeclared_code = core::ErrorCode::unknown;
    core::ErrorCode invalid_handle_code = core::ErrorCode::unknown;

    GraphBuilder builder;
    const auto back_buffer = builder.import_resource(
        "back-buffer",
        ExternalResourceId{5},
        ResourceState::present,
        ResourceState::present);
    const auto depth = builder.import_resource(
        "depth",
        ExternalResourceId{6},
        ResourceState::depth_write,
        ResourceState::depth_write);
    REQUIRE(back_buffer);
    REQUIRE(depth);
    const auto pass = builder.add_pass(
        "color-only",
        [back_buffer = back_buffer.value(),
         depth = depth.value(),
         &correct_access,
         &wrong_mode_code,
         &undeclared_code,
         &invalid_handle_code](const PassContext& context) {
            const auto write_result = context.write(back_buffer);
            correct_access =
                write_result &&
                write_result.value() == ExternalResourceId{5};

            const auto wrong_mode = context.read(back_buffer);
            if (!wrong_mode) {
                wrong_mode_code = wrong_mode.error().code();
            }
            const auto undeclared = context.write(depth);
            if (!undeclared) {
                undeclared_code = undeclared.error().code();
            }
            const auto invalid = context.write(ResourceHandle{});
            if (!invalid) {
                invalid_handle_code = invalid.error().code();
            }
            return core::Result<void>::success();
        });
    REQUIRE(pass);
    REQUIRE(builder.write(
        pass.value(),
        back_buffer.value(),
        ResourceState::render_target));

    auto graph = compile_graph(std::move(builder));
    RecordingTransitionRecorder recorder;
    REQUIRE(graph.execute(recorder));
    REQUIRE(correct_access);
    REQUIRE(wrong_mode_code == core::ErrorCode::invalid_state);
    REQUIRE(undeclared_code == core::ErrorCode::invalid_state);
    REQUIRE(invalid_handle_code ==
        core::ErrorCode::invalid_argument);
}

TEST_CASE(
    "render graph execution stops at the first callback failure",
    "[render-graph][execution][failure]")
{
    using namespace shark;
    using namespace render_graph;

    std::vector<std::string> execution_order;
    GraphBuilder builder;
    REQUIRE(builder.add_pass(
        "first",
        [&execution_order](const PassContext&) {
            execution_order.emplace_back("first");
            return core::Result<void>::success();
        }));
    REQUIRE(builder.add_pass(
        "failing",
        [&execution_order](const PassContext&) {
            execution_order.emplace_back("failing");
            return core::Result<void>::failure(core::Error{
                core::ErrorCategory::graphics,
                core::ErrorCode::operation_failed,
                "synthetic pass failure",
            });
        }));
    REQUIRE(builder.add_pass(
        "unreached",
        [&execution_order](const PassContext&) {
            execution_order.emplace_back("unreached");
            return core::Result<void>::success();
        }));

    auto graph = compile_graph(std::move(builder));
    RecordingTransitionRecorder recorder;
    const auto execution = graph.execute(recorder);
    REQUIRE_FALSE(execution);
    REQUIRE(execution.error().code() ==
        core::ErrorCode::operation_failed);
    REQUIRE((execution_order ==
        std::vector<std::string>{"first", "failing"}));
}

TEST_CASE(
    "transition recorder failure stops execution before its pass",
    "[render-graph][execution][failure]")
{
    using namespace shark;
    using namespace render_graph;

    bool callback_ran = false;
    GraphBuilder builder;
    const auto back_buffer = builder.import_resource(
        "back-buffer",
        ExternalResourceId{1},
        ResourceState::present,
        ResourceState::present);
    REQUIRE(back_buffer);
    const auto pass = builder.add_pass(
        "cube",
        [&callback_ran](const PassContext&) {
            callback_ran = true;
            return core::Result<void>::success();
        });
    REQUIRE(pass);
    REQUIRE(builder.write(
        pass.value(),
        back_buffer.value(),
        ResourceState::render_target));

    auto graph = compile_graph(std::move(builder));
    RecordingTransitionRecorder recorder;
    recorder.fail_at(0);
    const auto execution = graph.execute(recorder);
    REQUIRE_FALSE(execution);
    REQUIRE(execution.error().code() ==
        core::ErrorCode::unavailable);
    REQUIRE_FALSE(callback_ran);
    REQUIRE(recorder.transitions.empty());
}
