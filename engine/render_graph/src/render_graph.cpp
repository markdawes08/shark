#include <shark/render_graph/render_graph.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <atomic>
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <utility>

namespace shark::render_graph {
namespace {

std::atomic<std::uint64_t> next_builder_owner{1};

[[nodiscard]] std::uint64_t acquire_builder_owner() noexcept
{
    auto owner = next_builder_owner.fetch_add(
        1,
        std::memory_order_relaxed);
    while (owner == 0) {
        owner = next_builder_owner.fetch_add(
            1,
            std::memory_order_relaxed);
    }
    return owner;
}

[[nodiscard]] core::Error graph_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::graphics,
        code,
        std::move(message),
    };
}

[[nodiscard]] bool valid_state(const ResourceState state) noexcept
{
    switch (state) {
    case ResourceState::common:
    case ResourceState::present:
    case ResourceState::render_target:
    case ResourceState::depth_write:
    case ResourceState::depth_read:
    case ResourceState::pixel_shader_read:
    case ResourceState::vertex_buffer:
    case ResourceState::index_buffer:
    case ResourceState::shader_read:
    case ResourceState::copy_source:
    case ResourceState::copy_destination:
        return true;
    }

    return false;
}

[[nodiscard]] bool equivalent_state(
    const ResourceState left,
    const ResourceState right) noexcept
{
    if (left == right) {
        return true;
    }
    return (left == ResourceState::common &&
            right == ResourceState::present) ||
        (left == ResourceState::present &&
         right == ResourceState::common);
}

[[nodiscard]] bool valid_pass_state(
    const AccessMode mode,
    const ResourceState state) noexcept
{
    if (mode == AccessMode::read) {
        return state == ResourceState::depth_read ||
            state == ResourceState::pixel_shader_read ||
            state == ResourceState::vertex_buffer ||
            state == ResourceState::index_buffer ||
            state == ResourceState::shader_read ||
            state == ResourceState::copy_source;
    }
    if (mode == AccessMode::write) {
        return state == ResourceState::render_target ||
            state == ResourceState::depth_write ||
            state == ResourceState::copy_destination;
    }
    return false;
}

[[nodiscard]] std::string access_mode_name(const AccessMode mode)
{
    return mode == AccessMode::read ? "read" : "write";
}

} // namespace

ResourceHandle::ResourceHandle(
    const std::uint64_t owner,
    const std::uint32_t index) noexcept
    : owner_(owner),
      index_(index)
{
}

bool ResourceHandle::valid() const noexcept
{
    return owner_ != 0 &&
        index_ != std::numeric_limits<std::uint32_t>::max();
}

std::uint32_t ResourceHandle::index() const noexcept
{
    return index_;
}

PassHandle::PassHandle(
    const std::uint64_t owner,
    const std::uint32_t index) noexcept
    : owner_(owner),
      index_(index)
{
}

bool PassHandle::valid() const noexcept
{
    return owner_ != 0 &&
        index_ != std::numeric_limits<std::uint32_t>::max();
}

std::uint32_t PassHandle::index() const noexcept
{
    return index_;
}

PassContext::PassContext(
    const std::string_view pass_name,
    const std::span<const ResourceAccess> accesses) noexcept
    : pass_name_(pass_name),
      accesses_(accesses)
{
}

std::string_view PassContext::pass_name() const noexcept
{
    return pass_name_;
}

core::Result<ExternalResourceId> PassContext::read(
    const ResourceHandle resource) const
{
    return access(resource, AccessMode::read);
}

core::Result<ExternalResourceId> PassContext::write(
    const ResourceHandle resource) const
{
    return access(resource, AccessMode::write);
}

core::Result<ExternalResourceId> PassContext::access(
    const ResourceHandle resource,
    const AccessMode mode) const
{
    if (!resource.valid()) {
        return core::Result<ExternalResourceId>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "Pass resource access requires a valid resource handle"));
    }

    const auto declared = std::find_if(
        accesses_.begin(),
        accesses_.end(),
        [resource](const ResourceAccess& access) {
            return access.resource == resource;
        });
    if (declared == accesses_.end()) {
        auto message = std::string{"Pass '"};
        message.append(pass_name_);
        message.append("' attempted undeclared resource access");
        return core::Result<ExternalResourceId>::failure(graph_error(
            core::ErrorCode::invalid_state,
            std::move(message)));
    }
    if (declared->mode != mode) {
        auto message = std::string{"Pass '"};
        message.append(pass_name_);
        message.append("' declared the resource for ");
        message.append(access_mode_name(declared->mode));
        message.append(" access but requested ");
        message.append(access_mode_name(mode));
        return core::Result<ExternalResourceId>::failure(graph_error(
            core::ErrorCode::invalid_state,
            std::move(message)));
    }

    return core::Result<ExternalResourceId>::success(
        declared->external);
}

GraphBuilder::GraphBuilder() noexcept
    : owner_(acquire_builder_owner())
{
}

GraphBuilder::GraphBuilder(GraphBuilder&& other) noexcept
    : resources_(std::move(other.resources_)),
      passes_(std::move(other.passes_)),
      explicit_dependencies_(
          std::move(other.explicit_dependencies_)),
      owner_(other.owner_),
      consumed_(other.consumed_)
{
    other.resources_.clear();
    other.passes_.clear();
    other.explicit_dependencies_.clear();
    other.owner_ = acquire_builder_owner();
    other.consumed_ = false;
}

GraphBuilder& GraphBuilder::operator=(GraphBuilder&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    resources_ = std::move(other.resources_);
    passes_ = std::move(other.passes_);
    explicit_dependencies_ =
        std::move(other.explicit_dependencies_);
    owner_ = other.owner_;
    consumed_ = other.consumed_;

    other.resources_.clear();
    other.passes_.clear();
    other.explicit_dependencies_.clear();
    other.owner_ = acquire_builder_owner();
    other.consumed_ = false;
    return *this;
}

CompiledGraph::CompiledGraph(
    std::vector<CompiledPassInfo> passes,
    std::vector<PassCallback> callbacks,
    std::vector<ResourceTransition> final_transitions,
    const CompiledGraphStats stats) noexcept
    : passes_(std::move(passes)),
      callbacks_(std::move(callbacks)),
      final_transitions_(std::move(final_transitions)),
      stats_(stats)
{
}

std::span<const CompiledPassInfo> CompiledGraph::passes() const noexcept
{
    return passes_;
}

std::span<const ResourceTransition>
CompiledGraph::final_transitions() const noexcept
{
    return final_transitions_;
}

const CompiledGraphStats& CompiledGraph::stats() const noexcept
{
    return stats_;
}

core::Result<ExecutionStats> CompiledGraph::execute(
    TransitionRecorder& recorder) const
{
    if (passes_.size() != callbacks_.size()) {
        return core::Result<ExecutionStats>::failure(graph_error(
            core::ErrorCode::invalid_state,
            "Compiled render graph callback storage is inconsistent"));
    }

    ExecutionStats execution{};
    for (std::size_t index = 0; index < passes_.size(); ++index) {
        const auto& pass = passes_[index];
        for (const auto& transition : pass.transitions) {
            auto transition_result = recorder.transition(transition);
            if (!transition_result) {
                return core::Result<ExecutionStats>::failure(
                    std::move(transition_result).error());
            }
            ++execution.transitions_recorded;
        }

        const PassContext context{pass.name, pass.accesses};
        auto callback_result = callbacks_[index](context);
        if (!callback_result) {
            return core::Result<ExecutionStats>::failure(
                std::move(callback_result).error());
        }
        ++execution.passes_executed;
    }

    for (const auto& transition : final_transitions_) {
        auto transition_result = recorder.transition(transition);
        if (!transition_result) {
            return core::Result<ExecutionStats>::failure(
                std::move(transition_result).error());
        }
        ++execution.transitions_recorded;
    }

    return core::Result<ExecutionStats>::success(execution);
}

core::Result<ResourceHandle> GraphBuilder::import_resource(
    std::string name,
    const ExternalResourceId external,
    const ResourceState initial_state,
    const ResourceState final_state)
{
    if (consumed_) {
        return core::Result<ResourceHandle>::failure(graph_error(
            core::ErrorCode::invalid_state,
            "A compiled render-graph builder cannot import resources"));
    }
    if (name.empty()) {
        return core::Result<ResourceHandle>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "Imported render-graph resources require a nonempty name"));
    }
    if (!valid_state(initial_state) || !valid_state(final_state)) {
        return core::Result<ResourceHandle>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "Imported render-graph resource states are invalid"));
    }
    if (resources_.size() >=
        std::numeric_limits<std::uint32_t>::max()) {
        return core::Result<ResourceHandle>::failure(graph_error(
            core::ErrorCode::unavailable,
            "Render graph exhausted its resource-handle range"));
    }
    const auto duplicate_name = std::find_if(
        resources_.begin(),
        resources_.end(),
        [&name](const ImportedResource& resource) {
            return resource.name == name;
        });
    if (duplicate_name != resources_.end()) {
        return core::Result<ResourceHandle>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "Imported render-graph resource names must be unique"));
    }
    const auto duplicate_external = std::find_if(
        resources_.begin(),
        resources_.end(),
        [external](const ImportedResource& resource) {
            return resource.external == external;
        });
    if (duplicate_external != resources_.end()) {
        return core::Result<ResourceHandle>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "Imported render-graph resources cannot alias one external ID"));
    }

    const auto handle = ResourceHandle{
        owner_,
        static_cast<std::uint32_t>(resources_.size())};
    resources_.push_back(ImportedResource{
        std::move(name),
        external,
        initial_state,
        final_state,
    });
    return core::Result<ResourceHandle>::success(handle);
}

core::Result<PassHandle> GraphBuilder::add_pass(
    std::string name,
    PassCallback callback)
{
    if (consumed_) {
        return core::Result<PassHandle>::failure(graph_error(
            core::ErrorCode::invalid_state,
            "A compiled render-graph builder cannot add passes"));
    }
    if (name.empty()) {
        return core::Result<PassHandle>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "Render-graph passes require a nonempty name"));
    }
    if (!callback) {
        return core::Result<PassHandle>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "Render-graph passes require an execution callback"));
    }
    if (passes_.size() >=
        std::numeric_limits<std::uint32_t>::max()) {
        return core::Result<PassHandle>::failure(graph_error(
            core::ErrorCode::unavailable,
            "Render graph exhausted its pass-handle range"));
    }
    const auto duplicate = std::find_if(
        passes_.begin(),
        passes_.end(),
        [&name](const Pass& pass) {
            return pass.name == name;
        });
    if (duplicate != passes_.end()) {
        return core::Result<PassHandle>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "Render-graph pass names must be unique"));
    }

    const auto handle = PassHandle{
        owner_,
        static_cast<std::uint32_t>(passes_.size())};
    passes_.push_back(Pass{
        std::move(name),
        std::move(callback),
        {},
    });
    return core::Result<PassHandle>::success(handle);
}

core::Result<void> GraphBuilder::read(
    const PassHandle pass,
    const ResourceHandle resource,
    const ResourceState state)
{
    return add_access(
        pass,
        resource,
        state,
        AccessMode::read);
}

core::Result<void> GraphBuilder::write(
    const PassHandle pass,
    const ResourceHandle resource,
    const ResourceState state)
{
    return add_access(
        pass,
        resource,
        state,
        AccessMode::write);
}

core::Result<void> GraphBuilder::add_dependency(
    const PassHandle before,
    const PassHandle after)
{
    if (consumed_) {
        return core::Result<void>::failure(graph_error(
            core::ErrorCode::invalid_state,
            "A compiled render-graph builder cannot add dependencies"));
    }
    if (!before.valid() || before.owner_ != owner_ ||
        before.index() >= passes_.size() ||
        !after.valid() || after.owner_ != owner_ ||
        after.index() >= passes_.size()) {
        return core::Result<void>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "Render-graph dependencies require valid pass handles"));
    }
    if (before == after) {
        return core::Result<void>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "A render-graph pass cannot depend on itself"));
    }

    const auto duplicate = std::find_if(
        explicit_dependencies_.begin(),
        explicit_dependencies_.end(),
        [before, after](const Dependency& dependency) {
            return dependency.before == before &&
                dependency.after == after;
        });
    if (duplicate == explicit_dependencies_.end()) {
        explicit_dependencies_.push_back(Dependency{before, after});
    }
    return core::Result<void>::success();
}

core::Result<void> GraphBuilder::add_access(
    const PassHandle pass,
    const ResourceHandle resource,
    const ResourceState state,
    const AccessMode mode)
{
    if (consumed_) {
        return core::Result<void>::failure(graph_error(
            core::ErrorCode::invalid_state,
            "A compiled render-graph builder cannot add resource access"));
    }
    if (!pass.valid() || pass.owner_ != owner_ ||
        pass.index() >= passes_.size()) {
        return core::Result<void>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "Render-graph access requires a valid pass handle"));
    }
    if (!resource.valid() || resource.owner_ != owner_ ||
        resource.index() >= resources_.size()) {
        return core::Result<void>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "Render-graph access requires a valid resource handle"));
    }
    if (!valid_pass_state(mode, state)) {
        return core::Result<void>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "The requested resource state is incompatible with pass access"));
    }

    auto& accesses = passes_[pass.index()].accesses;
    const auto duplicate = std::find_if(
        accesses.begin(),
        accesses.end(),
        [resource](const ResourceAccess& access) {
            return access.resource == resource;
        });
    if (duplicate != accesses.end()) {
        return core::Result<void>::failure(graph_error(
            core::ErrorCode::invalid_argument,
            "A pass may declare each whole resource only once"));
    }

    accesses.push_back(ResourceAccess{
        resource,
        resources_[resource.index()].external,
        state,
        mode,
    });
    return core::Result<void>::success();
}

core::Result<CompiledGraph> GraphBuilder::compile() &&
{
    if (consumed_) {
        return core::Result<CompiledGraph>::failure(graph_error(
            core::ErrorCode::invalid_state,
            "A render-graph builder can be compiled only once"));
    }
    consumed_ = true;

    const auto pass_count = passes_.size();
    std::vector<std::vector<std::uint32_t>> predecessors(pass_count);
    std::vector<std::vector<std::uint32_t>> successors(pass_count);

    const auto add_edge = [&predecessors, &successors](
                              const std::uint32_t before,
                              const std::uint32_t after) {
        auto& incoming = predecessors[after];
        if (std::find(incoming.begin(), incoming.end(), before) !=
            incoming.end()) {
            return;
        }
        incoming.push_back(before);
        successors[before].push_back(after);
    };

    for (const auto& dependency : explicit_dependencies_) {
        add_edge(
            dependency.before.index(),
            dependency.after.index());
    }

    for (std::uint32_t resource_index = 0;
         resource_index < resources_.size();
         ++resource_index) {
        std::optional<std::uint32_t> last_writer;
        std::vector<std::uint32_t> readers_since_write;

        for (std::uint32_t pass_index = 0;
             pass_index < pass_count;
             ++pass_index) {
            const auto& accesses = passes_[pass_index].accesses;
            const auto access = std::find_if(
                accesses.begin(),
                accesses.end(),
                [resource_index](const ResourceAccess& candidate) {
                    return candidate.resource.index() == resource_index;
                });
            if (access == accesses.end()) {
                continue;
            }

            if (access->mode == AccessMode::read) {
                if (last_writer.has_value()) {
                    add_edge(*last_writer, pass_index);
                }
                readers_since_write.push_back(pass_index);
                continue;
            }

            if (last_writer.has_value()) {
                add_edge(*last_writer, pass_index);
            }
            for (const auto reader : readers_since_write) {
                add_edge(reader, pass_index);
            }
            readers_since_write.clear();
            last_writer = pass_index;
        }
    }

    for (auto& incoming : predecessors) {
        std::sort(incoming.begin(), incoming.end());
    }
    for (auto& outgoing : successors) {
        std::sort(outgoing.begin(), outgoing.end());
    }

    std::vector<std::size_t> indegrees;
    indegrees.reserve(pass_count);
    for (const auto& incoming : predecessors) {
        indegrees.push_back(incoming.size());
    }

    std::priority_queue<
        std::uint32_t,
        std::vector<std::uint32_t>,
        std::greater<>> ready;
    for (std::uint32_t pass_index = 0;
         pass_index < pass_count;
         ++pass_index) {
        if (indegrees[pass_index] == 0) {
            ready.push(pass_index);
        }
    }

    std::vector<std::uint32_t> order;
    order.reserve(pass_count);
    while (!ready.empty()) {
        const auto pass_index = ready.top();
        ready.pop();
        order.push_back(pass_index);

        for (const auto successor : successors[pass_index]) {
            --indegrees[successor];
            if (indegrees[successor] == 0) {
                ready.push(successor);
            }
        }
    }

    if (order.size() != pass_count) {
        std::string message{
            "Render graph contains a dependency cycle involving"};
        for (std::uint32_t pass_index = 0;
             pass_index < pass_count;
             ++pass_index) {
            if (indegrees[pass_index] != 0) {
                message.append(" '");
                message.append(passes_[pass_index].name);
                message.push_back('\'');
            }
        }
        return core::Result<CompiledGraph>::failure(graph_error(
            core::ErrorCode::invalid_state,
            std::move(message)));
    }

    CompiledGraphStats statistics{
        .imported_resource_count = resources_.size(),
        .pass_count = pass_count,
    };
    std::vector<ResourceState> current_states;
    current_states.reserve(resources_.size());
    for (const auto& resource : resources_) {
        current_states.push_back(resource.initial_state);
    }

    std::vector<CompiledPassInfo> compiled_passes;
    std::vector<PassCallback> callbacks;
    compiled_passes.reserve(pass_count);
    callbacks.reserve(pass_count);
    for (const auto pass_index : order) {
        auto& source = passes_[pass_index];
        CompiledPassInfo compiled{
            .handle = PassHandle{owner_, pass_index},
            .name = std::move(source.name),
            .accesses = std::move(source.accesses),
        };
        compiled.dependencies.reserve(
            predecessors[pass_index].size());
        for (const auto dependency : predecessors[pass_index]) {
            compiled.dependencies.push_back(PassHandle{
                owner_,
                dependency,
            });
        }
        statistics.dependency_count +=
            compiled.dependencies.size();

        for (const auto& access : compiled.accesses) {
            auto& current = current_states[access.resource.index()];
            if (equivalent_state(current, access.state)) {
                ++statistics.elided_transition_count;
                continue;
            }
            compiled.transitions.push_back(ResourceTransition{
                access.resource,
                access.external,
                current,
                access.state,
            });
            current = access.state;
            ++statistics.transition_count;
        }

        compiled_passes.push_back(std::move(compiled));
        callbacks.push_back(std::move(source.callback));
    }

    std::vector<ResourceTransition> final_transitions;
    for (std::uint32_t resource_index = 0;
         resource_index < resources_.size();
         ++resource_index) {
        const auto& resource = resources_[resource_index];
        const auto current = current_states[resource_index];
        if (equivalent_state(current, resource.final_state)) {
            ++statistics.elided_transition_count;
            continue;
        }
        final_transitions.push_back(ResourceTransition{
            ResourceHandle{owner_, resource_index},
            resource.external,
            current,
            resource.final_state,
        });
        ++statistics.transition_count;
    }

    return core::Result<CompiledGraph>::success(CompiledGraph{
        std::move(compiled_passes),
        std::move(callbacks),
        std::move(final_transitions),
        statistics,
    });
}

} // namespace shark::render_graph
