#pragma once

#include <shark/core/result.hpp>
#include <shark/render_graph/render_graph.hpp>

#include <directx/d3d12.h>

#include <cstddef>
#include <span>

namespace shark::rhi::d3d12::detail {

struct RenderGraphResourceBinding final {
    render_graph::ExternalResourceId external;
    ID3D12Resource* resource{};
};

[[nodiscard]] core::Result<D3D12_RESOURCE_STATES>
legacy_resource_state(render_graph::ResourceState state);

[[nodiscard]] core::Result<void> validate_render_graph_resource_bindings(
    std::span<const RenderGraphResourceBinding> resources);

[[nodiscard]] core::Result<D3D12_RESOURCE_BARRIER>
make_legacy_transition_barrier(
    const render_graph::ResourceTransition& transition,
    std::span<const RenderGraphResourceBinding> resources);

class LegacyRenderGraphTransitionRecorder final
    : public render_graph::TransitionRecorder {
public:
    LegacyRenderGraphTransitionRecorder(
        ID3D12GraphicsCommandList* command_list,
        std::span<const RenderGraphResourceBinding> resources) noexcept;

    [[nodiscard]] core::Result<void> transition(
        const render_graph::ResourceTransition& transition) override;

    [[nodiscard]] std::size_t recorded_transition_count() const noexcept;

private:
    ID3D12GraphicsCommandList* command_list_{};
    std::span<const RenderGraphResourceBinding> resources_;
    std::size_t recorded_transition_count_{};
};

} // namespace shark::rhi::d3d12::detail
