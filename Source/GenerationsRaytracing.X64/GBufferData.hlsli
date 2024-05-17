#pragma once

#include "RootSignature.hlsli"

#define GBUFFER_FLAG_IS_SKY                     (1 << 0)
#define GBUFFER_FLAG_IGNORE_DIFFUSE_LIGHT       (1 << 1)
#define GBUFFER_FLAG_IGNORE_SPECULAR_LIGHT      (1 << 2)
#define GBUFFER_FLAG_IGNORE_SHADOW              (1 << 3)
#define GBUFFER_FLAG_IGNORE_GLOBAL_LIGHT        (1 << 4)
#define GBUFFER_FLAG_IGNORE_LOCAL_LIGHT         (1 << 5)
#define GBUFFER_FLAG_IGNORE_GLOBAL_ILLUMINATION (1 << 6)
#define GBUFFER_FLAG_IGNORE_REFLECTION          (1 << 7)
#define GBUFFER_FLAG_HAS_LAMBERT_ADJUSTMENT     (1 << 8)
#define GBUFFER_FLAG_HALF_LAMBERT               (1 << 9)
#define GBUFFER_FLAG_MUL_BY_SPEC_GLOSS          (1 << 10)
#define GBUFFER_FLAG_IS_MIRROR_REFLECTION       (1 << 11)
#define GBUFFER_FLAG_IS_METALLIC                (1 << 12)
#define GBUFFER_FLAG_IS_WATER                   (1 << 13)
#define GBUFFER_FLAG_REFRACTION_ADD             (1 << 14)
#define GBUFFER_FLAG_REFRACTION_MUL             (1 << 15)
#define GBUFFER_FLAG_REFRACTION_OPACITY         (1 << 16)
#define GBUFFER_FLAG_REFRACTION_OVERLAY         (1 << 17)
#define GBUFFER_FLAG_REFRACTION_ALL             (GBUFFER_FLAG_REFRACTION_ADD | GBUFFER_FLAG_REFRACTION_MUL | GBUFFER_FLAG_REFRACTION_OPACITY | GBUFFER_FLAG_REFRACTION_OVERLAY)
#define GBUFFER_FLAG_IS_ADDITIVE                (1 << 18)

struct GBufferData
{
    float3 Position;
    uint Flags;

    float3 Diffuse;
    float Alpha;

    float3 Specular;
    float3 SpecularTint;
    float SpecularEnvironment;
    float SpecularGloss;
    float SpecularLevel;
    float SpecularFresnel;

    float3 Normal;
    float3 Falloff;
    float3 Emission;
    float3 TransColor;
    
    float Refraction;
    float RefractionOverlay;
    float2 RefractionOffset;
};

GBufferData LoadGBufferData(uint3 index)
{
    float4 gBuffer0 = g_GBuffer0_SRV[index];
    float4 gBuffer1 = g_GBuffer1_SRV[index];
    float4 gBuffer2 = g_GBuffer2_SRV[index];
    float4 gBuffer3 = g_GBuffer3_SRV[index];
    float4 gBuffer4 = g_GBuffer4_SRV[index];
    float4 gBuffer5 = g_GBuffer5_SRV[index];
    float4 gBuffer6 = g_GBuffer6_SRV[index];
    float4 gBuffer7 = g_GBuffer7_SRV[index];
    float4 gBuffer8 = g_GBuffer8_SRV[index];

    GBufferData gBufferData = (GBufferData) 0;

    gBufferData.Position = gBuffer0.xyz;
    gBufferData.Flags = asuint(gBuffer0.w);

    gBufferData.Diffuse = gBuffer1.rgb;
    gBufferData.Alpha = gBuffer1.a;

    gBufferData.Specular = gBuffer2.rgb;
    gBufferData.SpecularTint = gBuffer3.rgb;
    gBufferData.SpecularEnvironment = gBuffer2.w;
    gBufferData.SpecularGloss = gBuffer3.w;
    gBufferData.SpecularLevel = gBuffer4.x;
    gBufferData.SpecularFresnel = gBuffer4.y;

    gBufferData.Normal = gBuffer5.xyz;
    gBufferData.Falloff = gBuffer6.xyz;
    gBufferData.Emission = gBuffer7.xyz;
    
    gBufferData.TransColor = float3(gBuffer5.w, gBuffer6.w, gBuffer7.w);
    
    gBufferData.Refraction = gBuffer4.z;
    gBufferData.RefractionOverlay = gBuffer4.w;
    gBufferData.RefractionOffset = gBuffer8.xy;
    
    float lengthSquared = dot(gBufferData.Normal, gBufferData.Normal);
    gBufferData.Normal = select(lengthSquared > 0.0, gBufferData.Normal * rsqrt(lengthSquared), 0.0);

    return gBufferData;
}

void StoreGBufferData(uint3 index, GBufferData gBufferData)
{
    g_GBuffer0[index] = float4(gBufferData.Position, asfloat(gBufferData.Flags));
    g_GBuffer1[index] = float4(gBufferData.Diffuse, gBufferData.Alpha);
    g_GBuffer2[index] = float4(gBufferData.Specular, gBufferData.SpecularEnvironment);
    g_GBuffer3[index] = float4(gBufferData.SpecularTint, gBufferData.SpecularGloss);
    g_GBuffer4[index] = float4(gBufferData.SpecularLevel, gBufferData.SpecularFresnel, gBufferData.Refraction, gBufferData.RefractionOverlay);
    g_GBuffer5[index] = float4(gBufferData.Normal, gBufferData.TransColor.r);
    g_GBuffer6[index] = float4(gBufferData.Falloff, gBufferData.TransColor.g);
    g_GBuffer7[index] = float4(gBufferData.Emission, gBufferData.TransColor.b);
    g_GBuffer8[index] = float4(gBufferData.RefractionOffset, 0.0, 0.0);
}

#ifndef EXCLUDE_RAYTRACING_DEFINITIONS

#include "GeometryDesc.hlsli"
#include "MaterialData.hlsli"
#include "MaterialFlags.h"
#include "ShaderType.h"
#include "ProceduralEye.hlsli"

float ComputeFresnel(float3 normal)
{
    return pow(1.0 - saturate(dot(normal, -WorldRayDirection())), 5.0);
}

float ComputeFresnel(float3 normal, float2 fresnelParam)
{
    return lerp(fresnelParam.x, 1.0, ComputeFresnel(normal)) * fresnelParam.y;
}

float3 DecodeNormalMap(float4 value)
{
    value.x *= value.w;

    float3 normalMap;
    normalMap.xy = value.xy * 2.0 - 1.0;
    normalMap.z = sqrt(abs(1.0 - dot(normalMap.xy, normalMap.xy)));
    return normalMap;
}

float3 DecodeNormalMap(Vertex vertex, float3 value)
{
    return NormalizeSafe(
        vertex.Tangent * value.x -
        vertex.Binormal * value.y +
        vertex.Normal * value.z);
}

float3 DecodeNormalMap(Vertex vertex, float4 value)
{
    return DecodeNormalMap(vertex, DecodeNormalMap(value));
}

float ComputeFalloff(float3 normal, float3 falloffParam)
{
    return pow(1.0 - saturate(dot(normal, -WorldRayDirection())), falloffParam.z) * falloffParam.y + falloffParam.x;
}

float2 ComputeEnvMapTexCoord(float3 eyeDirection, float3 normal)
{
    float4 C[4];
    C[0] = float4(0.5, 500, 5, 1);
    C[1] = float4(1024, 0, -2, 3);
    C[2] = float4(0.25, 4, 0, 0);
    C[3] = float4(-1, 1, 0, 0.5);
    float4 r3 = eyeDirection.xyzx;
    float4 r4 = normal.xyzx;
    float4 r1, r2, r6; 
    r1.w = dot(r3.yzw, r4.yzw);
    r1.w = r1.w + r1.w;
    r2 = r1.wwww * r4.xyzw + -r3.xyzw;
    r3 = r2.wyzw * C[3].xxyz + C[3].zzzw;
    r6 = r2.xyzw * C[3].yxxz;
    r2 = select(r2.zzzz >= 0, r3.xyzw, r6.xyzw);
    r1.w = r2.z + C[0].w;
    r1.w = 1.0 / r1.w;
    r2.xy = r2.yx * r1.ww + C[0].ww;
    r3.x = r2.y * C[2].x + r2.w;
    r3.y = r2.x * C[0].x;
    return r3.xy;
}

void CreateWaterGBufferData(Vertex vertex, Material material, inout GBufferData gBufferData)
{
    gBufferData.Flags = GBUFFER_FLAG_IGNORE_LOCAL_LIGHT | 
        GBUFFER_FLAG_IS_MIRROR_REFLECTION | GBUFFER_FLAG_IS_WATER;

    float2 offset = material.WaterParam.xy * g_TimeParam.y * 0.08;
    float4 decal = SampleMaterialTexture2D(material.DiffuseTexture, vertex, float2(0.0, offset.x));
    
    gBufferData.Diffuse = decal.rgb * vertex.Color.rgb;
    gBufferData.Alpha = decal.a * vertex.Color.a;

    float3 normal1 = DecodeNormalMap(SampleMaterialTexture2D(material.NormalTexture, vertex, float2(0.0, offset.x)));
    float3 normal2 = DecodeNormalMap(SampleMaterialTexture2D(material.NormalTexture2, vertex, float2(0.0, offset.y)));
    float3 normal = NormalizeSafe((normal1 + normal2) * 0.5);
    
    gBufferData.Normal = NormalizeSafe(DecodeNormalMap(vertex, normal));
   
    gBufferData.SpecularFresnel = 1.0 - abs(dot(gBufferData.Normal, -WorldRayDirection()));
    gBufferData.SpecularFresnel *= gBufferData.SpecularFresnel;
    gBufferData.SpecularFresnel *= gBufferData.SpecularFresnel;
    
    gBufferData.RefractionOffset = normal.xy;
}

float4 ApplyFurParamTransform(float4 value, float furParam)
{
    value = value * 2.0 - 1.0;
    value = exp(-value * furParam);
    value = (1.0 - value) / (1.0 + value);
    float num = exp(-furParam);
    value *= 1.0 + num;
    value /= 1.0 - num;
    return value * 0.5 + 0.5;
}

float ConvertSpecularGlossToRoughness(float specularGloss)
{
    return 1.0 - pow(specularGloss, 0.2) * 0.25;
}

GBufferData CreateGBufferData(Vertex vertex, Material material, uint shaderType, InstanceDesc instanceDesc)
{
    GBufferData gBufferData = (GBufferData) 0;

    gBufferData.Position = vertex.SafeSpawnPoint;
    gBufferData.Diffuse = material.Diffuse.rgb;
    gBufferData.Alpha = material.Diffuse.a * material.Opacity.x;
    gBufferData.Specular = material.Specular.rgb;
    gBufferData.SpecularTint = 1.0;
    gBufferData.SpecularEnvironment = material.LuminanceRange + 1.0;
    gBufferData.SpecularGloss = material.GlossLevel.x * 500.0;
    gBufferData.SpecularLevel = material.GlossLevel.y * 5.0;
    gBufferData.Normal = vertex.Normal;

    switch (shaderType)
    {
        case SHADER_TYPE_BLEND:
            {
                float blendFactor = material.OpacityTexture != 0 ?
                    SampleMaterialTexture2D(material.OpacityTexture, vertex).x : vertex.Color.x;
                
                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                if (material.DiffuseTexture2 != 0)
                    diffuse = lerp(diffuse, SampleMaterialTexture2D(material.DiffuseTexture2, vertex), blendFactor);
                
                gBufferData.Diffuse *= diffuse.rgb;
                gBufferData.Alpha *= diffuse.a;

                if (material.SpecularTexture != 0)
                {
                    float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                    if (material.SpecularTexture2 != 0)
                        specular = lerp(specular, SampleMaterialTexture2D(material.SpecularTexture2, vertex), blendFactor);

                    gBufferData.SpecularTint *= specular.rgb;
                    gBufferData.SpecularEnvironment *= specular.a;
                }

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    if (material.GlossTexture2 != 0)
                        gloss = lerp(gloss, SampleMaterialTexture2D(material.GlossTexture2, vertex).x, blendFactor);

                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularGloss *= gloss;
                }
                else if (material.SpecularTexture == 0)
                {
                    gBufferData.Specular = 0.0;
                }

                if (material.NormalTexture != 0)
                {
                    float4 normal = SampleMaterialTexture2D(material.NormalTexture, vertex);
                    if (material.NormalTexture2 != 0)
                        normal = lerp(normal, SampleMaterialTexture2D(material.NormalTexture2, vertex), blendFactor);

                    gBufferData.Normal = DecodeNormalMap(vertex, normal);
                }
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;
                
                break;
            }

        case SHADER_TYPE_CHR_EYE:
            {
                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                float2 gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).xy;
                float4 normalMap = SampleMaterialTexture2D(material.NormalTexture, vertex);

                float3 highlightPosition = mul(instanceDesc.HeadTransform, float4(material.SonicEyeHighLightPosition, 1.0));
                float3 highlightNormal = DecodeNormalMap(vertex, float4(normalMap.xy, 0.0, 1.0));
                float3 lightDirection = NormalizeSafe(vertex.Position - highlightPosition);
                float3 halfwayDirection = NormalizeSafe(-WorldRayDirection() + lightDirection);

                float highlightSpecular = saturate(dot(highlightNormal, halfwayDirection));
                highlightSpecular = pow(highlightSpecular, max(1.0, min(1024.0, gBufferData.SpecularGloss * gloss.x)));
                highlightSpecular *= gBufferData.SpecularLevel * gloss.x;
                highlightSpecular *= ComputeFresnel(vertex.Normal) * 0.7 + 0.3;

                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a;

                gBufferData.Specular *= gloss.y * vertex.Color.a;
                gBufferData.SpecularEnvironment *= gloss.y * vertex.Color.a;
                gBufferData.SpecularGloss *= gloss.y;    
                gBufferData.SpecularFresnel = ComputeFresnel(DecodeNormalMap(vertex, float4(normalMap.zw, 0.0, 1.0))) * 0.7 + 0.3;
                gBufferData.Emission = highlightSpecular * material.SonicEyeHighLightColor.rgb;

                break;
            }

        case SHADER_TYPE_CHR_EYE_FHL_PROCEDURAL:
        case SHADER_TYPE_CHR_EYE_FHL:
            {
                float3 direction = mul(instanceDesc.HeadTransform, float4(0.0, 0.0, 1.0, 0.0));
                direction = NormalizeSafe(mul(float4(direction, 0.0), g_MtxView).xyz);
                float2 offset = material.TexCoordOffsets[0].xy * 2.0 + direction.xy * float2(-1.0, 1.0);
    
                float2 pupilOffset = -material.ChrEyeFHL1.zw * offset;
                float2 highLightOffset = material.ChrEyeFHL3.xy + material.ChrEyeFHL3.zw * offset;
                float2 catchLightOffset = material.ChrEyeFHL1.xy - offset * float2(
                    offset.x < 0 ? material.ChrEyeFHL2.x : material.ChrEyeFHL2.y, 
                    offset.y < 0 ? material.ChrEyeFHL2.z : material.ChrEyeFHL2.w);
    
                float3 diffuse;
                float pupil;
                float3 highLight;
                float catchLight;
                float4 mask;
    
                if (shaderType == SHADER_TYPE_CHR_EYE_FHL_PROCEDURAL)
                {
                    GenerateProceduralEye(
                        vertex.TexCoords[0],
                        material.IrisColor,
                        vertex.TexCoords[0] + pupilOffset,
                        material.PupilParam.xy,
                        material.PupilParam.z,
                        material.PupilParam.w,
                        vertex.TexCoords[0] + highLightOffset, 
                        material.HighLightColor,
                        vertex.TexCoords[0] + catchLightOffset,
                        diffuse,
                        pupil,
                        highLight,
                        catchLight,
                        mask);
                }
                else
                {
                    diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex).rgb;
                    pupil = SampleMaterialTexture2D(material.DiffuseTexture, vertex, pupilOffset).w;
                    highLight = SampleMaterialTexture2D(material.LevelTexture, vertex, highLightOffset).rgb;
                    catchLight = SampleMaterialTexture2D(material.LevelTexture, vertex, catchLightOffset).w;
                    mask = SampleMaterialTexture2D(material.DisplacementTexture, vertex);
                }
    
                gBufferData.Diffuse *= diffuse * pupil * (1.0 - catchLight);
                gBufferData.Specular *= pupil * mask.b * vertex.Color.w * (1.0 - catchLight);
                gBufferData.SpecularFresnel = ComputeFresnel(vertex.Normal) * 0.7 + 0.3;
                gBufferData.Emission = (highLight * pupil * mask.w * (1.0 - catchLight) + catchLight) / GetExposure();
                break;
            }

        case SHADER_TYPE_CHR_SKIN:
            {
                gBufferData.Flags = GBUFFER_FLAG_HAS_LAMBERT_ADJUSTMENT;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);

                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                gBufferData.SpecularTint *= specular.rgb * vertex.Color.rgb;
                gBufferData.SpecularEnvironment *= specular.a;

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularGloss *= gloss;
                }

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.7 + 0.3;

                gBufferData.Falloff = ComputeFalloff(gBufferData.Normal, material.SonicSkinFalloffParam.xyz) * vertex.Color.rgb;
                if (material.DisplacementTexture != 0)
                    gBufferData.Falloff *= SampleMaterialTexture2D(material.DisplacementTexture, vertex).rgb;

                break;
            }

        case SHADER_TYPE_CHR_SKIN_HALF:
            {
                gBufferData.Flags = GBUFFER_FLAG_HALF_LAMBERT;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                gBufferData.SpecularTint *= specular.rgb * vertex.Color.rgb;
                gBufferData.SpecularEnvironment *= specular.a;
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.7 + 0.3;

                gBufferData.Falloff = ComputeFalloff(gBufferData.Normal, material.SonicSkinFalloffParam.xyz) * vertex.Color.rgb;

                float3 viewNormal = mul(float4(gBufferData.Normal, 0.0), g_MtxView).xyz;
                float4 reflection = SampleMaterialTexture2D(material.ReflectionTexture, viewNormal.xy * float2(0.5, -0.5) + 0.5, 0);
                gBufferData.Diffuse *= reflection.rgb;

                break;
            }

        case SHADER_TYPE_CHR_SKIN_IGNORE:
            {
                gBufferData.Flags = GBUFFER_FLAG_IGNORE_DIFFUSE_LIGHT;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                gBufferData.SpecularTint *= specular.rgb * vertex.Color.rgb;
                gBufferData.SpecularEnvironment *= specular.a;
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.7 + 0.3;

                gBufferData.Falloff = ComputeFalloff(gBufferData.Normal, material.SonicSkinFalloffParam.xyz) * vertex.Color.rgb;

                gBufferData.Emission = material.ChrEmissionParam.rgb;

                if (material.DisplacementTexture != 0)
                    gBufferData.Emission += SampleMaterialTexture2D(material.DisplacementTexture, vertex).rgb;

                gBufferData.Emission *= material.Ambient.rgb * material.ChrEmissionParam.w * vertex.Color.rgb;

                float3 viewNormal = mul(float4(gBufferData.Normal, 0.0), g_MtxView).xyz;
                float4 reflection = SampleMaterialTexture2D(material.ReflectionTexture, viewNormal.xy * float2(0.5, -0.5) + 0.5, 0);

                gBufferData.Diffuse *= reflection.rgb;
                gBufferData.Emission += gBufferData.Diffuse;

                break;
            }

        case SHADER_TYPE_CLOUD:
            {
                gBufferData.Flags = GBUFFER_FLAG_HAS_LAMBERT_ADJUSTMENT;

                gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.7 + 0.3;

                gBufferData.Falloff = ComputeFalloff(gBufferData.Normal, material.SonicSkinFalloffParam.xyz) * vertex.Color.rgb;
                gBufferData.Falloff *= SampleMaterialTexture2D(material.DisplacementTexture, vertex).rgb;

                float3 viewNormal = mul(float4(vertex.Normal, 0.0), g_MtxView).xyz;
                float4 reflection = SampleMaterialTexture2D(material.ReflectionTexture, viewNormal.xy * float2(0.5, -0.5) + 0.5, 0);
                gBufferData.Alpha *= reflection.a * vertex.Color.a;

                break;
            }

#ifdef ENABLE_SYS_ERROR_FALLBACK
        default:
#endif
        case SHADER_TYPE_COMMON:
            {
                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                if (material.OpacityTexture != 0)
                    gBufferData.Alpha *= SampleMaterialTexture2D(material.OpacityTexture, vertex).x;
                
                if (material.SpecularTexture != 0)
                {
                    float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                    gBufferData.SpecularTint *= specular.rgb * vertex.Color.rgb;
                    gBufferData.SpecularEnvironment *= specular.a;
                }

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularGloss *= gloss;
                }
                else if (material.SpecularTexture == 0)
                {
                    gBufferData.Specular = 0.0;
                }

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;
            
                break;
            }

        case SHADER_TYPE_DIM:
            {
                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularGloss *= gloss;
                }

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;

                float3 viewNormal = mul(float4(gBufferData.Normal, 0.0), g_MtxView).xyz;

                gBufferData.Emission = material.Ambient.rgb * vertex.Color.rgb * 
                    SampleMaterialTexture2D(material.ReflectionTexture, viewNormal.xy * float2(0.5, -0.5) + 0.5, 0).rgb;

                break;
            }

        case SHADER_TYPE_DISTORTION:
            {
                gBufferData.Flags = GBUFFER_FLAG_REFRACTION_ADD;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                gBufferData.SpecularTint *= specular.rgb * vertex.Color.rgb;
                gBufferData.SpecularEnvironment *= specular.a;

                gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                if (material.NormalTexture2 != 0)
                    gBufferData.Normal = NormalizeSafe(gBufferData.Normal + DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture2, vertex)));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;

                float3 viewNormal = mul(float4(gBufferData.Normal, 0.0), g_MtxView).xyz;
                gBufferData.RefractionOffset = viewNormal.xy * 0.05;

                break;
            }

        case SHADER_TYPE_DISTORTION_OVERLAY:
            {
                gBufferData.Flags =
                    GBUFFER_FLAG_IGNORE_GLOBAL_LIGHT | GBUFFER_FLAG_IGNORE_LOCAL_LIGHT |
                    GBUFFER_FLAG_IGNORE_GLOBAL_ILLUMINATION | GBUFFER_FLAG_IGNORE_REFLECTION | GBUFFER_FLAG_REFRACTION_OVERLAY;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse = diffuse.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;
                gBufferData.Specular = vertex.Color.rgb;
                gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                if (material.NormalTexture2 != 0)
                    gBufferData.Normal = NormalizeSafe(gBufferData.Normal + DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture2, vertex)));

                float depth = ComputeDepth(vertex.Position, g_MtxView, g_MtxProjection);            
                gBufferData.Refraction = max(0.0, g_Depth[DispatchRaysIndex().xy] - depth) * (material.DistortionParam.y / (1.0 - depth));
            
                float3 viewNormal = mul(float4(gBufferData.Normal, 0.0), g_MtxView).xyz;
                gBufferData.RefractionOffset = viewNormal.xy * material.DistortionParam.w;
                gBufferData.RefractionOverlay = material.DistortionParam.z;

                break;
            }

        case SHADER_TYPE_ENM_EMISSION:
            {
                gBufferData.Flags = GBUFFER_FLAG_HAS_LAMBERT_ADJUSTMENT;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                if (material.SpecularTexture != 0)
                {
                    float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                    gBufferData.SpecularTint *= specular.rgb * vertex.Color.rgb;
                    gBufferData.SpecularEnvironment *= specular.a;
                }
                else
                {
                    gBufferData.Specular = 0.0;
                }
            
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.7 + 0.3;

                gBufferData.Emission = material.ChrEmissionParam.rgb;

                if (material.DisplacementTexture != 0)
                    gBufferData.Emission += SampleMaterialTexture2D(material.DisplacementTexture, vertex).rgb;

                gBufferData.Emission *= material.Ambient.rgb * material.ChrEmissionParam.w * vertex.Color.rgb;

                break;
            }

        case SHADER_TYPE_ENM_GLASS:
            {
                gBufferData.Flags = GBUFFER_FLAG_HAS_LAMBERT_ADJUSTMENT;

                float4 diffuse = 0.0;

                if (material.DiffuseTexture != 0)
                {
                    diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                    gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                }

                if (material.SpecularTexture != 0)
                {
                    float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                    gBufferData.SpecularTint *= specular.rgb * vertex.Color.rgb;
                    gBufferData.SpecularEnvironment *= specular.a;
                }

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.7 + 0.3;

                float3 viewNormal = mul(float4(gBufferData.Normal, 0.0), g_MtxView).xyz;
                float4 reflection = SampleMaterialTexture2D(material.ReflectionTexture, viewNormal.xy * float2(0.5, -0.5) + 0.5, 0);
                gBufferData.Alpha *= saturate(diffuse.a + reflection.a) * vertex.Color.a;

                gBufferData.Emission = material.ChrEmissionParam.rgb;

                if (material.DisplacementTexture != 0)
                    gBufferData.Emission += SampleMaterialTexture2D(material.DisplacementTexture, vertex).rgb;

                gBufferData.Emission *= material.Ambient.rgb * material.ChrEmissionParam.w * vertex.Color.rgb;

                break;
            }

        case SHADER_TYPE_ENM_IGNORE:
            {
                gBufferData.Flags = GBUFFER_FLAG_IGNORE_DIFFUSE_LIGHT;

                if (material.DiffuseTexture != 0)
                {
                    float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                    gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                    gBufferData.Alpha *= diffuse.a * vertex.Color.a;
                }

                if (material.SpecularTexture != 0)
                {
                    float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                    gBufferData.SpecularTint *= specular.rgb * vertex.Color.rgb;
                    gBufferData.SpecularEnvironment *= specular.a;
                }
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.7 + 0.3;

                gBufferData.Emission = material.ChrEmissionParam.rgb;

                if (material.DisplacementTexture != 0)
                    gBufferData.Emission += SampleMaterialTexture2D(material.DisplacementTexture, vertex).rgb;

                gBufferData.Emission *= material.Ambient.rgb * material.ChrEmissionParam.w * vertex.Color.rgb;
                gBufferData.Emission += gBufferData.Diffuse;

                break;
            }

        case SHADER_TYPE_FADE_OUT_NORMAL:
            {
                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb;
                gBufferData.Alpha *= diffuse.a;

                float3 normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));
                gBufferData.Normal = NormalizeSafe(lerp(normal, gBufferData.Normal, vertex.Color.x));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;

                break;
            }

        case SHADER_TYPE_FALLOFF:
            {
                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularGloss *= gloss;
                }
                else
                {
                    gBufferData.Specular = 0.0;
                }
                
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;

                float3 viewNormal = mul(float4(vertex.Normal, 0.0), g_MtxView).xyz;
                gBufferData.Emission = SampleMaterialTexture2D(material.DisplacementTexture, viewNormal.xy * float2(0.5, -0.5) + 0.5, 0).rgb;

                break;
            }

        case SHADER_TYPE_FALLOFF_V:
            {
                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb;
                gBufferData.Alpha *= diffuse.a;

                if (material.NormalTexture != 0)
                {
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                    if (material.NormalTexture2 != 0)
                    {
                        float3 normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture2, vertex));
                        gBufferData.Normal = NormalizeSafe(gBufferData.Normal + normal);
                    }
                }

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularGloss *= gloss;
                }
                else
                {
                    gBufferData.Specular = 0.0;
                }
                
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;

                float fresnel = 1.0 - saturate(dot(-WorldRayDirection(), vertex.Normal));
                fresnel *= fresnel;
                gBufferData.Emission = fresnel * vertex.Color.rgb;

                break;
            }

        case SHADER_TYPE_FUR:
            {
                gBufferData.Flags = GBUFFER_FLAG_HAS_LAMBERT_ADJUSTMENT;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                gBufferData.SpecularTint *= specular.rgb * vertex.Color.rgb;
                gBufferData.SpecularEnvironment *= specular.a;

                gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture2, vertex));
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.7 + 0.3;

                gBufferData.Falloff = ComputeFalloff(gBufferData.Normal, material.SonicSkinFalloffParam.xyz) * vertex.Color.rgb;
                gBufferData.Falloff *= SampleMaterialTexture2D(material.DisplacementTexture, vertex).rgb; 
                
                if (vertex.Flags & VERTEX_FLAG_MIPMAP)
                {
                    float2 flow = SampleMaterialTexture2D(material.NormalTexture, vertex).xy;
                    flow += 1.0 / 510.0;
                    flow = flow * 2.0 - 1.0;
                    flow *= 0.01 * material.FurParam.x;

                    float3 furColor = 1.0;
                    float furAlpha = 0.0;
                    float2 offset = 0.0;

                    for (uint i = 0; i < (uint) material.FurParam.z; i++)
                    {
                        float4 fur = SampleMaterialTexture2D(material.DiffuseTexture2, vertex, offset, material.FurParam.y);

                        float factor = (float) i / material.FurParam.z;
                        fur.rgb *= (1.0 - material.FurParam2.z) * factor + material.FurParam2.z;
                        fur.rgb *= fur.w * material.FurParam2.y;

                        furColor *= 1.0 - fur.w * material.FurParam2.y;
                        furColor += fur.rgb;
                        furAlpha += fur.w;

                        offset += flow;
                    }

                    furColor = ApplyFurParamTransform(float4(furColor, 0.0), material.FurParam.w).rgb;
                    furAlpha = ApplyFurParamTransform(float4(furAlpha / material.FurParam.z, 0.0, 0.0, 0.0), material.FurParam2.w).x;

                    // Convert to sRGB, the logic above is from Frontiers, meaning it was in linear space.
                    furColor = pow(saturate(furColor), 1.0 / 2.2);

                    gBufferData.Diffuse *= furColor;
                    gBufferData.SpecularTint *= furColor;
                    gBufferData.SpecularGloss *= furAlpha;
                    gBufferData.Falloff *= furColor;

                    // Divide by last mip map to neutralize the color.
                    Texture2D furTexture = ResourceDescriptorHeap[
                        NonUniformResourceIndex(material.DiffuseTexture2 & 0xFFFFF)];
                
                    furColor = furTexture.Load(int3(0, 0, 8)).rgb;
                    furColor = 1.0 / pow(saturate(furColor), 1.0 / 2.2);

                    gBufferData.Diffuse = saturate(gBufferData.Diffuse * furColor.rgb);
                    gBufferData.SpecularTint = saturate(gBufferData.SpecularTint * furColor.rgb);
                    gBufferData.Falloff *= furColor.rgb;
                }

                break;
            }

        case SHADER_TYPE_GLASS:
            {
                gBufferData.Flags = GBUFFER_FLAG_IS_MIRROR_REFLECTION;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb;
                gBufferData.Alpha *= diffuse.a;

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal, material.FresnelParam.xy);

                if (material.SpecularTexture != 0)
                {
                    float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                    gBufferData.SpecularTint *= specular.rgb;
                    gBufferData.SpecularEnvironment *= specular.a;
                }

                if (material.GlossTexture != 0)
                {
                    float4 gloss = SampleMaterialTexture2D(material.GlossTexture, vertex);
                    if (material.SpecularTexture != 0)
                    {
                        gBufferData.Specular *= gloss.x;
                        gBufferData.SpecularEnvironment *= gloss.x;
                        gBufferData.SpecularLevel *= gloss.x;
                    }
                    else
                    {
                        gBufferData.SpecularTint *= gloss.rgb;
                        gBufferData.SpecularEnvironment *= gloss.w;
                    }
                }
                else if (material.SpecularTexture == 0)
                {
                    gBufferData.Specular = 0.0;
                }

                float3 visibilityFactor = gBufferData.SpecularTint * gBufferData.SpecularEnvironment * gBufferData.SpecularFresnel * 0.5;
                gBufferData.Alpha = sqrt(max(gBufferData.Alpha * gBufferData.Alpha, dot(visibilityFactor, visibilityFactor)));

                break;
            }

        case SHADER_TYPE_ICE:
            {
                gBufferData.Flags = GBUFFER_FLAG_IS_MIRROR_REFLECTION;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularFresnel *= gloss;
                }

                break;
            }

        case SHADER_TYPE_IGNORE_LIGHT:
            {
                gBufferData.Flags =
                    GBUFFER_FLAG_IGNORE_GLOBAL_LIGHT | GBUFFER_FLAG_IGNORE_LOCAL_LIGHT |
                    GBUFFER_FLAG_IGNORE_GLOBAL_ILLUMINATION | GBUFFER_FLAG_IGNORE_REFLECTION;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);

                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                if (material.OpacityTexture != 0)
                    gBufferData.Alpha *= SampleMaterialTexture2D(material.OpacityTexture, vertex).x;

                if (material.DisplacementTexture != 0)
                {
                    gBufferData.Emission = SampleMaterialTexture2D(material.DisplacementTexture, vertex).rgb;
                    gBufferData.Emission += material.EmissionParam.rgb;
                    gBufferData.Emission *= material.Ambient.rgb * material.EmissionParam.w;
                }
                gBufferData.Emission += gBufferData.Diffuse;
                gBufferData.Diffuse = 0.0;

                break;
            }
        
        case SHADER_TYPE_IGNORE_LIGHT_TWICE:
            {
                gBufferData.Flags =
                    GBUFFER_FLAG_IGNORE_GLOBAL_LIGHT | GBUFFER_FLAG_IGNORE_LOCAL_LIGHT |
                    GBUFFER_FLAG_IGNORE_GLOBAL_ILLUMINATION | GBUFFER_FLAG_IGNORE_REFLECTION;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);

                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb * 2.0;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                gBufferData.Emission = gBufferData.Diffuse;
                gBufferData.Diffuse = 0.0;

                break;
            }

        case SHADER_TYPE_INDIRECT:
            {
                float4 offset = SampleMaterialTexture2D(material.DisplacementTexture, vertex);
                offset.xy = (offset.wx * 2.0 - 1.0) * material.OffsetParam.xy;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex, offset.xy);
                float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex, offset.xy).x;

                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                gBufferData.Specular *= gloss;
                gBufferData.SpecularEnvironment *= gloss;
                gBufferData.SpecularGloss *= gloss;

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex, offset.xy));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;

                break;
            }

        case SHADER_TYPE_INDIRECT_NO_LIGHT:
            {
                gBufferData.Flags = GBUFFER_FLAG_IGNORE_GLOBAL_LIGHT | GBUFFER_FLAG_IGNORE_REFLECTION;

                float4 offset = SampleMaterialTexture2D(material.DisplacementTexture, vertex);
                offset.xy = (offset.wx * 2.0 - 1.0) * material.OffsetParam.xy * 2.0;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex, offset.xy);
                float4 emission = SampleMaterialTexture2D(material.DisplacementTexture2, vertex, offset.xy); 

                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                gBufferData.Emission = gBufferData.Diffuse;
                gBufferData.Emission += emission.rgb * vertex.Color.rgb;

                break;
            }

        case SHADER_TYPE_INDIRECT_V:
            {
                float4 offset = SampleMaterialTexture2D(material.DisplacementTexture, vertex);

                offset.xy = (offset.wx * 2.0 - 1.0) * material.OffsetParam.xy * vertex.Color.w;
                offset.xy *= SampleMaterialTexture2D(material.OpacityTexture, vertex).x;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex, offset.xy);
                float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex, offset.xy).x;

                gBufferData.Diffuse *= diffuse.rgb;
                gBufferData.Alpha *= diffuse.a;

                gBufferData.Specular *= gloss;
                gBufferData.SpecularEnvironment *= gloss;
                gBufferData.SpecularGloss *= gloss;

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex, offset.xy));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;

                gBufferData.Emission = vertex.Color.rgb;

                break;
            }

        case SHADER_TYPE_LUMINESCENCE:
            {
                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularGloss *= gloss;
                }
                else
                {
                    gBufferData.Specular = 0.0;
                }
                
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;

                gBufferData.Emission = material.DisplacementTexture != 0 ? 
                    SampleMaterialTexture2D(material.DisplacementTexture, vertex).rgb : material.Emissive.rgb;

                gBufferData.Emission *= material.Ambient.rgb;
                break;
            }

        case SHADER_TYPE_LUMINESCENCE_V:
            {
                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                if (material.DiffuseTexture2 != 0)
                    diffuse = lerp(diffuse, SampleMaterialTexture2D(material.DiffuseTexture2, vertex), vertex.Color.w);
                
                gBufferData.Diffuse *= diffuse.rgb;
                gBufferData.Alpha *= diffuse.a;

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    if (material.GlossTexture2 != 0)
                        gloss = lerp(gloss, SampleMaterialTexture2D(material.GlossTexture2, vertex).x, vertex.Color.w);

                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularGloss *= gloss;
                }
                else
                {
                    gBufferData.Specular = 0.0;
                }
                
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;

                gBufferData.Emission = vertex.Color.rgb * material.Ambient.rgb;

                if (material.DisplacementTexture != 0)
                    gBufferData.Emission *= SampleMaterialTexture2D(material.DisplacementTexture, vertex).rgb;

                break;
            }

        case SHADER_TYPE_METAL:
            {
                gBufferData.Flags = GBUFFER_FLAG_IGNORE_DIFFUSE_LIGHT | GBUFFER_FLAG_IS_METALLIC;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);

                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                gBufferData.SpecularTint *= specular.rgb * vertex.Color.rgb;
                gBufferData.SpecularEnvironment *= specular.a;

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularGloss *= gloss;
                }

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal);

                break;
            }

        case SHADER_TYPE_MIRROR:
            {
                gBufferData.Flags = GBUFFER_FLAG_IS_MIRROR_REFLECTION;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;

                gBufferData.Specular = 0.0;
                gBufferData.SpecularFresnel = ComputeFresnel(vertex.Normal, material.FresnelParam.xy);

                break;
            }

        case SHADER_TYPE_RING:
            {
                gBufferData.Flags = GBUFFER_FLAG_MUL_BY_SPEC_GLOSS;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb;
                gBufferData.Alpha *= diffuse.a;

                float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                gBufferData.SpecularTint *= specular.rgb;
                gBufferData.SpecularEnvironment *= specular.a;
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.7 + 0.3;

                float4 reflection = SampleMaterialTexture2D(material.ReflectionTexture,
                    ComputeEnvMapTexCoord(-WorldRayDirection(), gBufferData.Normal), 0);

                gBufferData.Emission = reflection.rgb * material.LuminanceRange.x * reflection.a + reflection.rgb;
                gBufferData.Emission *= specular.rgb * gBufferData.SpecularFresnel;

                break;
            }

        case SHADER_TYPE_SHOE:
            {
                gBufferData.Flags = GBUFFER_FLAG_HAS_LAMBERT_ADJUSTMENT;

                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);

                gBufferData.Diffuse *= diffuse.rgb * vertex.Color.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;
                gBufferData.SpecularTint *= specular.rgb * vertex.Color.rgb;
                gBufferData.SpecularEnvironment *= specular.a;

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularGloss *= gloss;
                }

                gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));
                gBufferData.SpecularLevel *= ComputeFresnel(gBufferData.Normal) * 0.7 + 0.3;
                gBufferData.SpecularFresnel = 1.0;

                float fresnel = saturate(dot(gBufferData.Normal, -WorldRayDirection()));
                gBufferData.Diffuse *= fresnel * 0.8 + 0.2;

                break;
            }

        case SHADER_TYPE_TIME_EATER:
            {
                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                float4 specular = SampleMaterialTexture2D(material.SpecularTexture, vertex);
                float blend = SampleMaterialTexture2D(material.OpacityTexture, vertex).x;
                float4 normal = SampleMaterialTexture2D(material.NormalTexture, vertex);
                float4 normal2 = SampleMaterialTexture2D(material.NormalTexture2, vertex);

                gBufferData.Diffuse *= diffuse.rgb;
                gBufferData.Alpha *= diffuse.a * vertex.Color.a;
                gBufferData.SpecularTint *= specular.rgb;
                gBufferData.SpecularEnvironment *= specular.a;
                gBufferData.Normal = NormalizeSafe(DecodeNormalMap(vertex, normal) + DecodeNormalMap(vertex, normal2));
                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.7 + 0.3;
                gBufferData.Falloff = ComputeFalloff(gBufferData.Normal, material.SonicSkinFalloffParam);

                // TODO: Refraction

                break;
            }

        case SHADER_TYPE_TRANS_THIN:
            {
                float4 diffuse = SampleMaterialTexture2D(material.DiffuseTexture, vertex);
                gBufferData.Diffuse *= diffuse.rgb;
                gBufferData.Alpha *= diffuse.a;

                if (material.GlossTexture != 0)
                {
                    float gloss = SampleMaterialTexture2D(material.GlossTexture, vertex).x;
                    gBufferData.Specular *= gloss;
                    gBufferData.SpecularEnvironment *= gloss;
                    gBufferData.SpecularGloss *= gloss;
                }

                if (material.NormalTexture != 0)
                    gBufferData.Normal = DecodeNormalMap(vertex, SampleMaterialTexture2D(material.NormalTexture, vertex));

                gBufferData.SpecularFresnel = ComputeFresnel(gBufferData.Normal) * 0.6 + 0.4;

                gBufferData.TransColor = gBufferData.Diffuse * material.TransColorMask.rgb;

                break;
            }

        case SHADER_TYPE_WATER_ADD:
            {
                CreateWaterGBufferData(vertex, material, gBufferData);
                gBufferData.Flags |= GBUFFER_FLAG_IGNORE_DIFFUSE_LIGHT | GBUFFER_FLAG_REFRACTION_ADD;

                if (material.Flags & MATERIAL_FLAG_SOFT_EDGE)
                {
                    float3 viewPosition = mul(float4(vertex.Position, 0.0), g_MtxView).xyz;
                    gBufferData.Alpha *= saturate(viewPosition.z - LinearizeDepth(g_Depth[DispatchRaysIndex().xy], g_MtxInvProjection));
                }

                gBufferData.Refraction = 1.0;
                gBufferData.RefractionOffset *= 0.05;
                break;
            }

        case SHADER_TYPE_WATER_MUL:
            {
                CreateWaterGBufferData(vertex, material, gBufferData);
                gBufferData.Flags |= GBUFFER_FLAG_IGNORE_DIFFUSE_LIGHT | GBUFFER_FLAG_REFRACTION_MUL;

                if (material.Flags & MATERIAL_FLAG_SOFT_EDGE)
                {
                    float3 viewPosition = mul(float4(vertex.Position, 0.0), g_MtxView).xyz;
                    gBufferData.Alpha *= saturate(viewPosition.z - LinearizeDepth(g_Depth[DispatchRaysIndex().xy], g_MtxInvProjection));
                }

                gBufferData.RefractionOffset *= 0.05;
                break;
            }

        case SHADER_TYPE_WATER_OPACITY:
            {
                CreateWaterGBufferData(vertex, material, gBufferData);
                gBufferData.Flags |= GBUFFER_FLAG_REFRACTION_OPACITY;
                gBufferData.Refraction = gBufferData.Alpha;
            
                if (material.Flags & MATERIAL_FLAG_SOFT_EDGE)
                {
                    float3 viewPosition = mul(float4(vertex.Position, 0.0), g_MtxView).xyz;
                    gBufferData.Alpha = saturate((viewPosition.z - LinearizeDepth(g_Depth[DispatchRaysIndex().xy], g_MtxInvProjection)) / material.WaterParam.w);
                }

                gBufferData.RefractionOffset *= 0.05 + material.WaterParam.z;
                break;
            }
#ifndef ENABLE_SYS_ERROR_FALLBACK
        default:
            {
                gBufferData.Flags = 
                    GBUFFER_FLAG_IGNORE_GLOBAL_LIGHT | GBUFFER_FLAG_IGNORE_LOCAL_LIGHT |
                    GBUFFER_FLAG_IGNORE_GLOBAL_ILLUMINATION | GBUFFER_FLAG_IGNORE_REFLECTION;

                gBufferData.Diffuse = 0.0;
                gBufferData.Emission = float3(1.0, 0.0, 0.0);
                break;
            }
#endif
    }

    if (material.Flags & MATERIAL_FLAG_ADDITIVE)
        gBufferData.Flags |= GBUFFER_FLAG_IS_ADDITIVE;

    if (material.Flags & MATERIAL_FLAG_REFLECTION)
        gBufferData.Flags |= GBUFFER_FLAG_IS_MIRROR_REFLECTION;

    float playableParam = saturate(64.0 * (ComputeNdcPosition(vertex.Position, g_MtxView, g_MtxProjection).y - instanceDesc.PlayableParam));
    playableParam *= saturate((instanceDesc.ChrPlayableMenuParam - vertex.Position.y + 0.05) * 10);
    gBufferData.Diffuse = lerp(1.0, gBufferData.Diffuse, playableParam);
    gBufferData.Emission *= playableParam;

    if (material.Flags & MATERIAL_FLAG_VIEW_Z_ALPHA_FADE)
        gBufferData.Alpha *= 1.0 - saturate((RayTCurrent() - g_ViewZAlphaFade.y) * g_ViewZAlphaFade.x);

    gBufferData.SpecularGloss = clamp(gBufferData.SpecularGloss, 1.0, 1024.0);

    bool diffuseMask = all(gBufferData.Diffuse == 0.0);

    if (diffuseMask)
        gBufferData.Flags |= GBUFFER_FLAG_IGNORE_DIFFUSE_LIGHT | GBUFFER_FLAG_IGNORE_GLOBAL_ILLUMINATION;

    bool specularMask = (or(all(gBufferData.Specular == 0.0), all(gBufferData.SpecularTint == 0.0)) || gBufferData.SpecularLevel == 0.0 ||
        gBufferData.SpecularFresnel == 0.0) && !(gBufferData.Flags & GBUFFER_FLAG_IS_MIRROR_REFLECTION);

    if (specularMask)
        gBufferData.Flags |= GBUFFER_FLAG_IGNORE_SPECULAR_LIGHT | GBUFFER_FLAG_IGNORE_REFLECTION;

    diffuseMask |= (gBufferData.Flags & GBUFFER_FLAG_IGNORE_DIFFUSE_LIGHT) != 0;
    specularMask |= (gBufferData.Flags & GBUFFER_FLAG_IGNORE_SPECULAR_LIGHT) != 0;

    if (diffuseMask && specularMask)
        gBufferData.Flags |= GBUFFER_FLAG_IGNORE_GLOBAL_LIGHT | GBUFFER_FLAG_IGNORE_LOCAL_LIGHT;

    if ((material.Flags & MATERIAL_FLAG_NO_SHADOW) || (gBufferData.Flags & GBUFFER_FLAG_IGNORE_GLOBAL_LIGHT))
        gBufferData.Flags |= GBUFFER_FLAG_IGNORE_SHADOW;

    return gBufferData;
}

#endif