#include "shared/triangle_data.hlsli"

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOutput VSMain(const uint vertex_id : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(triangle_positions[vertex_id], 0.5F, 1.0F);
    output.color = triangle_colors[vertex_id];
    return output;
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    return float4(input.color, 1.0F);
}
