/*
 * Currently a test PS. Does a few random things...
 */

#include "Common.hlsli"

// REVIEW: formalize into Common.hlsli?
struct PixelShaderInput
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD1;
};

#define MAX_LIGHTS 8  // Must match the definition in TestRenderer

struct Light
{
    float3 Direction;
    float3 Color;
};

struct PointLight
{
    float3 Position;
    float3 Color;
    float Radius;
};

cbuffer Constants
{
    Light Lights[MAX_LIGHTS];
    PointLight PointLights[MAX_LIGHTS];
    float3 EyePosition;
    int NumLights;
    int NumPointLights;
};

Texture2D DiffuseMap;
Texture2D DerivativeMap;
Texture2D SpecularMap;
sampler Sampler;

float4 main(PixelShaderInput input) : SV_TARGET
{
    // TODO: take in material roughness.
    const float Roughness = 0.6f;   // Medium roughness

    float4 albedo = DiffuseMap.Sample(Sampler, input.TexCoord);

    // if alpha tested texture, clip out anything near fully transparent
    // TODO: should likely use alpha to coverage
    clip(albedo.a - 0.1f);

    // Get texture coordinates, and extract bump derivative map
    float width, height, numLevels;
    DerivativeMap.GetDimensions(0, width, height, numLevels);

    float2 normalSample = DerivativeMap.Sample(Sampler, input.TexCoord).xy * 2 - 1;

    float3 N = Deriv_ComputeNormal(
        input.WorldPosition, input.Normal, input.TexCoord,
        uint2(width, height), normalSample);

    float3 specularColor = SpecularMap.Sample(Sampler, input.TexCoord).xyz;

    int i;
    float3 diffuse = float3(0, 0, 0);
    float3 specular = float3(0, 0, 0);
    float3 V = normalize(EyePosition - input.WorldPosition);

    for (i = 0; i < NumLights; ++i)
    {
        float3 L = normalize(Lights[i].Direction);
        diffuse += albedo.xyz * Lights[i].Color * saturate(dot(N, L));
        specular += ComputeBRDF(L, N, V, Roughness, specularColor);
    }

    for (i = 0; i < NumPointLights; ++i)
    {
        float3 toLight = PointLights[i].Position - input.WorldPosition;
        float3 L = normalize(toLight);
        float dist = length(toLight);
        float atten = Att_InvSq(dist, PointLights[i].Radius);
        float3 lightColorDiffuse = PointLights[i].Color * atten;
        float3 lightColorSpecular = specularColor * atten;
        diffuse += albedo.xyz * lightColorDiffuse * saturate(dot(N, L));
        specular += ComputeBRDF(L, N, V, Roughness, lightColorSpecular);
    }

    return float4(diffuse + specular, albedo.a);
}