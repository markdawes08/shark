#include "shared/camera_constants.hlsli"

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
    const float3 unit_normal = normalize(input.normal);
    return float4(unit_normal * 0.5F + 0.5F, 1.0F);
}
