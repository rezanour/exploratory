/*
 * Deferred directional light pixel shader
 */

#include "Common.hlsli"

// TODO: Move these to common?
#define MAX_DLIGHTS_PER_PASS 4

struct DLight
{
    // NOTE: Light Dir is transformed into camera space already!
    float3 Direction;
    float Pad0;
    float3 Color;
    float Pad1;
};

cbuffer Constants
{
    DLight Lights[MAX_DLIGHTS_PER_PASS];
    uint NumLights;
    float Pad0;
    float2 InvViewportSize;
    float4x4 InvProjection;
};

struct VertexInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

Texture2D DepthTex : register (t0);
Texture2D NormalTex : register (t1);
Texture2D SpecularTex : register (t2);
Texture2D ColorTex : register (t3);

SamplerState LinearWrapSampler : register (s0);
SamplerState PointClampSampler : register (s1);

float3 ViewPositionFromDepth(float2 texCoord, float depth)
{
    float x = texCoord.x * 2 - 1;
    float y = (1 - texCoord.y) * 2 - 1;
    float4 projectedPos = float4(x, y, depth, 1.f);
    float4 viewSpacePos = mul(InvProjection, projectedPos);
    return viewSpacePos.xyz / viewSpacePos.w;
}

float4 main(VertexInput input) : SV_TARGET
{
    // TODO:
    // 1. Reconstruct camera space position from depth
    // 2. Fetch normal and specular info
    // 3. Compute diffuse & evaluate BRDF for each light, summing them
    // 4. Return computed light & Color

    float depth = DepthTex.Sample(LinearWrapSampler, input.TexCoord).x;
    float3 N = NormalTex.Sample(LinearWrapSampler, input.TexCoord).xyz * 2 - 1;
    float3 color = ColorTex.Sample(LinearWrapSampler, input.TexCoord).xyz;
    float4 SR = SpecularTex.Sample(LinearWrapSampler, input.TexCoord);
    float3 specularColor = SR.xyz;
    float roughness = SR.w;

    float3 viewPos = ViewPositionFromDepth(input.TexCoord, depth);

    uint i;
    float3 diffuse = float3(0, 0, 0);
    float3 specular = float3(0, 0, 0);
    float3 V = normalize(-viewPos);

    for (i = 0; i < NumLights; ++i)
    {
        float3 L = Lights[i].Direction;
        diffuse += color.xyz * Lights[i].Color * saturate(dot(N, L));
        specular += ComputeBRDF(L, N, V, roughness, specularColor);
    }

    return float4(diffuse + specular, 1.f);
}