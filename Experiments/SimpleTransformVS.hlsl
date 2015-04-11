cbuffer Constants
{
    float4x4 World;
    float4x4 ViewProjection;
};

struct Vertex
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct VertexOut
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD1;
};

VertexOut main(Vertex input)
{
    VertexOut output;
    float4 worldPos = mul(World, float4(input.Position, 1));
    output.Position = mul(ViewProjection, worldPos);
    output.WorldPosition = worldPos.xyz;
    output.Normal = mul((float3x3)World, input.Normal);
    output.TexCoord = input.TexCoord;

    return output;
}