#include "Precomp.h"
#include "DeferredRenderer11.h"

#define CheckResult(x) { HRESULT _hr = (x); if (FAILED(_hr)) { LogError(L#x L" failed with hr = 0x%08x.", _hr); return false; }}

std::unique_ptr<DeferredRenderer11> DeferredRenderer11::Create(HWND window)
{
    std::unique_ptr<DeferredRenderer11> renderer(new DeferredRenderer11());
    if (renderer)
    {
        if (renderer->Initialize(window))
        {
            return renderer;
        }
    }
    return nullptr;
}

DeferredRenderer11::DeferredRenderer11()
{
}

DeferredRenderer11::~DeferredRenderer11()
{
}

bool DeferredRenderer11::Render(FXMMATRIX view, FXMMATRIX projection, bool vsync)
{
    RenderPasses[(uint32_t)PassType::Geometry]->Apply();

    uint32_t offset = 0;
    Context->IASetVertexBuffers(0, 1, QuadVB.GetAddressOf(), &VertexStride[(uint32_t)VertexType::Standard], &offset);

    GeometryVSConstants constants;
    XMStoreFloat4x4(&constants.World, XMMatrixIdentity());
    XMStoreFloat4x4(&constants.View, view);
    XMStoreFloat4x4(&constants.Projection, projection);

    Context->VSSetConstantBuffers(0, 1, GeometryCB.GetAddressOf());
    Context->UpdateSubresource(GeometryCB.Get(), 0, nullptr, &constants, sizeof(constants), 0);

    Context->Draw(6, 0);

    CheckResult(SwapChain->Present(vsync ? 1 : 0, 0));

    return true;
}

bool DeferredRenderer11::Initialize(HWND window)
{
    ComPtr<IDXGIFactory2> dxgiFactory;
    CheckResult(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));

    // NOTE: For now, we just always grab adapter 0. Ideally, we'd search for the best match
    ComPtr<IDXGIAdapter> adapter;
    CheckResult(dxgiFactory->EnumAdapters(0, &adapter));

    UINT flags = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

#if defined(_DEBUG)
    flags = D3D11_CREATE_DEVICE_DEBUG;
#endif

    CheckResult(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        flags, &featureLevel, 1, D3D11_SDK_VERSION, &Device, nullptr, &Context));

    RECT rcClient{};
    GetClientRect(window, &rcClient);

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.BufferCount = 2;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.Width = rcClient.right - rcClient.left;
    scd.Height = rcClient.bottom - rcClient.top;
    scd.SampleDesc.Count = 1;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    CheckResult(dxgiFactory->CreateSwapChainForHwnd(Device.Get(), window, &scd,
        nullptr, nullptr, &SwapChain));

    // Create quad
    struct Vertex
    {
        XMFLOAT3 Position;
        XMFLOAT3 Normal;
        XMFLOAT3 Tangent;
        XMFLOAT3 Bitangent;
        XMFLOAT2 TexCoord;
    };

    Vertex verts[] =
    {
        { XMFLOAT3(-1, 1, 0.f), XMFLOAT3(0.f, 0.f, 1.f), XMFLOAT3(1.f, 0.f, 0.f), XMFLOAT3(0.f, -1.f, 0.f), XMFLOAT2(0.f, 0.f) },
        { XMFLOAT3(1, -1, 0.f), XMFLOAT3(0.f, 0.f, 1.f), XMFLOAT3(1.f, 0.f, 0.f), XMFLOAT3(0.f, -1.f, 0.f), XMFLOAT2(1.f, 1.f) },
        { XMFLOAT3(1, 1, 0.f), XMFLOAT3(0.f, 0.f, 1.f), XMFLOAT3(1.f, 0.f, 0.f), XMFLOAT3(0.f, -1.f, 0.f), XMFLOAT2(1.f, 0.f) },

        { XMFLOAT3(-1, 1, 0.f), XMFLOAT3(0.f, 0.f, 1.f), XMFLOAT3(1.f, 0.f, 0.f), XMFLOAT3(0.f, -1.f, 0.f), XMFLOAT2(0.f, 0.f) },
        { XMFLOAT3(-1, -1, 0.f), XMFLOAT3(0.f, 0.f, 1.f), XMFLOAT3(1.f, 0.f, 0.f), XMFLOAT3(0.f, -1.f, 0.f), XMFLOAT2(0.f, 1.f) },
        { XMFLOAT3(1, -1, 0.f), XMFLOAT3(0.f, 0.f, 1.f), XMFLOAT3(1.f, 0.f, 0.f), XMFLOAT3(0.f, -1.f, 0.f), XMFLOAT2(1.f, 1.f) },
    };

    D3D11_BUFFER_DESC bd{};
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = sizeof(verts);
    bd.StructureByteStride = sizeof(Vertex);
    bd.Usage = D3D11_USAGE_DEFAULT;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = verts;
    init.SysMemPitch = bd.ByteWidth;

    CheckResult(Device->CreateBuffer(&bd, &init, &QuadVB));

    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.ByteWidth = sizeof(GeometryVSConstants);
    bd.StructureByteStride = sizeof(GeometryVSConstants);

    CheckResult(Device->CreateBuffer(&bd, nullptr, &GeometryCB));

    if (!CreateStateObjects())
    {
        return false;
    }

    if (!CreateGBuffer())
    {
        return false;
    }

    Context->RSSetState(RasterizerState.Get());

    D3D11_VIEWPORT vp{};
    vp.Width = (float)scd.Width;
    vp.Height = (float)scd.Height;
    vp.MaxDepth = 1.f;
    Context->RSSetViewports(1, &vp);

    // Initialize passes
    RenderPasses[(uint32_t)PassType::Geometry] = GeometryPass11::Create(
        Device,
        GBuffer[(uint32_t)GBufferSlice::LightAccum],
        GBuffer[(uint32_t)GBufferSlice::Normals],
        GBuffer[(uint32_t)GBufferSlice::SpecularRoughness],
        GBuffer[(uint32_t)GBufferSlice::Color],
        DepthStencilBuffer);

    return true;
}

bool DeferredRenderer11::CreateStateObjects()
{
    // Create CCW rasterizer
    D3D11_RASTERIZER_DESC rd{};
    rd.CullMode = D3D11_CULL_BACK;
    rd.DepthClipEnable = 1;
    rd.FrontCounterClockwise = TRUE;
    rd.FillMode = D3D11_FILL_SOLID;
    CheckResult(Device->CreateRasterizerState(&rd, &RasterizerState));

    // Create samplers
    D3D11_SAMPLER_DESC sd{};
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.MaxLOD = D3D11_FLOAT32_MAX; // No clamping of mips
    CheckResult(Device->CreateSamplerState(&sd, &LinearWrapSampler));

    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    CheckResult(Device->CreateSamplerState(&sd, &PointClampSampler));

    return true;
}

bool DeferredRenderer11::CreateGBuffer()
{
    // We'll use the back buffer as the light accum, since that's where the final image is generated
    CheckResult(SwapChain->GetBuffer(0, IID_PPV_ARGS(&GBuffer[(uint32_t)GBufferSlice::LightAccum])));

    D3D11_TEXTURE2D_DESC td{};
    GBuffer[(uint32_t)GBufferSlice::LightAccum]->GetDesc(&td);

    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;

    for (int i = 0; i < (uint32_t)GBufferSlice::Count; ++i)
    {
        if (i == (uint32_t)GBufferSlice::LightAccum) continue;  // Already handled above

        CheckResult(Device->CreateTexture2D(&td, nullptr, &GBuffer[i]));
    }

    // Create depth buffer
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    td.Format = DXGI_FORMAT_R24G8_TYPELESS;
    CheckResult(Device->CreateTexture2D(&td, nullptr, &DepthStencilBuffer));

    return true;
}
