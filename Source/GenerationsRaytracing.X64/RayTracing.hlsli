#ifndef RAY_TRACING_H
#define RAY_TRACING_H
#include "RootSignature.hlsli"
#include "SharedDefinitions.hlsli"

struct [raypayload] PrimaryRayPayload
{
    float3 dDdx : read(anyhit, closesthit) : write(caller);
    float3 dDdy : read(anyhit, closesthit) : write(caller);
};

struct [raypayload] SecondaryRayPayload
{
    float3 Color  : read(caller)     : write(closesthit, miss);
    float T       : read(caller)     : write(closesthit, miss);
    uint Depth    : read(closesthit) : write(caller);
};

struct [raypayload] ShadowRayPayload
{
    bool Miss : read(caller) : write(caller, miss);
};

float4 GetBlueNoise(uint2 index)
{
    Texture2D texture = ResourceDescriptorHeap[g_BlueNoiseTextureId];
    return texture.Load(int3((index + g_BlueNoiseOffset) % 1024, 0));
}

float4 GetBlueNoise()
{
    return GetBlueNoise(DispatchRaysIndex().xy);
}

float3 GetUniformSample(float2 random)
{
    float cosTheta = random.x;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = 2.0 * PI * random.y;

    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float3 GetCosWeightedSample(float2 random)
{
    float cosTheta = sqrt(random.x);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = 2.0 * PI * random.y;

    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float4 GetPowerCosWeightedSample(float2 random, float specularPower)
{
    float cosTheta = pow(random.x, 1.0 / (specularPower + 1.0));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = 2.0 * PI * random.y;
    float pdf = pow(cosTheta, specularPower);
     
    return float4(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta, pdf);
}

float3 GetPerpendicularVector(float3 u)
{
    float3 a = abs(u);
    uint xm = ((a.x - a.y) < 0 && (a.x - a.z) < 0) ? 1 : 0;
    uint ym = (a.y - a.z) < 0 ? (1 ^ xm) : 0;
    uint zm = 1 ^ (xm | ym);
    return cross(u, float3(xm, ym, zm));
}

float3 TangentToWorld(float3 normal, float3 value)
{
    float3 binormal = GetPerpendicularVector(normal);
    float3 tangent = cross(binormal, normal);
    return NormalizeSafe(value.x * tangent + value.y * binormal + value.z * normal);
}

float TraceGlobalLightShadow(float3 position, float3 direction)
{
    float2 random = GetBlueNoise().xw;
    float radius = sqrt(random.x) * 0.01;
    float angle = random.y * 2.0 * PI;

    float3 sample;
    sample.x = cos(angle) * radius;
    sample.y = sin(angle) * radius;
    sample.z = sqrt(1.0 - saturate(dot(sample.xy, sample.xy)));

    RayDesc ray;

    ray.Origin = position;
    ray.Direction = TangentToWorld(direction, sample);
    ray.TMin = 0.0;
    ray.TMax = FLT_MAX;

    ShadowRayPayload payload = (ShadowRayPayload) 0;

    TraceRay(
        g_BVH,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        1,
        1,
        0,
        1,
        ray,
        payload);

    return payload.Miss ? 1.0 : 0.0;
}

float TraceLocalLightShadow(float3 position, LocalLight localLight)
{
    float3 direction = position - localLight.Position;
    float distance = length(direction);
    direction /= distance;

    RayDesc ray;

    ray.Origin = localLight.Position;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = -0.0001 + distance;

    ShadowRayPayload payload = (ShadowRayPayload) 0;

    TraceRay(
        g_BVH,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
        1,
        1,
        0,
        1,
        ray,
        payload);

    return payload.Miss ? 1.0 : 0.0;
}

float3 TraceSecondaryRay(uint depth, float3 position, float3 direction, uint missShaderIndex)
{
    RayDesc ray;

    ray.Origin = position;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = FLT_MAX;

    SecondaryRayPayload payload = (SecondaryRayPayload) 0;
    payload.Depth = depth;

    TraceRay(
        g_BVH,
        0,
        1,
        1,
        0,
        missShaderIndex,
        ray,
        payload);

    return payload.Color;
}

float3 TraceGI(uint depth, float3 position, float3 normal)
{
    float4 random = GetBlueNoise();
    float3 sampleDirection = GetCosWeightedSample(depth == 0 ? random.xy : random.zw);

    return TraceSecondaryRay(depth, position, TangentToWorld(normal, sampleDirection), 2);
}

float3 TraceReflection(
    uint depth,
    float3 position,
    float3 normal,
    float3 eyeDirection)
{
    return TraceSecondaryRay(depth, position, reflect(-eyeDirection, normal), 3);
}

float3 TraceReflection(
    uint depth,
    float3 position,
    float3 normal,
    float specularPower,
    float3 eyeDirection)
{
    float4 sampleDirection = GetPowerCosWeightedSample(GetBlueNoise().yz, specularPower);
    float3 halfwayDirection = TangentToWorld(normal, sampleDirection.xyz);

    float3 reflection = TraceReflection(depth, position, halfwayDirection, eyeDirection);
    reflection *= pow(saturate(dot(normal, halfwayDirection)), specularPower) / (0.0001 + sampleDirection.w);

    return reflection;
}

float3 TraceRefraction(
    uint depth,
    float3 position,
    float3 normal,
    float3 eyeDirection)
{
    return TraceSecondaryRay(depth, position, refract(-eyeDirection, normal, 0.95), 3);
}

#endif