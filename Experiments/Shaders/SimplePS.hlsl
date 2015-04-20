struct VertexOut
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT0;
    float3 BiTangent : BITANGENT0;
    float2 TexCoord : TEXCOORD1;
};

float4 main(VertexOut input) : SV_TARGET
{
    return float4(input.TexCoord.x, input.TexCoord.y, 0, 1);
}