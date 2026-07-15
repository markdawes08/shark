#include <shark/rhi/d3d12/device.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

[[nodiscard]] shark::rhi::d3d12::AdapterInfo adapter_with(
    const bool feature_level_12_0,
    const shark::rhi::d3d12::ShaderModel shader_model)
{
    shark::rhi::d3d12::AdapterInfo adapter;
    adapter.supports_feature_level_12_0 = feature_level_12_0;
    adapter.highest_shader_model = shader_model;
    return adapter;
}

} // namespace

TEST_CASE("the D3D12 required baseline has exact boundaries", "[gpu]")
{
    using shark::rhi::d3d12::ShaderModel;
    using shark::rhi::d3d12::supports_required_baseline;

    REQUIRE(supports_required_baseline(
        adapter_with(true, ShaderModel{6, 0})));
    REQUIRE(supports_required_baseline(
        adapter_with(true, ShaderModel{6, 9})));
    REQUIRE_FALSE(supports_required_baseline(
        adapter_with(false, ShaderModel{6, 9})));
    REQUIRE_FALSE(supports_required_baseline(
        adapter_with(true, ShaderModel{5, 1})));

    shark::rhi::d3d12::AdapterInfo missing_shader_model;
    missing_shader_model.supports_feature_level_12_0 = true;
    REQUIRE_FALSE(supports_required_baseline(missing_shader_model));
}

TEST_CASE("D3D12 adapter selectors are explicit tagged values", "[gpu]")
{
    using shark::rhi::d3d12::AdapterSelection;
    using shark::rhi::d3d12::AdapterSelectionKind;

    const auto automatic = AdapterSelection::high_performance();
    REQUIRE(automatic.kind == AdapterSelectionKind::high_performance);

    const auto indexed = AdapterSelection::by_preference_index(7);
    REQUIRE(indexed.kind == AdapterSelectionKind::preference_index);
    REQUIRE(indexed.preference_index == 7);

    const auto warp = AdapterSelection::warp();
    REQUIRE(warp.kind == AdapterSelectionKind::warp);
}
