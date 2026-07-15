#include "options.hpp"

#include <shark/core/error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <span>
#include <string_view>

namespace {

[[nodiscard]] shark::core::Result<shark::sandbox::Options> parse(
    const std::initializer_list<std::string_view> arguments)
{
    return shark::sandbox::parse_options(std::span{
        arguments.begin(),
        arguments.size(),
    });
}

} // namespace

TEST_CASE("sandbox options preserve the interactive hardware default", "[cli]")
{
    const auto result = parse({});
    REQUIRE(result);
    REQUIRE(result.value().run_mode ==
        shark::sandbox::RunMode::interactive);
    REQUIRE(result.value().adapter.kind ==
        shark::rhi::d3d12::AdapterSelectionKind::high_performance);
    REQUIRE_FALSE(result.value().gpu_based_validation);
}

TEST_CASE("sandbox options parse GPU smoke modifiers in any order", "[cli]")
{
    const auto indexed = parse({
        "--gpu-validation",
        "--adapter",
        "17",
        "--gpu-smoke",
    });
    REQUIRE(indexed);
    REQUIRE(indexed.value().run_mode ==
        shark::sandbox::RunMode::gpu_smoke);
    REQUIRE(indexed.value().adapter.kind ==
        shark::rhi::d3d12::AdapterSelectionKind::preference_index);
    REQUIRE(indexed.value().adapter.preference_index == 17);
    REQUIRE(indexed.value().gpu_based_validation);

    const auto warp = parse({"--warp", "--capabilities"});
    REQUIRE(warp);
    REQUIRE(warp.value().run_mode ==
        shark::sandbox::RunMode::capabilities);
    REQUIRE(warp.value().adapter.kind ==
        shark::rhi::d3d12::AdapterSelectionKind::warp);
}

TEST_CASE("sandbox options parse the fixed presentation smoke mode", "[cli]")
{
    const auto hardware = parse({"--present-smoke"});
    REQUIRE(hardware);
    REQUIRE(hardware.value().run_mode ==
        shark::sandbox::RunMode::present_smoke);
    REQUIRE(hardware.value().adapter.kind ==
        shark::rhi::d3d12::AdapterSelectionKind::high_performance);
    REQUIRE_FALSE(hardware.value().gpu_based_validation);

    const auto warp = parse({
        "--gpu-validation",
        "--warp",
        "--present-smoke",
    });
    REQUIRE(warp);
    REQUIRE(warp.value().run_mode ==
        shark::sandbox::RunMode::present_smoke);
    REQUIRE(warp.value().adapter.kind ==
        shark::rhi::d3d12::AdapterSelectionKind::warp);
    REQUIRE(warp.value().gpu_based_validation);

    const auto indexed = parse({
        "--adapter",
        "3",
        "--present-smoke",
    });
    REQUIRE(indexed);
    REQUIRE(indexed.value().adapter.kind ==
        shark::rhi::d3d12::AdapterSelectionKind::preference_index);
    REQUIRE(indexed.value().adapter.preference_index == 3);
}

TEST_CASE("sandbox options reject conflicting selections and modes", "[cli]")
{
    REQUIRE_FALSE(parse({"--warp", "--adapter", "0"}));
    REQUIRE_FALSE(parse({"--adapter", "0", "--adapter", "1"}));
    REQUIRE_FALSE(parse({"--warp", "--warp"}));
    REQUIRE_FALSE(parse({"--gpu-smoke", "--capabilities"}));
    REQUIRE_FALSE(parse({"--present-smoke", "--present-smoke"}));
    REQUIRE_FALSE(parse({"--present-smoke", "--platform-smoke"}));
    REQUIRE_FALSE(parse({"--present-smoke", "--gpu-smoke"}));
    REQUIRE_FALSE(parse({"--present-smoke", "--capabilities"}));
    REQUIRE_FALSE(parse({"--gpu-validation", "--gpu-validation"}));
    REQUIRE_FALSE(parse({"--platform-smoke", "--warp"}));
    REQUIRE_FALSE(parse({"--platform-smoke", "--gpu-validation"}));
}

TEST_CASE("sandbox adapter indices are strict unsigned decimal values", "[cli]")
{
    REQUIRE_FALSE(parse({"--adapter"}));
    REQUIRE_FALSE(parse({"--adapter", ""}));
    REQUIRE_FALSE(parse({"--adapter", "-1"}));
    REQUIRE_FALSE(parse({"--adapter", "+1"}));
    REQUIRE_FALSE(parse({"--adapter", "1x"}));
    REQUIRE_FALSE(parse({"--adapter", "4294967296"}));

    const auto maximum = parse({"--adapter", "4294967295"});
    REQUIRE(maximum);
    REQUIRE(maximum.value().adapter.preference_index == 4'294'967'295U);
}

TEST_CASE("sandbox options return structured errors for unknown input", "[cli]")
{
    const auto result = parse({"--not-a-shark-option"});
    REQUIRE_FALSE(result);
    REQUIRE(result.error().category() == shark::core::ErrorCategory::core);
    REQUIRE(result.error().code() ==
        shark::core::ErrorCode::invalid_argument);
}
