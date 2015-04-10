struct Vertex
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 BiTangent : BITANGENT;
    float2 TexCoord : TEXCOORD1;
};

#define MAX_LIGHTS 8  // Must match the definition in TestRenderer

struct Light
{
    float3 Direction;
    float3 Color;
};

struct PointLight
{
    float3 Position;
    float3 Color;
    float Radius;
};

cbuffer Constants
{
    Light Lights[MAX_LIGHTS];
    PointLight PointLights[MAX_LIGHTS];
    float3 EyePosition;
    int NumLights;
    int NumPointLights;
};

Texture2D DiffuseMap;
Texture2D NormalMap;
sampler Sampler;

static const float PI = 3.14156f;

float D_GGX(float Roughness, float nDotH)
{
    float a = Roughness * Roughness;
    float a2 = a * a;
    float denom = nDotH * nDotH * (a2 - 1) + 1;
    return a2 / (PI * denom * denom);
}

// Vis is the G / cancelling terms. Ex G/(nDotV * nDotL)
float Vis_Implicit(float nDotL, float nDotV)
{
    return nDotL * nDotV;
}

float Vis_CookTorrance(float nDotH, float nDotL, float nDotV, float vDotH)
{
    float a = (2 * nDotH * nDotV) / vDotH;
    float b = (2 * nDotH * nDotL) / vDotH;
    return min(1, min(a, b));
}

float3 F_Schlick(float3 specularColor, float hDotL)
{
    return specularColor + (1 - specularColor) * pow(1 - hDotL, 5);
}

// BRDF. Using some of the formulations from Epic's talk at 2013 siggraph 2012 PBS talk
float3 ComputeBRDF(float3 L, float3 N, float3 V, float3 specularColor)
{
    // TODO: take in material roughness.
    const float Roughness = 0.5f;   // Medium roughness

    float3 H = normalize(L + V);
    float nDotH = saturate(dot(N, H));
    float nDotL = saturate(dot(N, L));
    float nDotV = saturate(dot(N, V));
    float vDotH = saturate(dot(V, H));
    float hDotL = saturate(dot(H, L));

    return  0.25f * 
            D_GGX(Roughness, nDotH) * 
            //Vis_Implicit(nDotL, nDotV) * 
            Vis_CookTorrance(nDotH, nDotL, nDotV, vDotH) *
            F_Schlick(specularColor, hDotL);
}

float3 ApplyFalloff(float distance, float radius, float3 lightColor)
{
    float falloff = (radius * radius) / (distance * distance);
    return lightColor * falloff;
}

float4 main(Vertex input) : SV_TARGET
{
    float4 albedo = DiffuseMap.Sample(Sampler, input.TexCoord);

    // if alpha tested texture, clip out anything near fully transparent
    // TODO: should likely use alpha to coverage
    clip(albedo.a - 0.1f);

    float3 normalSample = NormalMap.Sample(Sampler, input.TexCoord).xyz;
    float3 normal = input.Normal;
    if (normalSample.x > 0 || normalSample.y > 0 || normalSample.z > 0)
    {
        // Build tangent space matrix (TODO: Should orthonormalize)
        float3x3 tangentToWorld = float3x3(normalize(input.Tangent), normalize(input.BiTangent), normalize(input.Normal));
        normal = mul(normalSample, tangentToWorld);
    }

    int i;
    float3 diffuse = float3(0, 0, 0);
    float3 specular = float3(0, 0, 0);
    float3 N = normalize(normal);
    float3 V = normalize(EyePosition - input.WorldPosition);
    for (i = 0; i < NumLights; ++i)
    {
        float3 L = normalize(Lights[i].Direction);
        diffuse += albedo.xyz * Lights[i].Color * saturate(dot(N, L));
        specular += ComputeBRDF(L, N, V, Lights[i].Color);
    }

    for (i = 0; i < NumPointLights; ++i)
    {
        float3 toLight = PointLights[i].Position - input.WorldPosition;
        float3 L = normalize(toLight);
        float dist = length(toLight);
        float3 lightColor = ApplyFalloff(dist, PointLights[i].Radius, PointLights[i].Color);
        diffuse += albedo.xyz * lightColor * saturate(dot(N, L));
        specular += ComputeBRDF(L, N, V, lightColor);
    }

    return float4(diffuse + specular, albedo.a);
}