#include "render_graph_executor.hpp"

#include <shark/core/error.hpp>

#include <string>
#include <utility>

namespace shark::rhi::d3d12::detail {
namespace {

[[nodiscard]] core::Error render_graph_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::graphics,
        code,
        std::move(message),
    };
}

[[nodiscard]] core::Result<ID3D12Resource*> resolve_resource(
    const render_graph::ExternalResourceId external,
    const std::span<const RenderGraphResourceBinding> resources)
{
    ID3D12Resource* resolved = nullptr;
    std::size_t match_count = 0;
    for (const auto& binding : resources) {
        if (binding.external != external) {
            continue;
        }
        ++match_count;
        resolved = binding.resource;
    }

    if (match_count == 0) {
        return core::Result<ID3D12Resource*>::failure(render_graph_error(
            core::ErrorCode::not_found,
            "A render-graph transition references an unbound D3D12 "
            "resource"));
    }
    if (match_count != 1) {
        return core::Result<ID3D12Resource*>::failure(
            render_graph_error(
                core::ErrorCode::invalid_state,
                "A render-graph external resource ID has more than "
                "one D3D12 binding"));
    }
    if (resolved == nullptr) {
        return core::Result<ID3D12Resource*>::failure(render_graph_error(
            core::ErrorCode::invalid_argument,
            "A render-graph D3D12 binding cannot contain a null "
            "resource"));
    }
    return core::Result<ID3D12Resource*>::success(resolved);
}

} // namespace

core::Result<D3D12_RESOURCE_STATES> legacy_resource_state(
    const render_graph::ResourceState state)
{
    using render_graph::ResourceState;
    switch (state) {
    case ResourceState::common:
        return core::Result<D3D12_RESOURCE_STATES>::success(
            D3D12_RESOURCE_STATE_COMMON);
    case ResourceState::present:
        return core::Result<D3D12_RESOURCE_STATES>::success(
            D3D12_RESOURCE_STATE_PRESENT);
    case ResourceState::render_target:
        return core::Result<D3D12_RESOURCE_STATES>::success(
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    case ResourceState::depth_write:
        return core::Result<D3D12_RESOURCE_STATES>::success(
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
    case ResourceState::shader_read:
        return core::Result<D3D12_RESOURCE_STATES>::success(
            D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    case ResourceState::copy_source:
        return core::Result<D3D12_RESOURCE_STATES>::success(
            D3D12_RESOURCE_STATE_COPY_SOURCE);
    case ResourceState::copy_destination:
        return core::Result<D3D12_RESOURCE_STATES>::success(
            D3D12_RESOURCE_STATE_COPY_DEST);
    }

    return core::Result<D3D12_RESOURCE_STATES>::failure(
        render_graph_error(
            core::ErrorCode::invalid_argument,
            "The render graph requested an unknown legacy D3D12 state"));
}

core::Result<void> validate_render_graph_resource_bindings(
    const std::span<const RenderGraphResourceBinding> resources)
{
    for (std::size_t index = 0; index < resources.size(); ++index) {
        if (resources[index].resource == nullptr) {
            return core::Result<void>::failure(render_graph_error(
                core::ErrorCode::invalid_argument,
                "A render-graph D3D12 binding cannot contain a null "
                "resource"));
        }
        for (std::size_t other = index + 1;
             other < resources.size();
             ++other) {
            if (resources[index].external ==
                resources[other].external) {
                return core::Result<void>::failure(
                    render_graph_error(
                        core::ErrorCode::invalid_state,
                        "A render-graph external resource ID has more "
                        "than one D3D12 binding"));
            }
        }
    }
    return core::Result<void>::success();
}

core::Result<D3D12_RESOURCE_BARRIER> make_legacy_transition_barrier(
    const render_graph::ResourceTransition& transition,
    const std::span<const RenderGraphResourceBinding> resources)
{
    if (transition.before == transition.after) {
        return core::Result<D3D12_RESOURCE_BARRIER>::failure(
            render_graph_error(
                core::ErrorCode::invalid_argument,
                "The render graph attempted to record a no-op transition"));
    }

    auto bindings_result =
        validate_render_graph_resource_bindings(resources);
    if (!bindings_result) {
        return core::Result<D3D12_RESOURCE_BARRIER>::failure(
            std::move(bindings_result).error());
    }
    auto resource_result = resolve_resource(
        transition.external,
        resources);
    if (!resource_result) {
        return core::Result<D3D12_RESOURCE_BARRIER>::failure(
            std::move(resource_result).error());
    }
    auto before_result = legacy_resource_state(transition.before);
    if (!before_result) {
        return core::Result<D3D12_RESOURCE_BARRIER>::failure(
            std::move(before_result).error());
    }
    auto after_result = legacy_resource_state(transition.after);
    if (!after_result) {
        return core::Result<D3D12_RESOURCE_BARRIER>::failure(
            std::move(after_result).error());
    }
    if (before_result.value() == after_result.value()) {
        return core::Result<D3D12_RESOURCE_BARRIER>::failure(
            render_graph_error(
                core::ErrorCode::invalid_argument,
                "Equivalent legacy D3D12 states must be elided before "
                "barrier recording"));
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource_result.value();
    barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before_result.value();
    barrier.Transition.StateAfter = after_result.value();
    return core::Result<D3D12_RESOURCE_BARRIER>::success(barrier);
}

LegacyRenderGraphTransitionRecorder::LegacyRenderGraphTransitionRecorder(
    ID3D12GraphicsCommandList* const command_list,
    const std::span<const RenderGraphResourceBinding> resources) noexcept
    : command_list_(command_list),
      resources_(resources)
{
}

core::Result<void> LegacyRenderGraphTransitionRecorder::transition(
    const render_graph::ResourceTransition& transition)
{
    if (command_list_ == nullptr) {
        return core::Result<void>::failure(render_graph_error(
            core::ErrorCode::invalid_state,
            "The legacy render-graph recorder has no command list"));
    }

    auto barrier_result = make_legacy_transition_barrier(
        transition,
        resources_);
    if (!barrier_result) {
        return core::Result<void>::failure(
            std::move(barrier_result).error());
    }
    const auto barrier = barrier_result.value();
    command_list_->ResourceBarrier(1, &barrier);
    ++recorded_transition_count_;
    return core::Result<void>::success();
}

std::size_t
LegacyRenderGraphTransitionRecorder::recorded_transition_count()
    const noexcept
{
    return recorded_transition_count_;
}

} // namespace shark::rhi::d3d12::detail
