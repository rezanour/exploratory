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

RenderTargetOut main(VertexIn input)
{
    RenderTargetOut output;

    // TODO: implement
    output.LightAccum = float4(float3(0.1f, 0.1f, 0.1f), 0.f);
    output.Normal = float4(input.Normal * 0.5 + 0.5, 1.f);
    output.SpecularRoughness = float4(float3(1.f, 1.f, 1.f), 0.5f);
    output.Color = float4(0.6f, 0.6f, 0.6f, 1.f);

    return output;
}
