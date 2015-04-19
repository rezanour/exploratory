#pragma once

#include "Geometry.h"

// Types
enum class PassType
{
    Geometry = 0,               // Render Geometry into GBuffer
    DirectLighting,             // Render direct light full screen passes
    PointLighting,              // Render point light spheres
    Count,
};

// RenderPass
class RenderPass11 :
    public std::enable_shared_from_this<RenderPass11>,
    public NonCopyable
{
public:
    virtual ~RenderPass11();

    PassType GetType() const { return Type; }

    void Apply();

protected:
    RenderPass11(const ComPtr<ID3D11Device>& device, PassType type);

    virtual void OnApply() = 0;

    bool InitializeShadersAndInputLayout(
        VertexType vertexType,
        const BYTE* vertexShaderData, size_t vertexShaderSize,
        const BYTE* pixelShaderData, size_t pixelShaderSize);

    void InitPSShaderResource(uint32_t slot, const ComPtr<ID3D11ShaderResourceView>& srv);
    void InitRenderTarget(uint32_t slot, const ComPtr<ID3D11RenderTargetView>& rtv);

protected:
    PassType                        Type;
    ComPtr<ID3D11DeviceContext>     Context;
    ComPtr<ID3D11InputLayout>       InputLayout;
    ComPtr<ID3D11VertexShader>      VertexShader;
    ComPtr<ID3D11PixelShader>       PixelShader;

    // Always nullptr. Used to clear out bindings for clean input->output or output->input transitions
    static ID3D11ShaderResourceView* const  NullSRVs[8];
    static ID3D11RenderTargetView* const    NullRTVs[8];

    // Not using ComPtr so that we can properly pass array of them
    // to the D3D API. Released in dtor.
    ID3D11ShaderResourceView*       PSShaderResources[8];
    ID3D11RenderTargetView*         RenderTargets[8];

    ComPtr<ID3D11DepthStencilView>  DepthStencilView;
    uint32_t                        NumSRVs;
    uint32_t                        NumRTVs;
};

// Geometry pass
class GeometryPass11 : public RenderPass11
{
public:
    static std::shared_ptr<GeometryPass11> Create(
        const ComPtr<ID3D11Device>& device,
        const ComPtr<ID3D11Texture2D>& lightAccum,
        const ComPtr<ID3D11Texture2D>& normals,
        const ComPtr<ID3D11Texture2D>& specularRoughness,
        const ComPtr<ID3D11Texture2D>& color,
        const ComPtr<ID3D11Texture2D>& depth);

private:
    GeometryPass11(const ComPtr<ID3D11Device>& device);

    virtual void OnApply() override;

    bool Initialize(
        const ComPtr<ID3D11Texture2D>& lightAccum,
        const ComPtr<ID3D11Texture2D>& normals,
        const ComPtr<ID3D11Texture2D>& specularRoughness,
        const ComPtr<ID3D11Texture2D>& color,
        const ComPtr<ID3D11Texture2D>& depth);

private:
};
