#pragma once

#include <shark/assets/dds_cubemap.hpp>
#include <shark/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace shark::assets {

inline constexpr std::uint32_t environment_source_width = 64;
inline constexpr std::uint32_t environment_source_height = 32;
inline constexpr std::uint32_t environment_radiance_dimension = 32;
inline constexpr std::uint32_t environment_radiance_mip_levels = 6;
inline constexpr std::uint32_t environment_irradiance_dimension = 8;
inline constexpr std::uint32_t environment_irradiance_mip_levels = 1;
inline constexpr std::uint32_t environment_specular_dimension = 32;
inline constexpr std::uint32_t environment_specular_mip_levels = 6;
inline constexpr std::uint32_t environment_brdf_lut_dimension = 32;
inline constexpr std::size_t environment_float_channels = 4;
inline constexpr std::size_t environment_texel_bytes =
    environment_float_channels * sizeof(float);

[[nodiscard]] constexpr std::uint32_t environment_mip_dimension(
    const std::uint32_t base_dimension,
    const std::uint32_t mip_level) noexcept
{
    return base_dimension >> mip_level;
}

[[nodiscard]] constexpr std::size_t environment_cube_texel_count(
    const std::uint32_t base_dimension,
    const std::uint32_t mip_levels) noexcept
{
    std::size_t result = 0;
    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        const auto dimension =
            environment_mip_dimension(base_dimension, mip);
        result += static_cast<std::size_t>(dimension) * dimension *
            cubemap_face_count;
    }
    return result;
}

inline constexpr std::size_t environment_source_texel_count =
    static_cast<std::size_t>(environment_source_width) *
    environment_source_height;
inline constexpr std::size_t environment_source_bytes =
    environment_source_texel_count * environment_texel_bytes;
inline constexpr std::size_t environment_radiance_texel_count =
    environment_cube_texel_count(
        environment_radiance_dimension,
        environment_radiance_mip_levels);
inline constexpr std::size_t environment_irradiance_texel_count =
    environment_cube_texel_count(
        environment_irradiance_dimension,
        environment_irradiance_mip_levels);
inline constexpr std::size_t environment_specular_texel_count =
    environment_cube_texel_count(
        environment_specular_dimension,
        environment_specular_mip_levels);
inline constexpr std::size_t environment_brdf_lut_texel_count =
    static_cast<std::size_t>(environment_brdf_lut_dimension) *
    environment_brdf_lut_dimension;
inline constexpr std::size_t environment_derived_texel_count =
    environment_radiance_texel_count +
    environment_irradiance_texel_count +
    environment_specular_texel_count +
    environment_brdf_lut_texel_count;
inline constexpr std::size_t environment_derived_bytes =
    environment_derived_texel_count * environment_texel_bytes;
inline constexpr std::size_t environment_radiance_subresource_count =
    cubemap_face_count * environment_radiance_mip_levels;
inline constexpr std::size_t environment_irradiance_subresource_count =
    cubemap_face_count * environment_irradiance_mip_levels;
inline constexpr std::size_t environment_specular_subresource_count =
    cubemap_face_count * environment_specular_mip_levels;
inline constexpr std::size_t environment_brdf_lut_subresource_count = 1;
inline constexpr std::size_t environment_derived_subresource_count =
    environment_radiance_subresource_count +
    environment_irradiance_subresource_count +
    environment_specular_subresource_count +
    environment_brdf_lut_subresource_count;

static_assert(environment_source_texel_count == 2'048);
static_assert(environment_source_bytes == 32'768);
static_assert(environment_radiance_texel_count == 8'190);
static_assert(environment_irradiance_texel_count == 384);
static_assert(environment_specular_texel_count == 8'190);
static_assert(environment_brdf_lut_texel_count == 1'024);
static_assert(environment_derived_texel_count == 17'788);
static_assert(environment_derived_bytes == 284'608);
static_assert(environment_derived_subresource_count == 79);

struct EnvironmentCubeSubresourceView final {
    CubemapFace face{CubemapFace::positive_x};
    std::uint32_t mip_level{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_pitch{};
    std::size_t slice_pitch{};
    std::span<const std::byte> pixels;
};

struct EnvironmentTexture2DView final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mip_level{};
    std::size_t row_pitch{};
    std::size_t slice_pitch{};
    std::span<const std::byte> pixels;
};

struct EnvironmentLightingMetadata final {
    std::size_t source_texel_count{};
    std::size_t source_byte_count{};
    std::size_t derived_texel_count{};
    std::size_t derived_subresource_count{};
    std::size_t derived_byte_count{};
    float source_peak_radiance{};
    float radiance_peak{};
    float diffuse_irradiance_peak{};
    float specular_peak{};
    float brdf_lut_peak{};
    float derived_peak_value{};

    [[nodiscard]] friend bool operator==(
        const EnvironmentLightingMetadata&,
        const EnvironmentLightingMetadata&) = default;
};

// Owns deterministic, project-generated linear RGBA32_FLOAT image data.
// Cubemap subresources are face-major, then mip-minor. The BRDF LUT stores
// its split-sum scale and bias in R/G; B is zero and A is one.
class EnvironmentLighting final {
public:
    EnvironmentLighting(const EnvironmentLighting&) = delete;
    EnvironmentLighting(EnvironmentLighting&&) noexcept = default;
    EnvironmentLighting& operator=(const EnvironmentLighting&) = delete;
    EnvironmentLighting& operator=(EnvironmentLighting&&) noexcept = default;
    ~EnvironmentLighting() = default;

    [[nodiscard]] const EnvironmentLightingMetadata& metadata()
        const noexcept;

    [[nodiscard]] std::size_t radiance_subresource_count() const noexcept;
    [[nodiscard]] std::optional<EnvironmentCubeSubresourceView>
    radiance_subresource(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<EnvironmentCubeSubresourceView>
    radiance_subresource(
        CubemapFace face,
        std::uint32_t mip_level) const noexcept;

    [[nodiscard]] std::size_t irradiance_subresource_count() const noexcept;
    [[nodiscard]] std::optional<EnvironmentCubeSubresourceView>
    irradiance_subresource(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<EnvironmentCubeSubresourceView>
    irradiance_subresource(
        CubemapFace face,
        std::uint32_t mip_level) const noexcept;

    [[nodiscard]] std::size_t specular_subresource_count() const noexcept;
    [[nodiscard]] std::optional<EnvironmentCubeSubresourceView>
    specular_subresource(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<EnvironmentCubeSubresourceView>
    specular_subresource(
        CubemapFace face,
        std::uint32_t mip_level) const noexcept;

    [[nodiscard]] EnvironmentTexture2DView brdf_lut() const noexcept;

private:
    struct CubeSubresourceStorage final {
        CubemapFace face{CubemapFace::positive_x};
        std::uint32_t mip_level{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<float> texels;
    };

    struct Texture2DStorage final {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<float> texels;
    };

    EnvironmentLighting(
        EnvironmentLightingMetadata metadata,
        std::vector<CubeSubresourceStorage> radiance,
        std::vector<CubeSubresourceStorage> irradiance,
        std::vector<CubeSubresourceStorage> specular,
        Texture2DStorage brdf_lut) noexcept;

    EnvironmentLightingMetadata metadata_;
    std::vector<CubeSubresourceStorage> radiance_;
    std::vector<CubeSubresourceStorage> irradiance_;
    std::vector<CubeSubresourceStorage> specular_;
    Texture2DStorage brdf_lut_;

    friend core::Result<EnvironmentLighting>
    generate_deterministic_environment_lighting();
};

// Generates a fixed 64x32 HDR latitude-longitude daylight source, converts it
// to a radiance cubemap, and derives irradiance, GGX-prefiltered specular, and
// split-sum BRDF lookup data. The directional sun remains analytic so this
// bounded source neither quantizes nor double-counts it. Generation uses no
// files, random state, or platform API.
[[nodiscard]] core::Result<EnvironmentLighting>
generate_deterministic_environment_lighting();

} // namespace shark::assets
