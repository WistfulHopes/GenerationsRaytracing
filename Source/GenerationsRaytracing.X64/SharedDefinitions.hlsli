#pragma once

#define PI 3.14159265358979323846
#define FLT_MAX asfloat(0x7f7fffff)
#define INF asfloat(0x7f800000)

float4 GetBlueNoise(uint2 index)
{
    Texture2DArray texture = ResourceDescriptorHeap[g_BlueNoiseTextureId];
    return texture.Load(int4((index + g_BlueNoiseOffset.xy) % 64, g_BlueNoiseOffset.z, 0));
}

float GetExposure()
{
    Texture2D texture = ResourceDescriptorHeap[g_AdaptionLuminanceTextureId];
    return g_MiddleGray / (texture.Load(0).x + 0.001);
}

uint InitRand(uint val0, uint val1, uint backoff = 16)
{
    uint v0 = val0, v1 = val1, s0 = 0;

    for (uint n = 0; n < backoff; n++)
    {
        s0 += 0x9e3779b9;
        v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
        v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
    }
    return v0;
}

float NextRand(inout uint s)
{
    s = (1664525u * s + 1013904223u);
    return float(s & 0x00FFFFFF) / float(0x01000000);
}

float3 GetCosWeightedSample(float2 random)
{
    float cosTheta = sqrt(random.x);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = 2.0 * PI * random.y;

    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float4 GetPowerCosWeightedSample(float2 random, float specularGloss)
{
    float cosTheta = pow(random.x, 1.0 / (specularGloss + 1.0));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = 2.0 * PI * random.y;
    float pdf = pow(cosTheta, specularGloss);
     
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

float2 ComputeNdcPosition(float3 position, float4x4 view, float4x4 projection)
{
    float4 projectedPosition = mul(mul(float4(position, 1.0), view), projection);
    return (projectedPosition.xy / projectedPosition.w * float2(0.5, -0.5) + 0.5);
}

float2 ComputePixelPosition(float3 position, float4x4 view, float4x4 projection)
{
    return ComputeNdcPosition(position, view, projection) * DispatchRaysDimensions().xy;
}

float ComputeDepth(float3 position, float4x4 view, float4x4 projection)
{
    float4 projectedPosition = mul(mul(float4(position, 1.0), view), projection);
    return projectedPosition.z / projectedPosition.w;
}

float LinearizeDepth(float depth, float4x4 invProjection)
{
    float4 position = mul(float4(0, 0, depth, 1), invProjection);
    return position.z / position.w;
}