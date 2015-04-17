/*
 * Common shader types, structures, and functions.
 */

// Supported defines
// USE_IMPLICIT_VIS - Uses a cheaper BRDF visibility approximation.


//*****************************************************************************
// Constants
//*****************************************************************************
static const float PI = 3.14159265359f;
static const float TwoPI = PI * 2.f;
static const float PIOver2 = PI * 0.5f;
static const float OneOverPI = 1.f / PI;


//*****************************************************************************
// Vertex layout of most of our assets
//*****************************************************************************
struct StandardVertex
{
    float3 Position : POSITION0;
    float3 Normal : NORMAL0;
    float3 Tangent : TANGENT0;
    float3 BiTangent : BITANGENT0;
    float2 TexCoord : TEXCOORD0;
};


//*****************************************************************************
// BRDF Normal Distribution Functions (NDFs)
//*****************************************************************************
float D_GGX(float Roughness, float nDotH)
{
    float a = Roughness * Roughness;
    float a2 = a * a;
    float denom = nDotH * nDotH * (a2 - 1) + 1;
    return a2 / (PI * denom * denom);
}

//*****************************************************************************
// BRDF Visibility Functions
// Visibility functions are the combination of both the standard BRDF Geometric 
// function + cancelling terms in the denominator. That is, for the simple 
// BRDF form of (D * G * F) / (Cancellation), Vis = G/Cancellation.
//*****************************************************************************
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

//*****************************************************************************
// BRDF Fresnel Functions
//*****************************************************************************
float3 F_Schlick(float3 specularColor, float hDotL)
{
    return specularColor + (1 - specularColor) * pow(1 - hDotL, 5);
}

//*****************************************************************************
// Compute BRDF using selected (via #define) Functions.
//*****************************************************************************
// L is Light direction (pointing away from surface).
// N is surface Normal
// V is the View direction (pointing away from surface).
// Roughness is a material property between 0 (ultra shiny) to 1 (completely matte).
// SpecularColor is a spectral material property. Sometimes referred to as F0 in
//    certain publications. Normally it's a scaled variation of grey, but some metals
//    such as copper, cobalt, etc... actually affect color in the specular reflection.
float3 ComputeBRDF(float3 L, float3 N, float3 V, float roughness, float3 specularColor)
{
    float3 H = normalize(L + V);
    float nDotH = saturate(dot(N, H));
    float nDotL = saturate(dot(N, L));
    float nDotV = saturate(dot(N, V));
    float hDotL = saturate(dot(H, L));

#if defined (USE_IMPLICIT_VIS)
    return  0.25f *
        D_GGX(roughness, nDotH) *
        Vis_Implicit(nDotL, nDotV) * 
        F_Schlick(specularColor, hDotL);
#else
    float vDotH = saturate(dot(V, H));
    return  0.25f *
        D_GGX(roughness, nDotH) *
        Vis_CookTorrance(nDotH, nDotL, nDotV, vDotH) *
        F_Schlick(specularColor, hDotL);
#endif
}


//*****************************************************************************
// Calculate attenuation values for point lights
//*****************************************************************************
float Att_InvSq(float distance, float radius)
{
    float denom = (distance / radius) + 1;
    return 1.f / (denom * denom);
}

//*****************************************************************************
// The following set of functions are related to using Derivative Maps
//*****************************************************************************

//*****************************************************************************
// Project the surface height gradient (dhdx, dhdy) onto the surface (n, dpdx, dpdy)
//*****************************************************************************
float3 Deriv_CalcSurfaceGrad(float3 N, float3 dpdx, float3 dpdy, float dhdx, float dhdy)
{
    float3 r1 = cross(dpdy, N);
    float3 r2 = cross(N, dpdx);

    return (r1 * dhdx + r2 * dhdy) / dot(dpdx, r1);
}

//*****************************************************************************
// Move the normal away from the surface normal in the opposite surface
// gradient direction
//*****************************************************************************
float3 Deriv_PerturbNormal(float3 N, float3 dpdx, float3 dpdy, float dhdx, float dhdy)
{
    return normalize(N - Deriv_CalcSurfaceGrad(N, dpdx, dpdy, dhdx, dhdy));
}

//*****************************************************************************
// Use chain rule to map partial uv derivative to height derivative
//*****************************************************************************
float Deriv_ApplyChainRule(float dhdu, float dhdv, float dud_, float dvd_)
{
    return dhdu * dud_ + dhdv * dvd_;
}

//*****************************************************************************
// Compute new surface normal using interpolated vertex attributes and
// Derivative map inputs (height derivatives with respect to U and V)
//*****************************************************************************
float3 Deriv_ComputeNormal(
    float3 worldPos, float3 N, float2 texCoord,
    uint2 derivativeMapSizeInTexels, float2 derivativeSample)
{
    float3 dpdx = ddx_fine(worldPos);
    float3 dpdy = ddy_fine(worldPos);

    uint width = derivativeMapSizeInTexels.x;
    uint height = derivativeMapSizeInTexels.y;
    float dhdx = Deriv_ApplyChainRule(derivativeSample.x, derivativeSample.y, ddx_fine(texCoord.x * width), ddx_fine(texCoord.y * height));
    float dhdy = Deriv_ApplyChainRule(derivativeSample.x, derivativeSample.y, ddy_fine(texCoord.x * width), ddy_fine(texCoord.y * height));

    return Deriv_PerturbNormal(N, dpdx, dpdy, dhdx, dhdy);
}
