#include "shared/camera_constants.hlsli"
#include "shared/pbr_ibl.hlsli"

static const float terrain_uv_repeats_per_meter = 0.5F;
static const float terrain_normal_strength = 0.65F;

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
    tangent = pbr_normalize_or(
        tangent,
        float3(1.0F, 0.0F, 0.0F));
    const float3 bitangent = pbr_normalize_or(
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
    return pbr_normalize_or(
        tangent_normal,
        float3(0.0F, 0.0F, 1.0F));
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
    if (material_view == 2U)
    {
        const float3 weights_linear =
            float3(ground_weight, 0.0F, rock_weight);
        return float4(weights_linear, 1.0F);
    }

    // U follows +X and V follows -Z so a flat heightfield produces a
    // right-handed tangent frame under Shark's world convention.
    const float2 terrain_uv =
        float2(
            input.world_position.x,
            -input.world_position.z) *
        terrain_uv_repeats_per_meter;
    const float3 ground_tangent_normal = decode_tangent_normal(
        material_normal_textures.Sample(
            material_environment_sampler,
            float3(terrain_uv, 0.0F)).xyz);
    const float3 rock_tangent_normal = decode_tangent_normal(
        material_normal_textures.Sample(
            material_environment_sampler,
            float3(terrain_uv, 1.0F)).xyz);
    const float3 blended_tangent_normal = pbr_normalize_or(
        ground_tangent_normal * ground_weight +
            rock_tangent_normal * rock_weight,
        float3(0.0F, 0.0F, 1.0F));
    const float3x3 tangent_frame = cotangent_frame(
        macro_normal,
        input.world_position,
        terrain_uv);
    const float3 shading_normal = pbr_normalize_or(
        mul(blended_tangent_normal, tangent_frame),
        macro_normal);

    if (material_view == 3U)
    {
        const float3 normal_linear =
            shading_normal * 0.5F + 0.5F;
        return float4(normal_linear, 1.0F);
    }

    const float3 ground_albedo =
        material_albedo_textures.Sample(
            material_environment_sampler,
            float3(terrain_uv, 0.0F)).rgb;
    const float3 rock_albedo =
        material_albedo_textures.Sample(
            material_environment_sampler,
            float3(terrain_uv, 1.0F)).rgb;
    const float3 albedo =
        ground_albedo * ground_weight +
        rock_albedo * rock_weight;

    const float ground_roughness = clamp(
        material_roughness_textures.Sample(
            material_environment_sampler,
            float3(terrain_uv, 0.0F)).r,
        pbr_minimum_roughness,
        1.0F);
    const float rock_roughness = clamp(
        material_roughness_textures.Sample(
            material_environment_sampler,
            float3(terrain_uv, 1.0F)).r,
        pbr_minimum_roughness,
        1.0F);
    const float roughness = sqrt(
        ground_roughness * ground_roughness * ground_weight +
        rock_roughness * rock_roughness * rock_weight);
    const float3 unit_view_direction = pbr_normalize_or(
        camera_world_position - input.world_position,
        shading_normal);
    const float3 terrain_linear =
        pbr_evaluate_environment(
            albedo,
            roughness,
            shading_normal,
            unit_view_direction) +
        pbr_evaluate_direct_sun(
            albedo,
            roughness,
            shading_normal,
            unit_view_direction);
    return float4(max(terrain_linear, 0.0F), 1.0F);
}
