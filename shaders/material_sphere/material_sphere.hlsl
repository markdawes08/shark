#include "shared/camera_constants.hlsli"
#include "shared/pbr_ibl.hlsli"

cbuffer MaterialSphereTransform : register(b2)
{
    float4 material_sphere_orientation;
    float3 material_sphere_world_position;
    float environment_proxy_radius;
    float environment_proxy_half_segment_length;
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

void environment_proxy_surface(
    const float3 authored_local_position,
    const float3 unit_direction,
    out float3 local_position,
    out float3 local_normal,
    out float capsule_proxy)
{
    // Keep the original material-sphere path exact. Shark's procedural avatar
    // parts reuse the same mesh only as a bounded capsule parameterization.
    if (environment_proxy_half_segment_length == 0.0F)
    {
        local_position =
            authored_local_position * environment_proxy_radius;
        local_normal = unit_direction;
        capsule_proxy = 0.0F;
        return;
    }

    static const float capsule_parameter_seam = 0.5F;
    if (unit_direction.y >= capsule_parameter_seam)
    {
        local_normal = pbr_normalize_or(
            float3(
                unit_direction.x,
                2.0F * unit_direction.y - 1.0F,
                unit_direction.z),
            float3(0.0F, 1.0F, 0.0F));
        local_position =
            local_normal * environment_proxy_radius;
        local_position.y +=
            environment_proxy_half_segment_length;
    }
    else if (unit_direction.y <= -capsule_parameter_seam)
    {
        local_normal = pbr_normalize_or(
            float3(
                unit_direction.x,
                2.0F * unit_direction.y + 1.0F,
                unit_direction.z),
            float3(0.0F, -1.0F, 0.0F));
        local_position =
            local_normal * environment_proxy_radius;
        local_position.y -=
            environment_proxy_half_segment_length;
    }
    else
    {
        local_normal = pbr_normalize_or(
            float3(
                unit_direction.x,
                0.0F,
                unit_direction.z),
            float3(1.0F, 0.0F, 0.0F));
        local_position = float3(
            local_normal.x * environment_proxy_radius,
            environment_proxy_half_segment_length *
                (unit_direction.y / capsule_parameter_seam),
            local_normal.z * environment_proxy_radius);
    }
    capsule_proxy = 1.0F;
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
    nointerpolation float capsule_proxy : TEXCOORD2;
};

VertexOutput VSMain(const VertexInput input)
{
    VertexOutput output;
    const float3 authored_local_position =
        input.position - material_sphere_authored_center;
    float3 local_position;
    float3 local_normal;
    float capsule_proxy;
    environment_proxy_surface(
        authored_local_position,
        input.normal,
        local_position,
        local_normal,
        capsule_proxy);
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
        local_normal,
        material_sphere_orientation);
    output.local_direction = local_normal;
    output.capsule_proxy = capsule_proxy;
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
    const float3 material_sphere_albedo = lerp(
        float3(0.32F, 0.34F, 0.36F),
        float3(0.42F, 0.30F, 0.22F),
        positive_x_marker * 0.65F);
    const float3 albedo = lerp(
        material_sphere_albedo,
        float3(0.08F, 0.30F, 0.72F),
        input.capsule_proxy);
    const float roughness = lerp(
        0.16F,
        0.34F,
        input.capsule_proxy);
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
