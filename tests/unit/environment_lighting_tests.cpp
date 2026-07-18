#include <shark/assets/environment_lighting.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace {

[[nodiscard]] std::array<float, 4> rgba_at(
    const std::span<const std::byte> pixels,
    const std::size_t row_pitch,
    const std::uint32_t x,
    const std::uint32_t y)
{
    std::array<float, 4> result{};
    const auto offset =
        static_cast<std::size_t>(y) * row_pitch +
        static_cast<std::size_t>(x) *
            shark::assets::environment_texel_bytes;
    REQUIRE(offset <= pixels.size());
    REQUIRE(
        pixels.size() - offset >=
        shark::assets::environment_texel_bytes);
    std::memcpy(
        result.data(),
        pixels.data() + offset,
        shark::assets::environment_texel_bytes);
    return result;
}

void require_finite_nonnegative(
    const shark::assets::EnvironmentCubeSubresourceView& view)
{
    REQUIRE(
        view.row_pitch ==
        static_cast<std::size_t>(view.width) *
            shark::assets::environment_texel_bytes);
    REQUIRE(
        view.slice_pitch ==
        view.row_pitch * view.height);
    REQUIRE(view.pixels.size() == view.slice_pitch);
    for (std::uint32_t y = 0; y < view.height; ++y) {
        for (std::uint32_t x = 0; x < view.width; ++x) {
            const auto pixel = rgba_at(
                view.pixels,
                view.row_pitch,
                x,
                y);
            for (const auto channel : pixel) {
                REQUIRE(std::isfinite(channel));
                REQUIRE(channel >= 0.0F);
            }
            REQUIRE(pixel[3] == 1.0F);
        }
    }
}

[[nodiscard]] float peak_rgb(
    const shark::assets::EnvironmentCubeSubresourceView& view)
{
    float result = 0.0F;
    for (std::uint32_t y = 0; y < view.height; ++y) {
        for (std::uint32_t x = 0; x < view.width; ++x) {
            const auto pixel = rgba_at(
                view.pixels,
                view.row_pitch,
                x,
                y);
            result = std::max({
                result,
                pixel[0],
                pixel[1],
                pixel[2],
            });
        }
    }
    return result;
}

[[nodiscard]] float average_rgb(
    const shark::assets::EnvironmentCubeSubresourceView& view)
{
    double result = 0.0;
    for (std::uint32_t y = 0; y < view.height; ++y) {
        for (std::uint32_t x = 0; x < view.width; ++x) {
            const auto pixel = rgba_at(
                view.pixels,
                view.row_pitch,
                x,
                y);
            result +=
                static_cast<double>(pixel[0]) +
                static_cast<double>(pixel[1]) +
                static_cast<double>(pixel[2]);
        }
    }
    return static_cast<float>(
        result /
        static_cast<double>(
            view.width * view.height * 3U));
}

} // namespace

TEST_CASE(
    "environment lighting owns the exact bounded HDR map contract",
    "[assets][environment][ibl][contract]")
{
    using namespace shark::assets;

    STATIC_REQUIRE(environment_source_width == 64);
    STATIC_REQUIRE(environment_source_height == 32);
    STATIC_REQUIRE(environment_source_texel_count == 2'048);
    STATIC_REQUIRE(environment_source_bytes == 32'768);
    STATIC_REQUIRE(environment_radiance_dimension == 32);
    STATIC_REQUIRE(environment_radiance_mip_levels == 6);
    STATIC_REQUIRE(environment_irradiance_dimension == 8);
    STATIC_REQUIRE(environment_irradiance_mip_levels == 1);
    STATIC_REQUIRE(environment_specular_dimension == 32);
    STATIC_REQUIRE(environment_specular_mip_levels == 6);
    STATIC_REQUIRE(environment_brdf_lut_dimension == 32);
    STATIC_REQUIRE(environment_radiance_subresource_count == 36);
    STATIC_REQUIRE(environment_irradiance_subresource_count == 6);
    STATIC_REQUIRE(environment_specular_subresource_count == 36);
    STATIC_REQUIRE(environment_derived_subresource_count == 79);
    STATIC_REQUIRE(environment_derived_texel_count == 17'788);
    STATIC_REQUIRE(environment_derived_bytes == 284'608);

    auto result = generate_deterministic_environment_lighting();
    REQUIRE(result);
    auto environment = std::move(result).value();

    const auto& metadata = environment.metadata();
    REQUIRE(
        metadata.source_texel_count ==
        environment_source_texel_count);
    REQUIRE(
        metadata.source_byte_count ==
        environment_source_bytes);
    REQUIRE(
        metadata.derived_texel_count ==
        environment_derived_texel_count);
    REQUIRE(
        metadata.derived_subresource_count ==
        environment_derived_subresource_count);
    REQUIRE(
        metadata.derived_byte_count ==
        environment_derived_bytes);
    REQUIRE(metadata.source_peak_radiance > 1.0F);
    REQUIRE(metadata.source_peak_radiance < 2.0F);
    REQUIRE(metadata.radiance_peak > 1.0F);
    REQUIRE(metadata.radiance_peak < 2.0F);
    REQUIRE(metadata.diffuse_irradiance_peak > 0.0F);
    REQUIRE(metadata.specular_peak > 1.0F);
    REQUIRE(metadata.specular_peak < 2.0F);
    REQUIRE(metadata.brdf_lut_peak > 0.0F);
    REQUIRE(
        metadata.derived_peak_value ==
        std::max({
            metadata.radiance_peak,
            metadata.diffuse_irradiance_peak,
            metadata.specular_peak,
            metadata.brdf_lut_peak,
        }));

    std::size_t derived_bytes = 0;
    REQUIRE(
        environment.radiance_subresource_count() ==
        environment_radiance_subresource_count);
    for (std::size_t face = 0; face < cubemap_face_count; ++face) {
        for (std::uint32_t mip = 0;
             mip < environment_radiance_mip_levels;
             ++mip) {
            const auto index =
                face * environment_radiance_mip_levels + mip;
            const auto view =
                environment.radiance_subresource(index);
            REQUIRE(view);
            REQUIRE(
                view->face ==
                static_cast<CubemapFace>(face));
            REQUIRE(view->mip_level == mip);
            REQUIRE(
                view->width ==
                environment_mip_dimension(
                    environment_radiance_dimension,
                    mip));
            REQUIRE(view->height == view->width);
            derived_bytes += view->pixels.size();
        }
    }

    REQUIRE(
        environment.irradiance_subresource_count() ==
        environment_irradiance_subresource_count);
    for (std::size_t index = 0;
         index < environment.irradiance_subresource_count();
         ++index) {
        const auto view =
            environment.irradiance_subresource(index);
        REQUIRE(view);
        REQUIRE(view->mip_level == 0);
        REQUIRE(view->width == environment_irradiance_dimension);
        REQUIRE(view->height == environment_irradiance_dimension);
        derived_bytes += view->pixels.size();
    }

    REQUIRE(
        environment.specular_subresource_count() ==
        environment_specular_subresource_count);
    for (std::size_t index = 0;
         index < environment.specular_subresource_count();
         ++index) {
        const auto view =
            environment.specular_subresource(index);
        REQUIRE(view);
        derived_bytes += view->pixels.size();
    }

    const auto lut = environment.brdf_lut();
    REQUIRE(lut.width == environment_brdf_lut_dimension);
    REQUIRE(lut.height == environment_brdf_lut_dimension);
    REQUIRE(lut.mip_level == 0);
    REQUIRE(
        lut.row_pitch ==
        static_cast<std::size_t>(lut.width) *
            environment_texel_bytes);
    REQUIRE(lut.slice_pitch == lut.row_pitch * lut.height);
    REQUIRE(lut.pixels.size() == lut.slice_pitch);
    derived_bytes += lut.pixels.size();
    REQUIRE(derived_bytes == environment_derived_bytes);

    REQUIRE_FALSE(environment.radiance_subresource(
        environment_radiance_subresource_count));
    REQUIRE_FALSE(environment.irradiance_subresource(
        CubemapFace::positive_x,
        1));
    REQUIRE_FALSE(environment.specular_subresource(
        static_cast<CubemapFace>(255),
        0));
}

TEST_CASE(
    "environment lighting generation is byte deterministic",
    "[assets][environment][ibl][determinism]")
{
    using namespace shark::assets;

    auto first_result =
        generate_deterministic_environment_lighting();
    auto second_result =
        generate_deterministic_environment_lighting();
    REQUIRE(first_result);
    REQUIRE(second_result);
    auto first = std::move(first_result).value();
    auto second = std::move(second_result).value();

    REQUIRE(first.metadata() == second.metadata());
    for (std::size_t index = 0;
         index < first.radiance_subresource_count();
         ++index) {
        const auto left = first.radiance_subresource(index);
        const auto right = second.radiance_subresource(index);
        REQUIRE(left);
        REQUIRE(right);
        REQUIRE(std::ranges::equal(
            left->pixels,
            right->pixels));
    }
    for (std::size_t index = 0;
         index < first.irradiance_subresource_count();
         ++index) {
        const auto left = first.irradiance_subresource(index);
        const auto right = second.irradiance_subresource(index);
        REQUIRE(left);
        REQUIRE(right);
        REQUIRE(std::ranges::equal(
            left->pixels,
            right->pixels));
    }
    for (std::size_t index = 0;
         index < first.specular_subresource_count();
         ++index) {
        const auto left = first.specular_subresource(index);
        const auto right = second.specular_subresource(index);
        REQUIRE(left);
        REQUIRE(right);
        REQUIRE(std::ranges::equal(
            left->pixels,
            right->pixels));
    }
    REQUIRE(std::ranges::equal(
        first.brdf_lut().pixels,
        second.brdf_lut().pixels));
}

TEST_CASE(
    "environment convolution and GGX prefilter remain finite and bounded",
    "[assets][environment][ibl][convolution]")
{
    using namespace shark::assets;

    auto result = generate_deterministic_environment_lighting();
    REQUIRE(result);
    auto environment = std::move(result).value();

    float radiance_peak = 0.0F;
    float roughest_specular_peak = 0.0F;
    for (std::size_t index = 0;
         index < environment.radiance_subresource_count();
         ++index) {
        const auto radiance =
            environment.radiance_subresource(index);
        REQUIRE(radiance);
        require_finite_nonnegative(*radiance);
        radiance_peak = std::max(
            radiance_peak,
            peak_rgb(*radiance));

        if (radiance->mip_level == 0) {
            const auto specular =
                environment.specular_subresource(
                    radiance->face,
                    0);
            REQUIRE(specular);
            REQUIRE(std::ranges::equal(
                radiance->pixels,
                specular->pixels));
        }
    }
    REQUIRE(radiance_peak > 1.0F);

    for (std::size_t index = 0;
         index < environment.specular_subresource_count();
         ++index) {
        const auto specular =
            environment.specular_subresource(index);
        REQUIRE(specular);
        require_finite_nonnegative(*specular);
        if (specular->mip_level ==
            environment_specular_mip_levels - 1U) {
            roughest_specular_peak = std::max(
                roughest_specular_peak,
                peak_rgb(*specular));
        }
    }
    REQUIRE(roughest_specular_peak > 0.0F);
    REQUIRE(roughest_specular_peak < radiance_peak);

    for (std::size_t index = 0;
         index < environment.irradiance_subresource_count();
         ++index) {
        const auto irradiance =
            environment.irradiance_subresource(index);
        REQUIRE(irradiance);
        require_finite_nonnegative(*irradiance);
    }
    const auto upper = environment.irradiance_subresource(
        CubemapFace::positive_y,
        0);
    const auto lower = environment.irradiance_subresource(
        CubemapFace::negative_y,
        0);
    REQUIRE(upper);
    REQUIRE(lower);
    REQUIRE(average_rgb(*upper) > average_rgb(*lower));
}

TEST_CASE(
    "split sum BRDF LUT uses finite RG scale and bias",
    "[assets][environment][ibl][brdf]")
{
    using namespace shark::assets;

    auto result = generate_deterministic_environment_lighting();
    REQUIRE(result);
    auto environment = std::move(result).value();
    const auto lut = environment.brdf_lut();

    bool found_scale = false;
    bool found_bias = false;
    std::array<float, 4> first{};
    std::array<float, 4> last{};
    for (std::uint32_t y = 0; y < lut.height; ++y) {
        for (std::uint32_t x = 0; x < lut.width; ++x) {
            const auto pixel = rgba_at(
                lut.pixels,
                lut.row_pitch,
                x,
                y);
            for (const auto channel : pixel) {
                REQUIRE(std::isfinite(channel));
                REQUIRE(channel >= 0.0F);
            }
            REQUIRE(pixel[2] == 0.0F);
            REQUIRE(pixel[3] == 1.0F);
            found_scale = found_scale || pixel[0] > 0.0F;
            found_bias = found_bias || pixel[1] > 0.0F;
            if (x == 0 && y == 0) {
                first = pixel;
            }
            if (x + 1U == lut.width &&
                y + 1U == lut.height) {
                last = pixel;
            }
        }
    }
    REQUIRE(found_scale);
    REQUIRE(found_bias);
    REQUIRE(first != last);
}
