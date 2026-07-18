#include "shared/camera_constants.hlsli"
#include "shared/daylight.hlsli"

cbuffer TerrainMaterialConstants : register(b1)
{
    float3 camera_world_position;
    uint terrain_material_view;
};

Texture2DArray<float4> terrain_albedo_textures : register(t0);
Texture2DArray<float4> terrain_normal_textures : register(t1);
Texture2DArray<float4> terrain_roughness_textures : register(t2);
SamplerState terrain_material_sampler : register(s0);

static const float pi = 3.14159265358979323846F;
static const float terrain_uv_repeats_per_meter = 0.5F;
static const float terrain_normal_strength = 0.65F;
static const float minimum_terrain_roughness = 0.08F;
static const float3 dielectric_fresnel_at_normal =
    float3(0.04F, 0.04F, 0.04F);

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 normal : NORMAL;
};

VertexOutput VSMain(const VertexInput input)
{
    VertexOutput output;
    output.position = mul(
        float4(input.position, 1.0F),
        view_projection);
    output.world_position = input.position;
    output.normal = input.normal;
    return output;
}

float3 normalize_or(
    const float3 value,
    const float3 fallback)
{
    const float length_squared = dot(value, value);
    return length_squared > 1.0e-12F
        ? value * rsqrt(length_squared)
        : fallback;
}

float3x3 fallback_tangent_frame(const float3 unit_normal)
{
    float3 tangent = float3(1.0F, 0.0F, 0.0F) -
        unit_normal * unit_normal.x;
    if (dot(tangent, tangent) <= 1.0e-12F)
    {
        const float3 forward = float3(0.0F, 0.0F, -1.0F);
        tangent = forward -
            unit_normal * dot(forward, unit_normal);
    }
    tangent = normalize_or(
        tangent,
        float3(1.0F, 0.0F, 0.0F));
    const float3 bitangent = normalize_or(
        cross(unit_normal, tangent),
        float3(0.0F, 0.0F, -1.0F));
    return float3x3(tangent, bitangent, unit_normal);
}

float3x3 cotangent_frame(
    const float3 unit_normal,
    const float3 world_position,
    const float2 uv)
{
    const float3 position_dx = ddx(world_position);
    const float3 position_dy = ddy(world_position);
    const float2 uv_dx = ddx(uv);
    const float2 uv_dy = ddy(uv);

    const float3 position_dy_perpendicular =
        cross(position_dy, unit_normal);
    const float3 position_dx_perpendicular =
        cross(unit_normal, position_dx);
    const float3 tangent =
        position_dy_perpendicular * uv_dx.x +
        position_dx_perpendicular * uv_dy.x;
    const float3 bitangent =
        position_dy_perpendicular * uv_dx.y +
        position_dx_perpendicular * uv_dy.y;
    const float maximum_length_squared = max(
        dot(tangent, tangent),
        dot(bitangent, bitangent));
    if (maximum_length_squared <= 1.0e-12F)
    {
        return fallback_tangent_frame(unit_normal);
    }

    const float inverse_scale = rsqrt(maximum_length_squared);
    return float3x3(
        tangent * inverse_scale,
        bitangent * inverse_scale,
        unit_normal);
}

float3 decode_tangent_normal(const float3 encoded_normal)
{
    float3 tangent_normal =
        encoded_normal * 2.0F - 1.0F;
    tangent_normal.xy *= terrain_normal_strength;
    return normalize_or(
        tangent_normal,
        float3(0.0F, 0.0F, 1.0F));
}

float3 fresnel_schlick(
    const float view_half_cosine,
    const float3 fresnel_at_normal)
{
    const float one_minus_cosine =
        1.0F - saturate(view_half_cosine);
    const float squared =
        one_minus_cosine * one_minus_cosine;
    const float fifth_power =
        squared * squared * one_minus_cosine;
    return fresnel_at_normal +
        (1.0F - fresnel_at_normal) * fifth_power;
}

float smith_ggx_masking(
    const float normal_direction_cosine,
    const float alpha_squared)
{
    const float cosine = saturate(normal_direction_cosine);
    const float denominator = cosine + sqrt(
        alpha_squared +
        (1.0F - alpha_squared) * cosine * cosine);
    return 2.0F * cosine / max(denominator, 1.0e-6F);
}

float3 evaluate_terrain_daylight(
    const float3 albedo,
    const float roughness,
    const float3 unit_normal,
    const float3 world_position)
{
    const float3 ground_ambient =
        float3(0.14F, 0.12F, 0.10F);
    const float hemisphere = saturate(
        unit_normal.y * 0.5F + 0.5F);
    const float3 ambient_irradiance = lerp(
        ground_ambient,
        sky_ambient_color,
        hemisphere) * ambient_strength;
    const float3 ambient = albedo * ambient_irradiance;

    const float3 light_direction = normalize_or(
        direction_to_sun,
        float3(0.0F, 1.0F, 0.0F));
    const float normal_light_cosine = saturate(dot(
        unit_normal,
        light_direction));
    if (normal_light_cosine <= 0.0F)
    {
        return ambient;
    }

    const float3 view_direction = normalize_or(
        camera_world_position - world_position,
        unit_normal);
    const float3 half_direction = normalize_or(
        light_direction + view_direction,
        unit_normal);
    const float normal_view_cosine = saturate(dot(
        unit_normal,
        view_direction));
    const float normal_half_cosine = saturate(dot(
        unit_normal,
        half_direction));
    const float view_half_cosine = saturate(dot(
        view_direction,
        half_direction));

    const float clamped_roughness = clamp(
        roughness,
        minimum_terrain_roughness,
        1.0F);
    const float alpha =
        clamped_roughness * clamped_roughness;
    const float alpha_squared = alpha * alpha;
    const float distribution_denominator =
        normal_half_cosine * normal_half_cosine *
            (alpha_squared - 1.0F) +
        1.0F;
    const float distribution = alpha_squared / max(
        pi * distribution_denominator *
            distribution_denominator,
        1.0e-6F);
    const float masking =
        smith_ggx_masking(
            normal_view_cosine,
            alpha_squared) *
        smith_ggx_masking(
            normal_light_cosine,
            alpha_squared);
    const float3 fresnel = fresnel_schlick(
        view_half_cosine,
        dielectric_fresnel_at_normal);
    const float3 specular = distribution * masking * fresnel /
        max(
            4.0F * normal_view_cosine *
                normal_light_cosine,
            1.0e-6F);
    const float3 diffuse =
        (1.0F - fresnel) * albedo / pi;

    // Multiplying the direct-light radiance by pi preserves the diffuse
    // exposure of Shark's original Lambert daylight while introducing the
    // energy-conserving dielectric BRDF.
    const float3 sunlight_radiance =
        sun_color *
        (pi * 1.25F * sun_intensity);
    return ambient +
        (diffuse + specular) *
            sunlight_radiance *
            normal_light_cosine;
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    // A negative-Y sentinel selects diagnostic lines. Absolute X selects
    // magenta versus cyan through the complementary green channel, while
    // absolute Z supplies blue.
    if (input.normal.y < -0.5F)
    {
        const float3 encoded = saturate(abs(input.normal));
        return float4(
            encoded.x,
            1.0F - encoded.x,
            encoded.z,
            1.0F);
    }

    const float3 macro_normal = normalize(input.normal);
    const float slope = saturate(1.0F - macro_normal.y);
    const float slope_rock = smoothstep(
        0.05F,
        0.18F,
        slope);
    const float height_rock =
        0.40F * smoothstep(
            -1.50F,
            -0.40F,
            input.world_position.y);
    const float rock_weight = saturate(
        1.0F -
        (1.0F - slope_rock) *
            (1.0F - height_rock));
    const float ground_weight = 1.0F - rock_weight;

    // TerrainMaterialView: 1 = shaded, 2 = layer weights,
    // 3 = world-space shading normal. Invalid values fall back to shaded.
    if (terrain_material_view == 2U)
    {
        const float3 weights_linear =
            float3(ground_weight, 0.0F, rock_weight);
        return float4(
            linear_to_srgb(weights_linear),
            1.0F);
    }

    // U follows +X and V follows -Z so a flat heightfield produces a
    // right-handed tangent frame under Shark's world convention.
    const float2 terrain_uv =
        float2(
            input.world_position.x,
            -input.world_position.z) *
        terrain_uv_repeats_per_meter;
    const float3 ground_tangent_normal = decode_tangent_normal(
        terrain_normal_textures.Sample(
            terrain_material_sampler,
            float3(terrain_uv, 0.0F)).xyz);
    const float3 rock_tangent_normal = decode_tangent_normal(
        terrain_normal_textures.Sample(
            terrain_material_sampler,
            float3(terrain_uv, 1.0F)).xyz);
    const float3 blended_tangent_normal = normalize_or(
        ground_tangent_normal * ground_weight +
            rock_tangent_normal * rock_weight,
        float3(0.0F, 0.0F, 1.0F));
    const float3x3 tangent_frame = cotangent_frame(
        macro_normal,
        input.world_position,
        terrain_uv);
    const float3 shading_normal = normalize_or(
        mul(blended_tangent_normal, tangent_frame),
        macro_normal);

    if (terrain_material_view == 3U)
    {
        const float3 normal_linear =
            shading_normal * 0.5F + 0.5F;
        return float4(
            linear_to_srgb(normal_linear),
            1.0F);
    }

    const float3 ground_albedo =
        terrain_albedo_textures.Sample(
            terrain_material_sampler,
            float3(terrain_uv, 0.0F)).rgb;
    const float3 rock_albedo =
        terrain_albedo_textures.Sample(
            terrain_material_sampler,
            float3(terrain_uv, 1.0F)).rgb;
    const float3 albedo =
        ground_albedo * ground_weight +
        rock_albedo * rock_weight;

    const float ground_roughness = clamp(
        terrain_roughness_textures.Sample(
            terrain_material_sampler,
            float3(terrain_uv, 0.0F)).r,
        minimum_terrain_roughness,
        1.0F);
    const float rock_roughness = clamp(
        terrain_roughness_textures.Sample(
            terrain_material_sampler,
            float3(terrain_uv, 1.0F)).r,
        minimum_terrain_roughness,
        1.0F);
    const float roughness = sqrt(
        ground_roughness * ground_roughness * ground_weight +
        rock_roughness * rock_roughness * rock_weight);
    const float3 terrain_linear = saturate(
        evaluate_terrain_daylight(
            albedo,
            roughness,
            shading_normal,
            input.world_position));
    return float4(linear_to_srgb(terrain_linear), 1.0F);
}
