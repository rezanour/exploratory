#include "Precomp.h"
#include "DeferredPasses11.h"
#include "Shaders/GeometryPassVS.h"
#include "Shaders/GeometryPassPS.h"

ID3D11ShaderResourceView* const  RenderPass11::NullSRVs[8] {};
ID3D11RenderTargetView* const    RenderPass11::NullRTVs[8] {};

RenderPass11::RenderPass11(const ComPtr<ID3D11Device>& device, PassType type)
    : Type(type)
    , NumSRVs(0)
    , NumRTVs(0)
{
    ZeroMemory(PSShaderResources, sizeof(PSShaderResources));
    ZeroMemory(RenderTargets, sizeof(RenderTargets));

    device->GetImmediateContext(&Context);
}

RenderPass11::~RenderPass11()
{
    for (int i = 0; i < _countof(PSShaderResources); ++i)
    {
        if (PSShaderResources[i]) { PSShaderResources[i]->Release(); PSShaderResources[i] = nullptr; }
    }

    for (int i = 0; i < _countof(RenderTargets); ++i)
    {
        if (RenderTargets[i]) { RenderTargets[i]->Release(); RenderTargets[i] = nullptr; }
    }
}

void RenderPass11::Apply()
{
    // Clear inputs & outputs
    Context->OMSetRenderTargets(_countof(NullRTVs), NullRTVs, nullptr);
    Context->PSSetShaderResources(0, _countof(NullSRVs), NullSRVs);

    // Bind shaders
    Context->VSSetShader(VertexShader.Get(), nullptr, 0);
    Context->PSSetShader(PixelShader.Get(), nullptr, 0);

    // Setup IA
    Context->IASetInputLayout(InputLayout.Get());
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Bind inputs & outputs
    Context->PSSetShaderResources(0, NumSRVs, PSShaderResources);
    Context->OMSetRenderTargets(NumRTVs, RenderTargets, DepthStencilView.Get());

    //// Bind state objects
    //static const float BlendFactor[] = { 1.f, 1.f, 1.f, 1.f };
    //Context->OMSetBlendState(pass.BlendState, BlendFactor, D3D11_DEFAULT_SAMPLE_MASK);

    OnApply();
}

bool RenderPass11::InitializeShadersAndInputLayout(
    VertexType vertexType,
    const BYTE* vertexShaderData, size_t vertexShaderSize,
    const BYTE* pixelShaderData, size_t pixelShaderSize)
{
    ComPtr<ID3D11Device> device;
    Context->GetDevice(&device);

    uint32_t typeIndex = (uint32_t)vertexType;
    if (typeIndex >= (uint32_t)VertexType::Count)
    {
        LogError(L"Invalid vertex type specified.");
        return false;
    }

    CheckResult(device->CreateVertexShader(vertexShaderData, vertexShaderSize, nullptr, &VertexShader));
    CheckResult(device->CreatePixelShader(pixelShaderData, pixelShaderSize, nullptr, &PixelShader));
    CheckResult(device->CreateInputLayout(VertexElements[typeIndex], VertexElementCount[typeIndex], vertexShaderData, vertexShaderSize, &InputLayout));

    return true;
}

void RenderPass11::InitPSShaderResource(uint32_t slot, const ComPtr<ID3D11ShaderResourceView>& srv)
{
    if (PSShaderResources[slot])
    {
        // Setting over an existing one!
        assert(false);
        PSShaderResources[slot]->Release();
    }
    PSShaderResources[slot] = srv.Get();
    PSShaderResources[slot]->AddRef();

    if (slot >= NumSRVs)
    {
        ++NumSRVs;
    }
}

void RenderPass11::InitRenderTarget(uint32_t slot, const ComPtr<ID3D11RenderTargetView>& rtv)
{
    if (RenderTargets[slot])
    {
        // Setting over an existing one!
        assert(false);
        RenderTargets[slot]->Release();
    }
    RenderTargets[slot] = rtv.Get();
    RenderTargets[slot]->AddRef();

    if (slot >= NumRTVs)
    {
        ++NumRTVs;
    }
}


// GeometryPass

std::shared_ptr<GeometryPass11> GeometryPass11::Create(
    const ComPtr<ID3D11Device>& device,
    const ComPtr<ID3D11Texture2D>& lightAccum,
    const ComPtr<ID3D11Texture2D>& normals,
    const ComPtr<ID3D11Texture2D>& specularRoughness,
    const ComPtr<ID3D11Texture2D>& color,
    const ComPtr<ID3D11Texture2D>& depth)
{
    std::shared_ptr<GeometryPass11> pass(new GeometryPass11(device));
    if (pass)
    {
        if (pass->Initialize(lightAccum, normals, specularRoughness, color, depth))
        {
            return pass;
        }
    }
    return nullptr;
}

GeometryPass11::GeometryPass11(const ComPtr<ID3D11Device>& device)
    : RenderPass11(device, PassType::Geometry)
{
}

bool GeometryPass11::Initialize(
    const ComPtr<ID3D11Texture2D>& lightAccum,
    const ComPtr<ID3D11Texture2D>& normals,
    const ComPtr<ID3D11Texture2D>& specularRoughness,
    const ComPtr<ID3D11Texture2D>& color,
    const ComPtr<ID3D11Texture2D>& depth)
{
    ComPtr<ID3D11Device> device;
    Context->GetDevice(&device);

    if (!InitializeShadersAndInputLayout(VertexType::Standard, GeometryPassVS, sizeof(GeometryPassVS), GeometryPassPS, sizeof(GeometryPassPS)))
    {
        LogError(L"Failed to initiailize shaders and input layout.");
        return false;
    }

    ComPtr<ID3D11RenderTargetView> rtv;
    CheckResult(device->CreateRenderTargetView(lightAccum.Get(), nullptr, rtv.ReleaseAndGetAddressOf()));
    InitRenderTarget(0, rtv);
    CheckResult(device->CreateRenderTargetView(normals.Get(), nullptr, rtv.ReleaseAndGetAddressOf()));
    InitRenderTarget(1, rtv);
    CheckResult(device->CreateRenderTargetView(specularRoughness.Get(), nullptr, rtv.ReleaseAndGetAddressOf()));
    InitRenderTarget(2, rtv);
    CheckResult(device->CreateRenderTargetView(color.Get(), nullptr, rtv.ReleaseAndGetAddressOf()));
    InitRenderTarget(3, rtv);

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd{};
    dsvd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    CheckResult(device->CreateDepthStencilView(depth.Get(), &dsvd, &DepthStencilView));

    return true;
}

void GeometryPass11::OnApply()
{
    static const float clearColor[] = { 0.f, 0.f, 0.f, 1.f };
    Context->ClearRenderTargetView(RenderTargets[0], clearColor);
    Context->ClearDepthStencilView(DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
}
