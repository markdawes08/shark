#pragma once

#include <shark/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shark::render_graph {

enum class ResourceState : std::uint8_t {
    common = 1,
    present,
    render_target,
    depth_write,
    depth_read,
    pixel_shader_read,
    shader_read,
    copy_source,
    copy_destination,
};

enum class AccessMode : std::uint8_t {
    read = 1,
    write,
};

struct ExternalResourceId final {
    std::uint32_t value{};

    [[nodiscard]] friend bool operator==(
        const ExternalResourceId&,
        const ExternalResourceId&) noexcept = default;
};

class ResourceHandle final {
public:
    ResourceHandle() noexcept = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint32_t index() const noexcept;

    [[nodiscard]] friend bool operator==(
        const ResourceHandle&,
        const ResourceHandle&) noexcept = default;

private:
    friend class GraphBuilder;

    ResourceHandle(
        std::uint64_t owner,
        std::uint32_t index) noexcept;

    std::uint64_t owner_{};
    std::uint32_t index_{std::numeric_limits<std::uint32_t>::max()};
};

class PassHandle final {
public:
    PassHandle() noexcept = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint32_t index() const noexcept;

    [[nodiscard]] friend bool operator==(
        const PassHandle&,
        const PassHandle&) noexcept = default;

private:
    friend class GraphBuilder;

    PassHandle(
        std::uint64_t owner,
        std::uint32_t index) noexcept;

    std::uint64_t owner_{};
    std::uint32_t index_{std::numeric_limits<std::uint32_t>::max()};
};

struct ResourceAccess final {
    ResourceHandle resource;
    ExternalResourceId external;
    ResourceState state;
    AccessMode mode;

    [[nodiscard]] friend bool operator==(
        const ResourceAccess&,
        const ResourceAccess&) noexcept = default;
};

struct ResourceTransition final {
    ResourceHandle resource;
    ExternalResourceId external;
    ResourceState before;
    ResourceState after;

    [[nodiscard]] friend bool operator==(
        const ResourceTransition&,
        const ResourceTransition&) noexcept = default;
};

class PassContext final {
public:
    [[nodiscard]] std::string_view pass_name() const noexcept;

    [[nodiscard]] core::Result<ExternalResourceId> read(
        ResourceHandle resource) const;
    [[nodiscard]] core::Result<ExternalResourceId> write(
        ResourceHandle resource) const;

private:
    friend class CompiledGraph;

    PassContext(
        std::string_view pass_name,
        std::span<const ResourceAccess> accesses) noexcept;

    [[nodiscard]] core::Result<ExternalResourceId> access(
        ResourceHandle resource,
        AccessMode mode) const;

    std::string_view pass_name_;
    std::span<const ResourceAccess> accesses_;
};

using PassCallback = std::function<
    core::Result<void>(const PassContext& context)>;

struct CompiledPassInfo final {
    PassHandle handle;
    std::string name;
    std::vector<PassHandle> dependencies;
    std::vector<ResourceAccess> accesses;
    std::vector<ResourceTransition> transitions;
};

struct CompiledGraphStats final {
    std::size_t imported_resource_count{};
    std::size_t pass_count{};
    std::size_t dependency_count{};
    std::size_t transition_count{};
    std::size_t elided_transition_count{};

    [[nodiscard]] friend bool operator==(
        const CompiledGraphStats&,
        const CompiledGraphStats&) noexcept = default;
};

struct ExecutionStats final {
    std::size_t passes_executed{};
    std::size_t transitions_recorded{};

    [[nodiscard]] friend bool operator==(
        const ExecutionStats&,
        const ExecutionStats&) noexcept = default;
};

class TransitionRecorder {
public:
    TransitionRecorder() = default;
    virtual ~TransitionRecorder() = default;

    TransitionRecorder(const TransitionRecorder&) = delete;
    TransitionRecorder& operator=(const TransitionRecorder&) = delete;
    TransitionRecorder(TransitionRecorder&&) = delete;
    TransitionRecorder& operator=(TransitionRecorder&&) = delete;

    [[nodiscard]] virtual core::Result<void> transition(
        const ResourceTransition& transition) = 0;
};

class CompiledGraph final {
public:
    CompiledGraph(const CompiledGraph&) = delete;
    CompiledGraph& operator=(const CompiledGraph&) = delete;
    CompiledGraph(CompiledGraph&&) noexcept = default;
    CompiledGraph& operator=(CompiledGraph&&) noexcept = default;
    ~CompiledGraph() = default;

    [[nodiscard]] std::span<const CompiledPassInfo> passes() const noexcept;
    [[nodiscard]] std::span<const ResourceTransition>
    final_transitions() const noexcept;
    [[nodiscard]] const CompiledGraphStats& stats() const noexcept;

    [[nodiscard]] core::Result<ExecutionStats> execute(
        TransitionRecorder& recorder) const;

private:
    friend class GraphBuilder;

    CompiledGraph(
        std::vector<CompiledPassInfo> passes,
        std::vector<PassCallback> callbacks,
        std::vector<ResourceTransition> final_transitions,
        CompiledGraphStats stats) noexcept;

    std::vector<CompiledPassInfo> passes_;
    std::vector<PassCallback> callbacks_;
    std::vector<ResourceTransition> final_transitions_;
    CompiledGraphStats stats_;
};

class GraphBuilder final {
public:
    GraphBuilder() noexcept;
    GraphBuilder(const GraphBuilder&) = delete;
    GraphBuilder& operator=(const GraphBuilder&) = delete;
    GraphBuilder(GraphBuilder&& other) noexcept;
    GraphBuilder& operator=(GraphBuilder&& other) noexcept;
    ~GraphBuilder() = default;

    [[nodiscard]] core::Result<ResourceHandle> import_resource(
        std::string name,
        ExternalResourceId external,
        ResourceState initial_state,
        ResourceState final_state);

    [[nodiscard]] core::Result<PassHandle> add_pass(
        std::string name,
        PassCallback callback);

    [[nodiscard]] core::Result<void> read(
        PassHandle pass,
        ResourceHandle resource,
        ResourceState state);
    [[nodiscard]] core::Result<void> write(
        PassHandle pass,
        ResourceHandle resource,
        ResourceState state);

    [[nodiscard]] core::Result<void> add_dependency(
        PassHandle before,
        PassHandle after);

    [[nodiscard]] core::Result<CompiledGraph> compile() &&;

private:
    struct ImportedResource final {
        std::string name;
        ExternalResourceId external;
        ResourceState initial_state;
        ResourceState final_state;
    };

    struct Pass final {
        std::string name;
        PassCallback callback;
        std::vector<ResourceAccess> accesses;
    };

    struct Dependency final {
        PassHandle before;
        PassHandle after;
    };

    [[nodiscard]] core::Result<void> add_access(
        PassHandle pass,
        ResourceHandle resource,
        ResourceState state,
        AccessMode mode);

    std::vector<ImportedResource> resources_;
    std::vector<Pass> passes_;
    std::vector<Dependency> explicit_dependencies_;
    std::uint64_t owner_{};
    bool consumed_{};
};

} // namespace shark::render_graph
