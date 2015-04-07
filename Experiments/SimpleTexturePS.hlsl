#define USE_LIGHT

struct Vertex
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
};

Texture2D DiffuseMap;
sampler Sampler;

float4 main(Vertex input) : SV_TARGET
{
#if defined(USE_LIGHT)
    static const float3 lightDir1 = normalize(float3(-1.f, -1.f, -1.f));
    static const float3 lightDir2 = normalize(float3(-1.f, -1.f, 1.f));

    float4 diffuse = DiffuseMap.Sample(Sampler, input.TexCoord);

    float nDotL1 = saturate(dot(normalize(input.Normal), -lightDir1));
    float nDotL2 = saturate(dot(normalize(input.Normal), -lightDir2));

    return float4(diffuse.xyz * nDotL1 + diffuse.xyz * nDotL2, 1.f);
#else
    float4 diffuse = DiffuseMap.Sample(Sampler, input.TexCoord);
    return diffuse;
#endif
}