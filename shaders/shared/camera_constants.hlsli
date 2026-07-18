cbuffer CameraConstants : register(b0)
{
    row_major float4x4 view_projection;
    row_major float4x4 sky_view_projection;

    float3 direction_to_sun;
    float sun_disk_outer_cosine;

    float3 sun_color;
    float sun_disk_inner_cosine;

    float3 zenith_color;
    float sky_gradient_exponent;

    float3 horizon_color;
    float ambient_strength;

    float3 nadir_color;
    float sun_halo_outer_cosine;

    float3 sky_ambient_color;
    float sun_intensity;
};
