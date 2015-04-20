/*
 * Debug shader to display depth buffer
 */

struct Vertex
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

Texture2D DepthMap : register(t0);
SamplerState LinearWrapSampler : register(s0);

float4 main(Vertex input) : SV_TARGET
{
    float depth = DepthMap.Sample(LinearWrapSampler, input.TexCoord).x;

    // TODO: Convert to linear depth
    return float4(depth, depth, depth, 1.f);
}