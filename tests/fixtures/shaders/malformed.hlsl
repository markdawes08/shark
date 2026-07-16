float4 VSMain(const uint vertex_id : SV_VertexID) : SV_Position
{
    return float4(
        SHARK_EXPECTED_MALFORMED_SHADER_FAILURE,
        vertex_id,
        0.0F,
        1.0F);
}
