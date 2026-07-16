cbuffer CameraConstants : register(b0)
{
    row_major float4x4 view_projection;
    row_major float4x4 sky_view_projection;
};
