#ifndef SHARK_PBR_IBL_HLSLI
#define SHARK_PBR_IBL_HLSLI

// Shared by terrain and the S-003 material-sphere proof. The root constants
// occupy exactly eight DWORDs and the SRVs match the contiguous terrain table.
cbuffer EnvironmentMaterialConstants : register(b1)
{
    float3 camera_world_position;
    uint material_view;

    uint environment_mode;
    float specular_max_lod;
    float2 environment_material_reserved;
};

Texture2DArray<float4> material_albedo_textures : register(t0);
Texture2DArray<float4> material_normal_textures : register(t1);
Texture2DArray<float4> material_roughness_textures : register(t2);
TextureCube<float4> environment_irradiance_texture : register(t3);
TextureCube<float4> environment_prefiltered_specular_texture : register(t4);
Texture2D<float4> environment_brdf_lut : register(t5);
SamplerState material_environment_sampler : register(s0);

static const float pbr_pi = 3.14159265358979323846F;
static const float pbr_minimum_roughness = 0.08F;
static const float3 pbr_dielectric_fresnel_at_normal =
    float3(0.04F, 0.04F, 0.04F);

float3 pbr_normalize_or(
    const float3 value,
    const float3 fallback)
{
    const float length_squared = dot(value, value);
    return length_squared > 1.0e-12F
        ? value * rsqrt(length_squared)
        : fallback;
}

float pbr_clamp_roughness(const float roughness)
{
    return clamp(
        roughness,
        pbr_minimum_roughness,
        1.0F);
}

float3 pbr_fresnel_schlick(
    const float direction_cosine,
    const float3 fresnel_at_normal)
{
    const float one_minus_cosine =
        1.0F - saturate(direction_cosine);
    const float squared =
        one_minus_cosine * one_minus_cosine;
    const float fifth_power =
        squared * squared * one_minus_cosine;
    return fresnel_at_normal +
        (1.0F - fresnel_at_normal) * fifth_power;
}

float3 pbr_fresnel_schlick_roughness(
    const float direction_cosine,
    const float3 fresnel_at_normal,
    const float roughness)
{
    const float3 grazing_fresnel = max(
        1.0F - roughness,
        fresnel_at_normal);
    const float one_minus_cosine =
        1.0F - saturate(direction_cosine);
    const float squared =
        one_minus_cosine * one_minus_cosine;
    const float fifth_power =
        squared * squared * one_minus_cosine;
    return fresnel_at_normal +
        (grazing_fresnel - fresnel_at_normal) *
            fifth_power;
}

float pbr_smith_ggx_masking(
    const float normal_direction_cosine,
    const float alpha_squared)
{
    const float cosine = saturate(normal_direction_cosine);
    const float denominator = cosine + sqrt(
        alpha_squared +
        (1.0F - alpha_squared) * cosine * cosine);
    return 2.0F * cosine / max(denominator, 1.0e-6F);
}

float3 pbr_evaluate_direct_sun(
    const float3 albedo,
    const float roughness,
    const float3 unit_normal,
    const float3 unit_view_direction)
{
    const float3 light_direction = pbr_normalize_or(
        direction_to_sun,
        float3(0.0F, 1.0F, 0.0F));
    const float normal_light_cosine = saturate(dot(
        unit_normal,
        light_direction));
    if (normal_light_cosine <= 0.0F)
    {
        return float3(0.0F, 0.0F, 0.0F);
    }

    const float3 half_direction = pbr_normalize_or(
        light_direction + unit_view_direction,
        unit_normal);
    const float normal_view_cosine = saturate(dot(
        unit_normal,
        unit_view_direction));
    const float normal_half_cosine = saturate(dot(
        unit_normal,
        half_direction));
    const float view_half_cosine = saturate(dot(
        unit_view_direction,
        half_direction));

    const float clamped_roughness =
        pbr_clamp_roughness(roughness);
    const float alpha =
        clamped_roughness * clamped_roughness;
    const float alpha_squared = alpha * alpha;
    const float distribution_denominator =
        normal_half_cosine * normal_half_cosine *
            (alpha_squared - 1.0F) +
        1.0F;
    const float distribution = alpha_squared / max(
        pbr_pi * distribution_denominator *
            distribution_denominator,
        1.0e-6F);
    const float masking =
        pbr_smith_ggx_masking(
            normal_view_cosine,
            alpha_squared) *
        pbr_smith_ggx_masking(
            normal_light_cosine,
            alpha_squared);
    const float3 fresnel = pbr_fresnel_schlick(
        view_half_cosine,
        pbr_dielectric_fresnel_at_normal);
    const float3 specular = distribution * masking * fresnel /
        max(
            4.0F * normal_view_cosine *
                normal_light_cosine,
            1.0e-6F);
    const float3 diffuse =
        (1.0F - fresnel) * albedo / pbr_pi;

    // The pi factor preserves T-003's diffuse exposure after expressing the
    // direct light as a radiance multiplied by a Lambert/GGX BRDF.
    const float3 sunlight_radiance =
        sun_color *
        (pbr_pi * 1.25F * sun_intensity);
    return (diffuse + specular) *
        sunlight_radiance *
        normal_light_cosine;
}

float3 pbr_evaluate_procedural_ambient(
    const float3 albedo,
    const float3 unit_normal)
{
    const float3 ground_ambient =
        float3(0.14F, 0.12F, 0.10F);
    const float hemisphere = saturate(
        unit_normal.y * 0.5F + 0.5F);
    const float3 ambient_irradiance = lerp(
        ground_ambient,
        sky_ambient_color,
        hemisphere) * ambient_strength;
    return albedo * ambient_irradiance;
}

float2 pbr_sample_brdf_lut(
    const float normal_view_cosine,
    const float roughness)
{
    uint width = 0U;
    uint height = 0U;
    environment_brdf_lut.GetDimensions(width, height);
    const float2 dimensions = max(
        float2(
            float(width),
            float(height)),
        1.0F);
    const float2 half_texel = 0.5F / dimensions;
    const float2 lookup = clamp(
        float2(
            saturate(normal_view_cosine),
            pbr_clamp_roughness(roughness)),
        half_texel,
        1.0F - half_texel);
    return environment_brdf_lut.SampleLevel(
        material_environment_sampler,
        lookup,
        0.0F).rg;
}

float3 pbr_evaluate_ibl(
    const float3 albedo,
    const float roughness,
    const float3 unit_normal,
    const float3 unit_view_direction)
{
    const float clamped_roughness =
        pbr_clamp_roughness(roughness);
    const float normal_view_cosine = saturate(dot(
        unit_normal,
        unit_view_direction));
    const float3 fresnel = pbr_fresnel_schlick_roughness(
        normal_view_cosine,
        pbr_dielectric_fresnel_at_normal,
        clamped_roughness);
    const float3 diffuse_weight = 1.0F - fresnel;
    const float3 irradiance = max(
        environment_irradiance_texture.SampleLevel(
            material_environment_sampler,
            unit_normal,
            0.0F).rgb,
        0.0F);
    const float3 diffuse =
        diffuse_weight * albedo * irradiance / pbr_pi;

    const float3 reflection_direction = reflect(
        -unit_view_direction,
        unit_normal);
    const float mip_level =
        clamped_roughness * max(specular_max_lod, 0.0F);
    const float3 prefiltered_specular = max(
        environment_prefiltered_specular_texture.SampleLevel(
            material_environment_sampler,
            reflection_direction,
            mip_level).rgb,
        0.0F);
    const float2 brdf = pbr_sample_brdf_lut(
        normal_view_cosine,
        clamped_roughness);
    // The LUT's scale/bias pair already integrates the angular Fresnel
    // factor, so split-sum reconstruction uses the material F0 here.
    const float3 specular =
        prefiltered_specular *
        (pbr_dielectric_fresnel_at_normal * brdf.x + brdf.y);
    return diffuse + specular;
}

float3 pbr_evaluate_environment(
    const float3 albedo,
    const float roughness,
    const float3 unit_normal,
    const float3 unit_view_direction)
{
    return environment_mode == 2U
        ? pbr_evaluate_ibl(
            albedo,
            roughness,
            unit_normal,
            unit_view_direction)
        : pbr_evaluate_procedural_ambient(
            albedo,
            unit_normal);
}

#endif
