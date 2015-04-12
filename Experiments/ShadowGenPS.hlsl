struct Vertex
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD1;
};

Texture2D DiffuseMap;
sampler Sampler;

struct Output
{
    float Depth : SV_TARGET0;
    float3 WorldPosition : SV_TARGET1;
    float3 Normal : SV_TARGET2;
    float3 Flux : SV_TARGET3;
};

Output main(Vertex input)
{
    static const float3 LightColor = float3(0.6f, 0.6f, 0.6f);

    float4 albedo = DiffuseMap.Sample(Sampler, input.TexCoord);

    // if alpha tested texture, clip out anything near fully transparent
    // TODO: should likely use alpha to coverage
    clip(albedo.a - 0.1f);

    Output output = (Output)0;
    output.Depth = input.Position.z;
    output.WorldPosition = input.WorldPosition;
    output.Normal = input.Normal;
    output.Flux = albedo.xyz * saturate(dot(input.Normal, LightColor));

    return output;
}