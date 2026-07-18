#include "shared/camera_constants.hlsli"
#include "shared/daylight.hlsli"

cbuffer SkyEnvironmentConstants : register(b1)
{
    uint environment_mode;
    float3 sky_environment_reserved;
};

TextureCube<float4> environment_radiance_texture : register(t0);
SamplerState environment_radiance_sampler : register(s0);

struct VertexInput
{
    float3 position : POSITION;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 sample_direction : TEXCOORD0;
};

VertexOutput VSMain(const VertexInput input)
{
    VertexOutput output;
    const float4 clip_position = mul(
        float4(input.position, 1.0F),
        sky_view_projection);

    // Shark clears reversed-Z depth to zero. Pin the sky to that far plane;
    // GREATER_EQUAL lets it fill untouched pixels while opaque depth wins.
    output.position = float4(
        clip_position.xy,
        0.0F,
        clip_position.w);
    output.sample_direction = input.position;
    return output;
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    const float3 sample_direction = normalize(
        input.sample_direction);
    float3 sky_linear = evaluate_daylight_sky_linear(
        sample_direction);
    if (environment_mode == 2U)
    {
        sky_linear = max(
            environment_radiance_texture.SampleLevel(
                environment_radiance_sampler,
                sample_direction,
                0.0F).rgb,
            0.0F);

        // Keep the small directional sun analytic. Baking it into the
        // intentionally bounded 32x32 cubemap would turn the disk into a
        // bright texel block and would double-count it in IBL plus direct
        // lighting.
        const float sun_cosine = dot(
            sample_direction,
            direction_to_sun);
        const float disk = smoothstep(
            sun_disk_outer_cosine,
            sun_disk_inner_cosine,
            sun_cosine);
        float halo_axis = saturate(
            (sun_cosine - sun_halo_outer_cosine) /
            (1.0F - sun_halo_outer_cosine));
        halo_axis *= halo_axis;
        const float halo = halo_axis * halo_axis;
        const float sun_radiance =
            0.35F * halo * (1.0F - disk) +
            12.0F * disk;
        sky_linear +=
            sun_color * (sun_intensity * sun_radiance);
    }
    return float4(sky_linear, 1.0F);
}
