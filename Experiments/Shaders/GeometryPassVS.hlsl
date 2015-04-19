/*
 * GBuffer Geometry Pass
 */

#include "Common.hlsli"

cbuffer Constants
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

struct VertexOut
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 BiTangent : BITANGENT;
    float2 TexCoord : TEXCOORD;
};

VertexOut main(StandardVertex input)
{
    VertexOut output;

    float4 worldPos = mul(World, float4(input.Position, 1));
    output.Position = mul(Projection, mul(View, worldPos));

    // Assumes orthonormal (simple rotation & scaling)
    float3x3 invTransWorld = (float3x3)World;
    float3x3 toView = mul(View, invTransWorld);
    output.Normal = mul(toView, input.Normal);
    output.Tangent = mul(toView, input.Tangent);
    output.BiTangent = mul(toView, input.BiTangent);

    output.TexCoord = input.TexCoord;

    return output;
}