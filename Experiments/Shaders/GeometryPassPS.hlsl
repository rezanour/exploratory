/*
* GBuffer Geometry Pass
*/

#include "Common.hlsli"

struct VertexIn
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 BiTangent : BITANGENT;
    float2 TexCoord : TEXCOORD;
};

struct RenderTargetOut
{
    float4 LightAccum : SV_TARGET0;
    float4 Normal : SV_TARGET1;
    float4 SpecularRoughness: SV_TARGET2;
    float4 Color : SV_TARGET3;
};

Texture2D AlbedoMap : register(t0);
Texture2D NormalMap : register(t1);
Texture2D SpecularMap : register(t2);

SamplerState LinearWrapSampler : register(s0);
SamplerState PointClampSampler : register(s1);

RenderTargetOut main(VertexIn input)
{
    RenderTargetOut output;

    // Albedo
    output.Color = AlbedoMap.Sample(LinearWrapSampler, input.TexCoord);

    clip(output.Color.a - 0.1);

    // Normal
    float3 N = NormalMap.Sample(LinearWrapSampler, input.TexCoord).xyz;
    if (any(N))
    {
        N = N * 2 - 1;
        N.y *= -1;
    }
    else
    {
        N = float3(0.5f, 0.5f, 1.f);
    }
    output.Normal.xyz = mul(N, float3x3(normalize(input.Tangent), -normalize(input.BiTangent), normalize(input.Normal)));
    output.Normal.xyz = output.Normal.xyz * 0.5 + 0.5;

    // Specular
    output.SpecularRoughness.xyz = SpecularMap.Sample(LinearWrapSampler, input.TexCoord).xyz;
    if (!any(output.SpecularRoughness.xyz))
    {
        output.SpecularRoughness.xyz = float3(0.5f, 0.5f, 0.5f);
    }
    output.SpecularRoughness.w = 0.7f;

    // TODO: implement
    output.LightAccum = float4(0, 0, 0, 0);// float4(float3(0.1f, 0.1f, 0.1f), 0.f);

    return output;
}
