#pragma once

#include <shark/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace shark::assets {

enum class TextureColorSpace : std::uint8_t {
    linear = 1,
    srgb = 2,
};

enum class TextureFormat : std::uint8_t {
    rgba8_unorm = 1,
    rgba8_unorm_srgb = 2,
};

enum class CubemapFace : std::uint8_t {
    positive_x = 0,
    negative_x = 1,
    positive_y = 2,
    negative_y = 3,
    positive_z = 4,
    negative_z = 5,
};

inline constexpr std::size_t cubemap_face_count = 6;

struct CubemapSubresourceView final {
    CubemapFace face = CubemapFace::positive_x;
    std::uint32_t mip_level = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t row_pitch = 0;
    std::size_t slice_pitch = 0;
    std::span<const std::byte> pixels;
};

class DdsCubemap final {
public:
    DdsCubemap(const DdsCubemap&) = delete;
    DdsCubemap(DdsCubemap&&) noexcept = default;
    DdsCubemap& operator=(const DdsCubemap&) = delete;
    DdsCubemap& operator=(DdsCubemap&&) noexcept = default;
    ~DdsCubemap() = default;

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::uint32_t mip_levels() const noexcept;
    [[nodiscard]] TextureFormat format() const noexcept;
    [[nodiscard]] TextureColorSpace color_space() const noexcept;
    [[nodiscard]] std::size_t subresource_count() const noexcept;

    // DDS cubemap subresources are exposed in face-major, then mip-major order:
    // +X mips, -X mips, +Y mips, -Y mips, +Z mips, -Z mips.
    [[nodiscard]] std::optional<CubemapSubresourceView> subresource(
        std::size_t index) const noexcept;
    [[nodiscard]] std::optional<CubemapSubresourceView> subresource(
        CubemapFace face,
        std::uint32_t mip_level) const noexcept;

private:
    struct SubresourceStorage final {
        CubemapFace face = CubemapFace::positive_x;
        std::uint32_t mip_level = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::size_t row_pitch = 0;
        std::size_t slice_pitch = 0;
        std::vector<std::byte> pixels;
    };

    DdsCubemap(
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t mip_levels,
        TextureFormat format,
        TextureColorSpace color_space,
        std::vector<SubresourceStorage> subresources) noexcept;

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t mip_levels_ = 0;
    TextureFormat format_ = TextureFormat::rgba8_unorm;
    TextureColorSpace color_space_ = TextureColorSpace::linear;
    std::vector<SubresourceStorage> subresources_;

    friend core::Result<DdsCubemap> load_dds_cubemap(
        std::span<const std::byte> encoded_dds,
        TextureColorSpace expected_color_space);
};

[[nodiscard]] core::Result<DdsCubemap> load_dds_cubemap(
    std::span<const std::byte> encoded_dds,
    TextureColorSpace expected_color_space);

[[nodiscard]] core::Result<DdsCubemap> load_dds_cubemap_file(
    const std::filesystem::path& path,
    TextureColorSpace expected_color_space);

} // namespace shark::assets
