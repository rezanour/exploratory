struct Vertex
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TANGENT;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
};

#define MAX_LIGHTS 8  // Must match the definition in TestRenderer

struct Light
{
    float3 Direction;
    float3 Color;
};

cbuffer Constants
{
    Light Lights[MAX_LIGHTS];
    float3 EyePosition;
    int NumLights;
};

Texture2D DiffuseMap;
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

float4 main(Vertex input) : SV_TARGET
{
    float4 albedo = DiffuseMap.Sample(Sampler, input.TexCoord);

    // if alpha tested texture, clip out anything near fully transparent
    // TODO: should likely use alpha to coverage
    clip(albedo.a - 0.1f);

    float3 diffuse = float3(0, 0, 0);
    float3 specular = float3(0, 0, 0);
    float3 N = normalize(input.Normal);
    float3 V = normalize(EyePosition - input.WorldPosition);
    for (int i = 0; i < NumLights; ++i)
    {
        float3 L = normalize(Lights[i].Direction);
        diffuse += albedo.xyz * Lights[i].Color * saturate(dot(N, L));
        specular += ComputeBRDF(L, N, V, Lights[i].Color);
    }

    return float4(diffuse + specular, albedo.a);
}