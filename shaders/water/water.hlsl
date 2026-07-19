#include "shared/camera_constants.hlsli"
#include "shared/daylight.hlsli"

cbuffer WaterSurfaceConstants : register(b1)
{
    float3 water_camera_world_position;
    float water_visual_time_seconds;

    float2 water_center;
    float2 water_semi_axes;

    float water_x_warp_square_offset;
    float water_x_warp_divisor;
    float water_z_warp_square_offset;
    float water_z_warp_divisor;

    float waterline_y;
    float water_core_depth;
    float2 water_render_half_extents;

    uint water_environment_mode;
    float water_environment_max_lod;
    float2 water_reserved;
};

TextureCube<float4> water_environment_radiance : register(t0);
SamplerState water_environment_sampler : register(s0);

static const float2 water_quad_corners[6] =
{
    float2(-1.0F, -1.0F),
    float2(-1.0F, 1.0F),
    float2(1.0F, 1.0F),
    float2(-1.0F, -1.0F),
    float2(1.0F, 1.0F),
    float2(1.0F, -1.0F),
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 world_position : TEXCOORD0;
};

VertexOutput VSMain(const uint vertex_id : SV_VertexID)
{
    VertexOutput output;
    const float2 horizontal =
        water_center +
        water_quad_corners[vertex_id] *
            water_render_half_extents;
    output.world_position =
        float3(horizontal.x, waterline_y, horizontal.y);
    output.position = mul(
        float4(output.world_position, 1.0F),
        view_projection);
    return output;
}

float water_normalized_radius_squared(const float2 position)
{
    const float2 offset = position - water_center;
    const float warped_x =
        offset.x +
        (offset.y * offset.y -
         water_x_warp_square_offset) /
            water_x_warp_divisor;
    const float warped_z =
        offset.y +
        (offset.x * offset.x -
         water_z_warp_square_offset) /
            water_z_warp_divisor;
    const float2 normalized =
        float2(warped_x, warped_z) /
        water_semi_axes;
    return dot(normalized, normalized);
}

float3 water_wave_normal(
    const float2 position,
    const float time_seconds)
{
    const float2 direction_a =
        normalize(float2(0.82F, 0.57F));
    const float2 direction_b =
        normalize(float2(-0.36F, 0.93F));
    const float frequency_a = 0.22F;
    const float frequency_b = 0.37F;
    const float amplitude_a = 0.055F;
    const float amplitude_b = 0.028F;
    const float phase_a =
        dot(position, direction_a) * frequency_a +
        time_seconds * 0.85F;
    const float phase_b =
        dot(position, direction_b) * frequency_b -
        time_seconds * 1.15F;
    const float2 gradient =
        direction_a *
            (amplitude_a * frequency_a * cos(phase_a)) +
        direction_b *
            (amplitude_b * frequency_b * cos(phase_b));
    return normalize(float3(-gradient.x, 1.0F, -gradient.y));
}

float3 water_environment_sample(const float3 direction)
{
    if (water_environment_mode == 2U)
    {
        return max(
            water_environment_radiance.SampleLevel(
                water_environment_sampler,
                direction,
                min(1.0F, water_environment_max_lod)).rgb,
            0.0F);
    }
    return evaluate_daylight_sky_linear(direction);
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    const float radius_squared =
        water_normalized_radius_squared(input.world_position.xz);
    clip(1.0F - radius_squared);

    const float interior = saturate(1.0F - radius_squared);
    const float edge_coverage = saturate(interior * 48.0F);
    const float approximate_depth =
        water_core_depth * interior * interior;
    const float depth_amount = saturate(
        approximate_depth / max(water_core_depth, 1.0e-4F));
    const float absorption =
        1.0F - exp(-0.42F * approximate_depth);

    const float3 unit_normal = water_wave_normal(
        input.world_position.xz,
        water_visual_time_seconds);
    const float3 unit_view_direction = normalize(
        water_camera_world_position - input.world_position);
    const float normal_view_cosine = saturate(dot(
        unit_normal,
        unit_view_direction));
    const float one_minus_cosine = 1.0F - normal_view_cosine;
    const float fresnel =
        0.02F +
        0.98F *
            one_minus_cosine * one_minus_cosine *
            one_minus_cosine * one_minus_cosine *
            one_minus_cosine;

    const float3 reflection_direction = reflect(
        -unit_view_direction,
        unit_normal);
    float3 reflection =
        water_environment_sample(reflection_direction);

    // The retained IBL radiance intentionally omits the tiny sun disk.
    // Restore a bounded highlight analytically so both lighting modes agree.
    const float3 half_direction = normalize(
        direction_to_sun + unit_view_direction);
    const float sun_glint = pow(
        saturate(dot(unit_normal, half_direction)),
        192.0F);
    reflection +=
        sun_color * (sun_intensity * 1.6F * sun_glint);

    const float3 refraction_direction = refract(
        -unit_view_direction,
        unit_normal,
        0.7501875F);
    const float3 refracted_environment =
        water_environment_sample(refraction_direction);
    const float3 shallow_tint =
        float3(0.055F, 0.30F, 0.34F);
    const float3 deep_tint =
        float3(0.015F, 0.085F, 0.12F);
    const float3 absorption_tint = lerp(
        shallow_tint,
        deep_tint,
        depth_amount);
    const float3 scatter =
        absorption *
        (absorption_tint +
         refracted_environment * 0.08F);

    // Premultiplied alpha leaves the already-rendered terrain visible as
    // straight-through transmission while reflection and absorption grow
    // with Fresnel and the analytic basin-depth proxy.
    const float alpha = edge_coverage * saturate(
        fresnel + (1.0F - fresnel) * absorption);
    const float3 premultiplied_color = edge_coverage * (
        fresnel * reflection +
        (1.0F - fresnel) * scatter);
    return float4(max(premultiplied_color, 0.0F), alpha);
}
