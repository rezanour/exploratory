#pragma once

#include "DeferredPasses11.h"

struct GeoMesh;

// D3D11 based deferred renderer
class DeferredRenderer11
{
public:
    static std::unique_ptr<DeferredRenderer11> Create(HWND window);
    ~DeferredRenderer11();

    bool AddMesh(const std::shared_ptr<GeoMesh>& mesh);

    bool Render(FXMMATRIX view, FXMMATRIX projection, bool vsync);

private:
    DeferredRenderer11();
    DeferredRenderer11(const DeferredRenderer11&);
    DeferredRenderer11& operator= (const DeferredRenderer11&);

private:
    // Initialization
    bool Initialize(HWND window);
    bool CreateStateObjects();
    bool CreateGBuffer();

private:
    ComPtr<IDXGISwapChain1>         SwapChain;
    ComPtr<ID3D11Device>            Device;
    ComPtr<ID3D11DeviceContext>     Context;
    ComPtr<ID3D11Texture2D>         DepthStencilBuffer;

    // State objects
    ComPtr<ID3D11RasterizerState>   RasterizerState;
    ComPtr<ID3D11BlendState>        AdditiveBlendState;
    ComPtr<ID3D11SamplerState>      LinearWrapSampler;
    ComPtr<ID3D11SamplerState>      PointClampSampler;

    // Fullscreen quad
    ComPtr<ID3D11Buffer>            QuadVB;

    // Geometry pass VS constant buffer
    struct GeometryVSConstants
    {
        XMFLOAT4X4 World;
        XMFLOAT4X4 View;
        XMFLOAT4X4 Projection;
    };
    ComPtr<ID3D11Buffer>            GeometryCB;

    enum class GBufferSlice
    {
        LightAccum = 0,             // BackBuffer. RGB = Light, A = Glow factor for bloom.
        // Initialize RGB during Geo pass with indirect & constant terms
        Normals,                    // TODO: Move to R16G16 FP16, R=X, G=Y, Z=sqrt(1 - X*X - Y*Y). No neg Z,
        // but these are screen space so all point forward anyways
        SpecularRoughness,          // RGB = SpecularColor, A = Roughness
        Color,                      // RGB = Color
        Count,
    };

    ComPtr<ID3D11Texture2D>         GBuffer[(uint32_t)GBufferSlice::Count];

    // Rendering passes
    std::shared_ptr<RenderPass11>   RenderPasses[(uint32_t)PassType::Count];
};
