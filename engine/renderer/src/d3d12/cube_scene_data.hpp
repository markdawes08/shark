#pragma once

#include <directx/d3d12.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace shark::renderer::d3d12::detail {

struct CubeVertex final {
    std::array<float, 3> position{};
    std::array<float, 2> uv{};
};

static_assert(std::is_standard_layout_v<CubeVertex>);
static_assert(sizeof(CubeVertex) == sizeof(float) * 5U);
static_assert(offsetof(CubeVertex, position) == 0U);
static_assert(offsetof(CubeVertex, uv) == sizeof(float) * 3U);

inline constexpr std::array<CubeVertex, 24> cube_vertices{{
    // +Z
    {{{-1.0F, -1.0F, 1.0F}}, {{0.0F, 1.0F}}},
    {{{1.0F, -1.0F, 1.0F}}, {{1.0F, 1.0F}}},
    {{{1.0F, 1.0F, 1.0F}}, {{1.0F, 0.0F}}},
    {{{-1.0F, 1.0F, 1.0F}}, {{0.0F, 0.0F}}},

    // -Z
    {{{1.0F, -1.0F, -1.0F}}, {{0.0F, 1.0F}}},
    {{{-1.0F, -1.0F, -1.0F}}, {{1.0F, 1.0F}}},
    {{{-1.0F, 1.0F, -1.0F}}, {{1.0F, 0.0F}}},
    {{{1.0F, 1.0F, -1.0F}}, {{0.0F, 0.0F}}},

    // +X
    {{{1.0F, -1.0F, 1.0F}}, {{0.0F, 1.0F}}},
    {{{1.0F, -1.0F, -1.0F}}, {{1.0F, 1.0F}}},
    {{{1.0F, 1.0F, -1.0F}}, {{1.0F, 0.0F}}},
    {{{1.0F, 1.0F, 1.0F}}, {{0.0F, 0.0F}}},

    // -X
    {{{-1.0F, -1.0F, -1.0F}}, {{0.0F, 1.0F}}},
    {{{-1.0F, -1.0F, 1.0F}}, {{1.0F, 1.0F}}},
    {{{-1.0F, 1.0F, 1.0F}}, {{1.0F, 0.0F}}},
    {{{-1.0F, 1.0F, -1.0F}}, {{0.0F, 0.0F}}},

    // +Y
    {{{-1.0F, 1.0F, 1.0F}}, {{0.0F, 1.0F}}},
    {{{1.0F, 1.0F, 1.0F}}, {{1.0F, 1.0F}}},
    {{{1.0F, 1.0F, -1.0F}}, {{1.0F, 0.0F}}},
    {{{-1.0F, 1.0F, -1.0F}}, {{0.0F, 0.0F}}},

    // -Y
    {{{-1.0F, -1.0F, -1.0F}}, {{0.0F, 1.0F}}},
    {{{1.0F, -1.0F, -1.0F}}, {{1.0F, 1.0F}}},
    {{{1.0F, -1.0F, 1.0F}}, {{1.0F, 0.0F}}},
    {{{-1.0F, -1.0F, 1.0F}}, {{0.0F, 0.0F}}},
}};

inline constexpr std::array<std::uint16_t, 36> cube_indices{{
    0, 1, 2, 0, 2, 3,
    4, 5, 6, 4, 6, 7,
    8, 9, 10, 8, 10, 11,
    12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19,
    20, 21, 22, 20, 22, 23,
}};

inline constexpr std::uint32_t checker_width = 8;
inline constexpr std::uint32_t checker_height = 8;
inline constexpr std::size_t checker_bytes_per_pixel = 4;
inline constexpr std::size_t checker_pixel_count =
    static_cast<std::size_t>(checker_width) *
    static_cast<std::size_t>(checker_height);
inline constexpr std::size_t checker_byte_count =
    checker_pixel_count * checker_bytes_per_pixel;
inline constexpr DXGI_FORMAT checker_format =
    DXGI_FORMAT_R8G8B8A8_UNORM;
inline constexpr std::uint16_t checker_mip_levels = 1;

inline constexpr std::array<std::uint8_t, checker_bytes_per_pixel>
    checker_dark_rgba{{32, 32, 32, 255}};
inline constexpr std::array<std::uint8_t, checker_bytes_per_pixel>
    checker_light_rgba{{224, 224, 224, 255}};

using CheckerPixels = std::array<std::uint8_t, checker_byte_count>;

[[nodiscard]] constexpr CheckerPixels make_checker_pixels() noexcept
{
    CheckerPixels pixels{};
    for (std::uint32_t y = 0; y < checker_height; ++y) {
        for (std::uint32_t x = 0; x < checker_width; ++x) {
            const auto& color = ((x + y) % 2U == 0U)
                ? checker_light_rgba
                : checker_dark_rgba;
            const auto pixel_index =
                static_cast<std::size_t>(y) * checker_width + x;
            const auto byte_offset =
                pixel_index * checker_bytes_per_pixel;
            for (std::size_t channel = 0;
                 channel < checker_bytes_per_pixel;
                 ++channel) {
                pixels[byte_offset + channel] = color[channel];
            }
        }
    }
    return pixels;
}

inline constexpr CheckerPixels checker_pixels = make_checker_pixels();

inline constexpr std::uint32_t cube_vertex_stride =
    static_cast<std::uint32_t>(sizeof(CubeVertex));
inline constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 2>
    cube_input_elements{{
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            static_cast<UINT>(offsetof(CubeVertex, position)),
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "TEXCOORD",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            static_cast<UINT>(offsetof(CubeVertex, uv)),
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
    }};
inline constexpr D3D12_INPUT_LAYOUT_DESC cube_input_layout{
    cube_input_elements.data(),
    static_cast<UINT>(cube_input_elements.size()),
};

inline constexpr DXGI_FORMAT cube_depth_format =
    DXGI_FORMAT_D32_FLOAT;
inline constexpr float cube_depth_clear_value = 0.0F;
inline constexpr D3D12_COMPARISON_FUNC cube_depth_comparison =
    D3D12_COMPARISON_FUNC_GREATER_EQUAL;
inline constexpr D3D12_DEPTH_WRITE_MASK cube_depth_write_mask =
    D3D12_DEPTH_WRITE_MASK_ALL;
inline constexpr D3D12_RESOURCE_FLAGS cube_depth_resource_flags =
    D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
inline constexpr D3D12_RESOURCE_STATES cube_depth_resource_state =
    D3D12_RESOURCE_STATE_DEPTH_WRITE;

} // namespace shark::renderer::d3d12::detail
