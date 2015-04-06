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
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
};

VertexOut main(Vertex input)
{
    VertexOut output;
    output.Position = mul(ViewProjection, mul(World, float4(input.Position, 1)));
    output.Normal = mul((float3x3)World, input.Normal);
    output.TexCoord = input.TexCoord;

    return output;
}