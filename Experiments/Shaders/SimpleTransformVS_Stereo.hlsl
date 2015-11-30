/*
 * Simple transform VS
 */

#include "Common.hlsli"

cbuffer Constants
{
    float4x4 World;
    float4x4 ViewProjection[2];
};

struct VertexOut
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT0;
    float3 BiTangent : BITANGENT0;
    float2 TexCoord : TEXCOORD1;
    float ClipDistance : SV_ClipDistance0;
};

VertexOut main(StandardVertex input, uint instanceID : SV_InstanceID)
{
    VertexOut output;

    float4 worldPos = mul(World, float4(input.Position, 1));

    uint stereoIdx = instanceID & 0x01;
    output.Position = mul(ViewProjection[stereoIdx], worldPos);
    if (stereoIdx == 0)
    {
        // Left side
        output.ClipDistance = 0.5 * (output.Position.w - output.Position.x);
        output.Position = float4(-output.ClipDistance, output.Position.yzw);
    }
    else
    {
        // Right side
        output.ClipDistance = 0.5 * (output.Position.x + output.Position.w);
        output.Position = float4(output.ClipDistance, output.Position.yzw);
    }

    output.WorldPosition = worldPos.xyz;

    // Assumes non-translate upperleft 3x3 submatrix of World
    // is orthonormal (simple rotation & scaling)
    output.Normal = mul((float3x3)World, input.Normal);
    output.Tangent = mul((float3x3)World, input.Tangent);
    output.BiTangent = mul((float3x3)World, input.BiTangent);

    output.TexCoord = input.TexCoord;

    return output;
}
