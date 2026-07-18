#include "shared/camera_constants.hlsli"
#include "shared/daylight.hlsli"

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
};

VertexOutput VSMain(const VertexInput input)
{
    VertexOutput output;
    output.position = mul(
        float4(input.position, 1.0F),
        view_projection);
    output.normal = input.normal;
    return output;
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    // T-001 encodes the bounds overlay with a negative-Y sentinel normal.
    // Keep it unmistakable while the terrain surface gains daylight shading.
    if (input.normal.y < -0.5F)
    {
        return float4(1.0F, 0.0F, 1.0F, 1.0F);
    }

    const float3 unit_normal = normalize(input.normal);
    const float slope = saturate(1.0F - unit_normal.y);
    const float rock_blend = smoothstep(0.25F, 0.75F, slope);
    const float3 grass_albedo =
        float3(0.20F, 0.32F, 0.12F);
    const float3 rock_albedo =
        float3(0.30F, 0.27F, 0.21F);
    const float3 albedo = lerp(
        grass_albedo,
        rock_albedo,
        rock_blend);
    const float3 illumination =
        evaluate_daylight_illumination(unit_normal);
    const float3 terrain_linear = saturate(
        albedo * illumination);
    return float4(linear_to_srgb(terrain_linear), 1.0F);
}
