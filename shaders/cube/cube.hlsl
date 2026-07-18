#include "shared/camera_constants.hlsli"

Texture2D<float4> checker_texture : register(t0);
SamplerState checker_sampler : register(s0);

struct VertexInput
{
    float3 position : POSITION;
    float2 texture_coordinates : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 texture_coordinates : TEXCOORD0;
};

VertexOutput VSMain(const VertexInput input)
{
    VertexOutput output;
    output.position = mul(
        float4(input.position, 1.0F),
        view_projection);
    output.texture_coordinates = input.texture_coordinates;
    return output;
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    // The checker is a linear diagnostic color and now feeds Shark's HDR
    // scene target; the final tone-map pass owns display encoding.
    return checker_texture.Sample(
        checker_sampler,
        input.texture_coordinates);
}
