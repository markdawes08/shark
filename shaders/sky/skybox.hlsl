#include "shared/camera_constants.hlsli"

TextureCube<float4> skybox_texture : register(t0);
SamplerState skybox_sampler : register(s0);

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
    const float4 diagnostic_sample = skybox_texture.Sample(
        skybox_sampler,
        input.sample_direction);

    // Temporary presentation treatment until Shark owns an HDR environment.
    // Retain a small contribution from the orientation cubemap so this pass
    // continues to exercise the real TextureCube sampling path.
    const float3 sky_blue = float3(0.36F, 0.62F, 0.90F);
    const float3 treated_color = lerp(
        diagnostic_sample.rgb,
        sky_blue,
        0.96F);
    return float4(treated_color, 1.0F);
}
