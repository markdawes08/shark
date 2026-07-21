#include "shared/camera_constants.hlsli"
#include "shared/pbr_ibl.hlsli"

cbuffer MaterialSphereTransform : register(b2)
{
    float4 material_sphere_orientation;
    float3 material_sphere_world_position;
}

static const float3 material_sphere_authored_center =
    float3(3.0F, 1.25F, -1.0F);

float3 material_sphere_rotate(
    const float3 value,
    const float4 orientation)
{
    const float3 twice_cross =
        2.0F * cross(orientation.xyz, value);
    return value +
        orientation.w * twice_cross +
        cross(orientation.xyz, twice_cross);
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
    float3 local_direction : TEXCOORD1;
};

VertexOutput VSMain(const VertexInput input)
{
    VertexOutput output;
    const float3 local_position =
        input.position - material_sphere_authored_center;
    const float3 world_position =
        material_sphere_world_position +
        material_sphere_rotate(
            local_position,
            material_sphere_orientation);
    output.position = mul(
        float4(world_position, 1.0F),
        view_projection);
    output.world_position = world_position;
    output.normal = material_sphere_rotate(
        input.normal,
        material_sphere_orientation);
    output.local_direction = input.normal;
    return output;
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    // This deliberately glossy neutral dielectric makes the prefiltered
    // environment response obvious without claiming a metallic material.
    const float3 unit_local_direction = pbr_normalize_or(
        input.local_direction,
        float3(1.0F, 0.0F, 0.0F));
    const float positive_x_marker = smoothstep(
        0.94F,
        0.985F,
        unit_local_direction.x);
    const float3 albedo = lerp(
        float3(0.32F, 0.34F, 0.36F),
        float3(0.42F, 0.30F, 0.22F),
        positive_x_marker * 0.65F);
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
