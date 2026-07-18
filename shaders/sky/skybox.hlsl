#include "shared/camera_constants.hlsli"
#include "shared/daylight.hlsli"

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
    const float3 sky_linear = evaluate_daylight_sky_linear(
        input.sample_direction);
    return float4(linear_to_srgb(sky_linear), 1.0F);
}
