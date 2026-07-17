#include "render_graph_executor.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

TEST_CASE(
    "render graph states map to exact legacy D3D12 resource states",
    "[gpu][render-graph][barrier]")
{
    using namespace shark;
    using render_graph::ResourceState;
    using rhi::d3d12::detail::legacy_resource_state;

    REQUIRE(
        legacy_resource_state(ResourceState::common).value() ==
        D3D12_RESOURCE_STATE_COMMON);
    REQUIRE(
        legacy_resource_state(ResourceState::present).value() ==
        D3D12_RESOURCE_STATE_PRESENT);
    REQUIRE(
        legacy_resource_state(ResourceState::render_target).value() ==
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    REQUIRE(
        legacy_resource_state(ResourceState::depth_write).value() ==
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    REQUIRE(
        legacy_resource_state(ResourceState::depth_read).value() ==
        D3D12_RESOURCE_STATE_DEPTH_READ);
    REQUIRE(
        legacy_resource_state(ResourceState::pixel_shader_read).value() ==
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    REQUIRE(
        legacy_resource_state(ResourceState::vertex_buffer).value() ==
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    REQUIRE(
        legacy_resource_state(ResourceState::index_buffer).value() ==
        D3D12_RESOURCE_STATE_INDEX_BUFFER);
    REQUIRE(
        legacy_resource_state(ResourceState::shader_read).value() ==
        D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    REQUIRE(
        legacy_resource_state(ResourceState::copy_source).value() ==
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    REQUIRE(
        legacy_resource_state(ResourceState::copy_destination).value() ==
        D3D12_RESOURCE_STATE_COPY_DEST);
    REQUIRE_FALSE(legacy_resource_state(
        static_cast<ResourceState>(255)));
}

TEST_CASE(
    "legacy render graph barriers resolve imported resources exactly",
    "[gpu][render-graph][barrier]")
{
    using namespace shark;
    using namespace rhi::d3d12::detail;

    constexpr render_graph::ExternalResourceId back_buffer_id{7};
    auto* const resource = reinterpret_cast<ID3D12Resource*>(
        std::uintptr_t{0x1000});
    const std::array resources{
        RenderGraphResourceBinding{back_buffer_id, resource},
    };
    REQUIRE(validate_render_graph_resource_bindings(resources));
    const render_graph::ResourceTransition transition{
        render_graph::ResourceHandle{},
        back_buffer_id,
        render_graph::ResourceState::present,
        render_graph::ResourceState::render_target,
    };

    const auto result = make_legacy_transition_barrier(
        transition,
        resources);
    REQUIRE(result);
    const auto& barrier = result.value();
    REQUIRE(barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION);
    REQUIRE(barrier.Flags == D3D12_RESOURCE_BARRIER_FLAG_NONE);
    REQUIRE(barrier.Transition.pResource == resource);
    REQUIRE(
        barrier.Transition.Subresource ==
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    REQUIRE(
        barrier.Transition.StateBefore ==
        D3D12_RESOURCE_STATE_PRESENT);
    REQUIRE(
        barrier.Transition.StateAfter ==
        D3D12_RESOURCE_STATE_RENDER_TARGET);
}

TEST_CASE(
    "legacy render graph barriers transition depth for skybox reads",
    "[gpu][render-graph][barrier][skybox]")
{
    using namespace shark;
    using namespace rhi::d3d12::detail;

    constexpr render_graph::ExternalResourceId depth_id{9};
    auto* const resource = reinterpret_cast<ID3D12Resource*>(
        std::uintptr_t{0x3000});
    const std::array resources{
        RenderGraphResourceBinding{depth_id, resource},
    };
    const render_graph::ResourceTransition transition{
        render_graph::ResourceHandle{},
        depth_id,
        render_graph::ResourceState::depth_write,
        render_graph::ResourceState::depth_read,
    };

    const auto result = make_legacy_transition_barrier(
        transition,
        resources);
    REQUIRE(result);
    const auto& barrier = result.value();
    REQUIRE(barrier.Transition.pResource == resource);
    REQUIRE(
        barrier.Transition.StateBefore ==
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    REQUIRE(
        barrier.Transition.StateAfter ==
        D3D12_RESOURCE_STATE_DEPTH_READ);
}

TEST_CASE(
    "legacy render graph barriers reject invalid resource bindings",
    "[gpu][render-graph][barrier]")
{
    using namespace shark;
    using namespace rhi::d3d12::detail;

    constexpr render_graph::ExternalResourceId resource_id{4};
    const render_graph::ResourceTransition transition{
        render_graph::ResourceHandle{},
        resource_id,
        render_graph::ResourceState::present,
        render_graph::ResourceState::render_target,
    };

    const std::array<RenderGraphResourceBinding, 0> missing{};
    REQUIRE_FALSE(make_legacy_transition_barrier(
        transition,
        missing));

    auto* const first = reinterpret_cast<ID3D12Resource*>(
        std::uintptr_t{0x1000});
    auto* const second = reinterpret_cast<ID3D12Resource*>(
        std::uintptr_t{0x2000});
    const std::array duplicates{
        RenderGraphResourceBinding{resource_id, first},
        RenderGraphResourceBinding{resource_id, second},
    };
    REQUIRE_FALSE(validate_render_graph_resource_bindings(
        duplicates));
    REQUIRE_FALSE(make_legacy_transition_barrier(
        transition,
        duplicates));

    const std::array null_then_duplicate{
        RenderGraphResourceBinding{resource_id, nullptr},
        RenderGraphResourceBinding{resource_id, second},
    };
    REQUIRE_FALSE(make_legacy_transition_barrier(
        transition,
        null_then_duplicate));

    const std::array null_resource{
        RenderGraphResourceBinding{resource_id, nullptr},
    };
    REQUIRE_FALSE(validate_render_graph_resource_bindings(
        null_resource));
    REQUIRE_FALSE(make_legacy_transition_barrier(
        transition,
        null_resource));

    const auto no_op = render_graph::ResourceTransition{
        render_graph::ResourceHandle{},
        resource_id,
        render_graph::ResourceState::present,
        render_graph::ResourceState::present,
    };
    const std::array valid{
        RenderGraphResourceBinding{resource_id, first},
    };
    REQUIRE_FALSE(make_legacy_transition_barrier(no_op, valid));

    const auto native_alias = render_graph::ResourceTransition{
        render_graph::ResourceHandle{},
        resource_id,
        render_graph::ResourceState::common,
        render_graph::ResourceState::present,
    };
    REQUIRE_FALSE(make_legacy_transition_barrier(
        native_alias,
        valid));

    LegacyRenderGraphTransitionRecorder recorder{nullptr, valid};
    REQUIRE_FALSE(recorder.transition(transition));
    REQUIRE(recorder.recorded_transition_count() == 0);
}
