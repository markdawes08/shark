#include "dependency_probe_values.hlsli"

float4 PSMain() : SV_Target0
{
    return float4(dependency_probe_red, 0.0F, 0.0F, 1.0F);
}
