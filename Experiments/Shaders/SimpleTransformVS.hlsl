/*
 * Simple transform VS
 */

#include "Common.hlsli"

cbuffer Constants
{
    float4x4 World;
    float4x4 ViewProjection;
};

struct VertexOut
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD1;
};

VertexOut main(StandardVertex input)
{
    VertexOut output;

    float4 worldPos = mul(World, float4(input.Position, 1));

    output.Position = mul(ViewProjection, worldPos);
    output.WorldPosition = worldPos.xyz;

    // Assumes non-translate upperleft 3x3 submatrix of World
    // is orthonormal (simple rotation & scaling)
    output.Normal = mul((float3x3)World, input.Normal);

    output.TexCoord = input.TexCoord;

    return output;
}