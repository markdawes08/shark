#ifndef SHARK_DAYLIGHT_HLSLI
#define SHARK_DAYLIGHT_HLSLI

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

float3 evaluate_daylight_sky_linear(float3 sample_direction)
{
    const float3 unit_direction = normalize(sample_direction);
    const float up = saturate(unit_direction.y);
    const float down = saturate(-unit_direction.y);

    const float3 upper_sky = lerp(
        horizon_color,
        zenith_color,
        pow(up, sky_gradient_exponent));
    const float3 lower_sky = lerp(
        horizon_color,
        nadir_color,
        pow(down, sky_gradient_exponent));
    float3 sky = unit_direction.y >= 0.0F
        ? upper_sky
        : lower_sky;

    const float sun_cosine = dot(
        unit_direction,
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
    sky += sun_color *
        (0.16F * halo * (1.0F - disk) * sun_intensity);

    const float3 sun_disk = saturate(
        sun_color * sun_intensity);
    return saturate(lerp(sky, sun_disk, disk));
}

float3 evaluate_daylight_illumination(float3 unit_normal)
{
    const float3 ground_ambient =
        float3(0.14F, 0.12F, 0.10F);
    const float hemisphere = saturate(
        unit_normal.y * 0.5F + 0.5F);
    const float3 ambient = lerp(
        ground_ambient,
        sky_ambient_color,
        hemisphere) * ambient_strength;
    const float diffuse = saturate(dot(
        unit_normal,
        direction_to_sun));
    const float3 sunlight =
        sun_color * (1.25F * sun_intensity * diffuse);
    return ambient + sunlight;
}

#endif
