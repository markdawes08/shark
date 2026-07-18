Texture2D<float4> hdr_scene_texture : register(t0);

struct VertexOutput
{
    float4 position : SV_Position;
};

VertexOutput VSMain(const uint vertex_id : SV_VertexID)
{
    VertexOutput output;
    const float2 coordinates = float2(
        float((vertex_id << 1U) & 2U),
        float(vertex_id & 2U));
    output.position = float4(
        coordinates * float2(2.0F, -2.0F) +
            float2(-1.0F, 1.0F),
        0.0F,
        1.0F);
    return output;
}

float3 aces_fitted(const float3 hdr_color)
{
    const float3 color = max(hdr_color, 0.0F);
    const float numerator_scale = 2.51F;
    const float numerator_bias = 0.03F;
    const float denominator_scale = 2.43F;
    const float denominator_bias = 0.59F;
    const float denominator_offset = 0.14F;
    return saturate(
        color *
            (numerator_scale * color + numerator_bias) /
        (color *
            (denominator_scale * color + denominator_bias) +
         denominator_offset));
}

float linear_to_srgb_channel(float value)
{
    value = saturate(value);
    return value <= 0.0031308F
        ? 12.92F * value
        : 1.055F * pow(value, 1.0F / 2.4F) - 0.055F;
}

float3 linear_to_srgb(float3 value)
{
    return float3(
        linear_to_srgb_channel(value.r),
        linear_to_srgb_channel(value.g),
        linear_to_srgb_channel(value.b));
}

float4 PSMain(const VertexOutput input) : SV_Target0
{
    const int2 texel = int2(input.position.xy);
    const float3 hdr_color =
        hdr_scene_texture.Load(int3(texel, 0)).rgb;
    return float4(
        linear_to_srgb(aces_fitted(hdr_color)),
        1.0F);
}
