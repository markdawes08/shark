#include "shared/camera_constants.hlsli"
#include "shared/pbr_ibl.hlsli"

cbuffer MaterialSphereTransform : register(b2)
{
    float3 material_sphere_translation;
}

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
    const float3 world_position =
        input.position + material_sphere_translation;
    output.position = mul(
        float4(world_position, 1.0F),
        view_projection);
    output.world_position = world_position;
    output.normal = input.normal;
    return output;
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    // This deliberately glossy neutral dielectric makes the prefiltered
    // environment response obvious without claiming a metallic material.
    const float3 albedo = float3(0.32F, 0.34F, 0.36F);
    const float roughness = 0.16F;
    const float3 unit_normal = pbr_normalize_or(
        input.normal,
        float3(0.0F, 1.0F, 0.0F));
    const float3 unit_view_direction = pbr_normalize_or(
        camera_world_position - input.world_position,
        unit_normal);
    const float3 lighting =
        pbr_evaluate_environment(
            albedo,
            roughness,
            unit_normal,
            unit_view_direction) +
        pbr_evaluate_direct_sun(
            albedo,
            roughness,
            unit_normal,
            unit_view_direction);
    return float4(max(lighting, 0.0F), 1.0F);
}
