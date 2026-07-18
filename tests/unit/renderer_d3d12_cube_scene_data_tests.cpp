#include "cube_scene_data.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace {

using shark::renderer::d3d12::detail::CubeVertex;

[[nodiscard]] std::array<float, 3> subtract(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) noexcept
{
    return {
        left[0] - right[0],
        left[1] - right[1],
        left[2] - right[2],
    };
}

[[nodiscard]] std::array<float, 3> cross(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) noexcept
{
    return {
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    };
}

[[nodiscard]] float length_squared(
    const std::array<float, 3>& value) noexcept
{
    return value[0] * value[0] +
        value[1] * value[1] +
        value[2] * value[2];
}

[[nodiscard]] std::array<std::uint8_t, 4> checker_pixel(
    const std::uint32_t x,
    const std::uint32_t y) noexcept
{
    using namespace shark::renderer::d3d12::detail;

    const auto pixel_index =
        static_cast<std::size_t>(y) * checker_width + x;
    const auto byte_offset =
        pixel_index * checker_bytes_per_pixel;
    return {
        checker_pixels[byte_offset],
        checker_pixels[byte_offset + 1U],
        checker_pixels[byte_offset + 2U],
        checker_pixels[byte_offset + 3U],
    };
}

} // namespace

TEST_CASE(
    "cube scene geometry has exact bounded position and UV data",
    "[renderer][d3d12][gpu][cube][contract]")
{
    using namespace shark::renderer::d3d12::detail;

    STATIC_REQUIRE(cube_vertices.size() == 24);
    STATIC_REQUIRE(cube_indices.size() == 36);
    STATIC_REQUIRE(sizeof(CubeVertex) == sizeof(float) * 5U);
    STATIC_REQUIRE(offsetof(CubeVertex, position) == 0U);
    STATIC_REQUIRE(offsetof(CubeVertex, uv) == sizeof(float) * 3U);

    std::array<float, 3> minimum{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    std::array<float, 3> maximum{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };

    for (const auto& vertex : cube_vertices) {
        for (std::size_t axis = 0; axis < vertex.position.size(); ++axis) {
            REQUIRE(std::isfinite(vertex.position[axis]));
            minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
            maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
        }
        for (const auto coordinate : vertex.uv) {
            REQUIRE(std::isfinite(coordinate));
            REQUIRE(coordinate >= 0.0F);
            REQUIRE(coordinate <= 1.0F);
        }
    }

    constexpr std::array<float, 3> expected_minimum{
        -1.0F,
        -1.0F,
        -1.0F,
    };
    constexpr std::array<float, 3> expected_maximum{
        1.0F,
        1.0F,
        1.0F,
    };
    REQUIRE(minimum == expected_minimum);
    REQUIRE(maximum == expected_maximum);
}

TEST_CASE(
    "cube indices describe twelve finite nondegenerate triangles",
    "[renderer][d3d12][gpu][cube][contract]")
{
    using namespace shark::renderer::d3d12::detail;

    std::array<bool, cube_vertices.size()> referenced{};
    for (std::size_t triangle = 0;
         triangle < cube_indices.size() / 3U;
         ++triangle) {
        const auto first_index = cube_indices[triangle * 3U];
        const auto second_index = cube_indices[triangle * 3U + 1U];
        const auto third_index = cube_indices[triangle * 3U + 2U];

        REQUIRE(first_index < cube_vertices.size());
        REQUIRE(second_index < cube_vertices.size());
        REQUIRE(third_index < cube_vertices.size());
        REQUIRE(first_index != second_index);
        REQUIRE(second_index != third_index);
        REQUIRE(first_index != third_index);

        referenced[first_index] = true;
        referenced[second_index] = true;
        referenced[third_index] = true;

        const auto first_edge = subtract(
            cube_vertices[second_index].position,
            cube_vertices[first_index].position);
        const auto second_edge = subtract(
            cube_vertices[third_index].position,
            cube_vertices[first_index].position);
        const auto area_vector = cross(first_edge, second_edge);
        REQUIRE(std::isfinite(length_squared(area_vector)));
        REQUIRE(length_squared(area_vector) > 0.0F);
    }

    REQUIRE(std::all_of(
        referenced.begin(),
        referenced.end(),
        [](const bool value) { return value; }));
}

TEST_CASE(
    "checker pixels are deterministic RGBA8 alternating texels",
    "[renderer][d3d12][gpu][cube][texture][contract]")
{
    using namespace shark::renderer::d3d12::detail;

    STATIC_REQUIRE(checker_width == 8);
    STATIC_REQUIRE(checker_height == 8);
    STATIC_REQUIRE(checker_bytes_per_pixel == 4);
    STATIC_REQUIRE(checker_byte_count == 256);
    STATIC_REQUIRE(checker_pixels.size() == checker_byte_count);
    STATIC_REQUIRE(make_checker_pixels() == checker_pixels);

    for (std::uint32_t y = 0; y < checker_height; ++y) {
        for (std::uint32_t x = 0; x < checker_width; ++x) {
            const auto pixel = checker_pixel(x, y);
            const auto expected = ((x + y) % 2U == 0U)
                ? checker_light_rgba
                : checker_dark_rgba;
            REQUIRE(pixel == expected);
            REQUIRE(pixel[3] == 255);

            if (x + 1U < checker_width) {
                REQUIRE(pixel != checker_pixel(x + 1U, y));
            }
            if (y + 1U < checker_height) {
                REQUIRE(pixel != checker_pixel(x, y + 1U));
            }
        }
    }
}

TEST_CASE(
    "cube vertex and reversed-Z depth contracts match D3D12",
    "[renderer][d3d12][gpu][cube][depth][contract]")
{
    using namespace shark::renderer::d3d12::detail;

    STATIC_REQUIRE(cube_vertex_stride == 20);
    STATIC_REQUIRE(cube_input_elements.size() == 2);
    STATIC_REQUIRE(cube_input_layout.NumElements == 2);
    STATIC_REQUIRE(
        cube_input_layout.pInputElementDescs ==
        cube_input_elements.data());

    const auto& position = cube_input_elements[0];
    REQUIRE(std::string_view{position.SemanticName} == "POSITION");
    REQUIRE(position.SemanticIndex == 0);
    REQUIRE(position.Format == DXGI_FORMAT_R32G32B32_FLOAT);
    REQUIRE(position.InputSlot == 0);
    REQUIRE(position.AlignedByteOffset == 0);
    REQUIRE(
        position.InputSlotClass ==
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA);
    REQUIRE(position.InstanceDataStepRate == 0);

    const auto& uv = cube_input_elements[1];
    REQUIRE(std::string_view{uv.SemanticName} == "TEXCOORD");
    REQUIRE(uv.SemanticIndex == 0);
    REQUIRE(uv.Format == DXGI_FORMAT_R32G32_FLOAT);
    REQUIRE(uv.InputSlot == 0);
    REQUIRE(uv.AlignedByteOffset == sizeof(float) * 3U);
    REQUIRE(
        uv.InputSlotClass ==
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA);
    REQUIRE(uv.InstanceDataStepRate == 0);

    STATIC_REQUIRE(checker_format == DXGI_FORMAT_R8G8B8A8_UNORM);
    STATIC_REQUIRE(checker_mip_levels == 1);
    STATIC_REQUIRE(cube_depth_format == DXGI_FORMAT_D32_FLOAT);
    STATIC_REQUIRE(cube_depth_clear_value == 0.0F);
    STATIC_REQUIRE(
        cube_depth_comparison ==
        D3D12_COMPARISON_FUNC_GREATER_EQUAL);
    STATIC_REQUIRE(
        cube_depth_write_mask ==
        D3D12_DEPTH_WRITE_MASK_ALL);
    STATIC_REQUIRE(
        cube_depth_resource_flags ==
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    STATIC_REQUIRE(
        cube_depth_resource_state ==
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
}
