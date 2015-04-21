#include "Precomp.h"
#include "DeferredRenderer11.h"
#include "Geometry.h"
#include "Object.h"
#include "Shaders/GeometryPassVS.h"
#include "Shaders/GeometryPassPS.h"
#include "Shaders/ClipSpacePassthroughVS.h"
#include "Shaders/DirectionalLightsPS.h"
#include "Shaders/DbgRenderDepthPS.h"

ID3D11ShaderResourceView* const  DeferredRenderer11::NullSRVs[8] {};
ID3D11RenderTargetView* const    DeferredRenderer11::NullRTVs[8] {};

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
    ZeroMemory(PSShaderResources, sizeof(PSShaderResources));
    ZeroMemory(RenderTargets, sizeof(RenderTargets));
    ZeroMemory(NumShaderResources, sizeof(NumShaderResources));
    ZeroMemory(NumRenderTargets, sizeof(NumRenderTargets));
    ZeroMemory(&Viewport, sizeof(Viewport));
}

DeferredRenderer11::~DeferredRenderer11()
{
}

bool DeferredRenderer11::Render(FXMMATRIX view, FXMMATRIX projection, bool vsync)
{
    static const float clearLights[] = { 0.f, 0.f, 0.f, 1.f };
    static const float clearNormals[] = { 0.5f, 0.5f, 1.f, 1.f };
    static const float clearSpecularRoughness[] = { 0.f, 0.f, 0.f, 0.5f };
    static const float clearColor[] = { 0.f, 0.f, 0.f, 1.f };

    Context->ClearRenderTargetView(GBufferRTV[(uint32_t)GBufferSlice::LightAccum].Get(), clearLights);
    Context->ClearRenderTargetView(GBufferRTV[(uint32_t)GBufferSlice::Normals].Get(), clearNormals);
    Context->ClearRenderTargetView(GBufferRTV[(uint32_t)GBufferSlice::SpecularRoughness].Get(), clearSpecularRoughness);
    Context->ClearRenderTargetView(GBufferRTV[(uint32_t)GBufferSlice::Color].Get(), clearColor);

    Context->ClearDepthStencilView(DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);

    Context->PSSetSamplers(0, 1, LinearWrapSampler.GetAddressOf());
    Context->PSSetSamplers(1, 1, PointClampSampler.GetAddressOf());

    ////////////////////////////////
    // Geometry pass

    ApplyPass(PassType::Geometry, DepthStencilView);

    Context->VSSetConstantBuffers(0, 1, GeometryCB.GetAddressOf());

    GeometryVSConstants constants;
    XMStoreFloat4x4(&constants.View, view);
    XMStoreFloat4x4(&constants.Projection, projection);

    for (auto& obj : Objects)
    {
        XMMATRIX root = XMLoadFloat4x4(&obj->RootTransform);

        for (auto& part : obj->Parts)
        {
            XMMATRIX xform = XMLoadFloat4x4(&part->RelativeTransform);

            // We should map this as a dynamic CB most likely, for better perf (or at least split out world from the camera stuff)
            XMStoreFloat4x4(&constants.World, xform * root);
            Context->UpdateSubresource(GeometryCB.Get(), 0, nullptr, &constants, sizeof(constants), 0);

            ID3D11ShaderResourceView* srvs[] = { part->AlbedoSRV.Get(), part->NormalSRV.Get(), part->SpecularSRV.Get() };
            Context->PSSetShaderResources(0, _countof(srvs), srvs);

            BindGeometryPool(part->Mesh->Pool);
            DrawMesh(part->Mesh);
        }
    }

    ////////////////////////////////
    // Geometry pass

    ApplyPass(PassType::DirectionalLighting, nullptr);

    Context->PSSetConstantBuffers(0, 1, DLightCB.GetAddressOf());

    XMVECTOR det;

    DLightPSConstants dLightConstants;
    dLightConstants.NumLights = 1;
    XMStoreFloat3(&dLightConstants.Lights[0].Dir, XMVector3TransformNormal(XMVector3Normalize(XMVectorSet(1.f, 1.f, 1.f, 0.f)), view));
    dLightConstants.Lights[0].Color = XMFLOAT3(1.f, 1.f, 1.f);
    dLightConstants.InvViewportSize.x = 1.f / Viewport.Width;
    dLightConstants.InvViewportSize.y = 1.f / Viewport.Height;
    XMStoreFloat4x4(&dLightConstants.InvProjection, XMMatrixInverse(&det, projection));
    Context->UpdateSubresource(DLightCB.Get(), 0, nullptr, &dLightConstants, sizeof(dLightConstants), 0);

    BindGeometryPool(FullscreenQuad->Pool);
    DrawMesh(FullscreenQuad);

    //Context->CopyResource(GBuffer[(uint32_t)GBufferSlice::LightAccum].Get(), GBuffer[(uint32_t)GBufferSlice::Color].Get());
    //Context->CopyResource(GBuffer[(uint32_t)GBufferSlice::LightAccum].Get(), GBuffer[(uint32_t)GBufferSlice::Normals].Get());
    //Context->CopyResource(GBuffer[(uint32_t)GBufferSlice::LightAccum].Get(), GBuffer[(uint32_t)GBufferSlice::SpecularRoughness].Get());

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
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    CheckResult(dxgiFactory->CreateSwapChainForHwnd(Device.Get(), window, &scd,
        nullptr, nullptr, &SwapChain));

    Viewport.Width = (float)scd.Width;
    Viewport.Height = (float)scd.Height;
    Viewport.MaxDepth = 1.f;
    Context->RSSetViewports(1, &Viewport);

    if (!CreateStateObjects())
    {
        return false;
    }

    if (!CreateGBuffer())
    {
        return false;
    }

    if (!InitializePasses())
    {
        return false;
    }

    // Create geometry pass CB
    D3D11_BUFFER_DESC bd{};

    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.ByteWidth = sizeof(GeometryVSConstants);
    bd.StructureByteStride = sizeof(GeometryVSConstants);
    bd.Usage = D3D11_USAGE_DEFAULT;

    CheckResult(Device->CreateBuffer(&bd, nullptr, &GeometryCB));

    bd.ByteWidth = sizeof(DLightPSConstants);
    bd.StructureByteStride = sizeof(DLightPSConstants);

    CheckResult(Device->CreateBuffer(&bd, nullptr, &DLightCB));

    // Create fullscreen quad
    ClipSpace2DVertex verts[] =
    {
        { XMFLOAT2(-1, 1),  XMFLOAT2(0.f, 0.f) },
        { XMFLOAT2(-1, -1), XMFLOAT2(0.f, 1.f) },
        { XMFLOAT2(1, -1),  XMFLOAT2(1.f, 1.f) },
        { XMFLOAT2(1, 1),   XMFLOAT2(1.f, 0.f) },
    };

    uint32_t indices[] = 
    {
        0, 1, 2, 0, 2, 3
    };

    FullscreenQuad = std::make_shared<GeoMesh>();
    FullscreenQuad->Pool = GeometryPool::Create(Device, VertexType::ClipSpace2D, 1024, 1024);

    if (!FullscreenQuad->Pool->ReserveRange(_countof(verts), _countof(indices), &FullscreenQuad->BaseVertex, &FullscreenQuad->BaseIndex))
    {
        assert(false);
        return false;
    }

    FullscreenQuad->NumIndices = _countof(indices);

    D3D11_BOX box{};
    box.right = _countof(verts) * sizeof(ClipSpace2DVertex);
    box.bottom = 1;
    box.back = 1;
    Context->UpdateSubresource(FullscreenQuad->Pool->GetVertexBuffer().Get(), 0, &box, &verts, sizeof(verts), 0);

    box.right = FullscreenQuad->NumIndices * sizeof(uint32_t);
    Context->UpdateSubresource(FullscreenQuad->Pool->GetIndexBuffer().Get(), 0, &box, &indices, sizeof(indices), 0);

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

    // Create BlendStates
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    CheckResult(Device->CreateBlendState(&bd, &AdditiveBlendState));

    return true;
}

bool DeferredRenderer11::CreateGBuffer()
{
    // We'll use the back buffer as the light accum, since that's where the final image is generated
    CheckResult(SwapChain->GetBuffer(0, IID_PPV_ARGS(&GBuffer[(uint32_t)GBufferSlice::LightAccum])));
    CheckResult(Device->CreateRenderTargetView(GBuffer[(uint32_t)GBufferSlice::LightAccum].Get(), nullptr, &GBufferRTV[(uint32_t)GBufferSlice::LightAccum]));

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
        CheckResult(Device->CreateShaderResourceView(GBuffer[i].Get(), nullptr, &GBufferSRV[i]));
        CheckResult(Device->CreateRenderTargetView(GBuffer[i].Get(), nullptr, &GBufferRTV[i]));
    }

    // Create depth buffer
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    td.Format = DXGI_FORMAT_R24G8_TYPELESS;
    CheckResult(Device->CreateTexture2D(&td, nullptr, &DepthStencilBuffer));

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd{};
    dsvd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    CheckResult(Device->CreateDepthStencilView(DepthStencilBuffer.Get(), &dsvd, &DepthStencilView));

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    CheckResult(Device->CreateShaderResourceView(DepthStencilBuffer.Get(), &srvd, &DepthStencilSRV));

    return true;
}

bool DeferredRenderer11::InitializePasses()
{
    // Geometry Pass
    CheckResult(Device->CreateVertexShader(GeometryPassVS, sizeof(GeometryPassVS), nullptr, &VertexShader[(uint32_t)PassType::Geometry]));
    CheckResult(Device->CreatePixelShader(GeometryPassPS, sizeof(GeometryPassPS), nullptr, &PixelShader[(uint32_t)PassType::Geometry]));
    CheckResult(Device->CreateInputLayout(VertexElements[(uint32_t)VertexType::Standard], VertexElementCount[(uint32_t)VertexType::Standard], GeometryPassVS, sizeof(GeometryPassVS), &InputLayout[(uint32_t)PassType::Geometry]));
    RenderTargets[(uint32_t)PassType::Geometry][0] = GBufferRTV[(uint32_t)GBufferSlice::LightAccum].Get();
    RenderTargets[(uint32_t)PassType::Geometry][1] = GBufferRTV[(uint32_t)GBufferSlice::Normals].Get();
    RenderTargets[(uint32_t)PassType::Geometry][2] = GBufferRTV[(uint32_t)GBufferSlice::SpecularRoughness].Get();
    RenderTargets[(uint32_t)PassType::Geometry][3] = GBufferRTV[(uint32_t)GBufferSlice::Color].Get();
    NumRenderTargets[(uint32_t)PassType::Geometry] = 4;

    // Directional Lights Pass
    CheckResult(Device->CreateVertexShader(ClipSpacePassthroughVS, sizeof(ClipSpacePassthroughVS), nullptr, &VertexShader[(uint32_t)PassType::DirectionalLighting]));
    CheckResult(Device->CreatePixelShader(DirectionalLightsPS, sizeof(DirectionalLightsPS), nullptr, &PixelShader[(uint32_t)PassType::DirectionalLighting]));
    CheckResult(Device->CreateInputLayout(VertexElements[(uint32_t)VertexType::ClipSpace2D], VertexElementCount[(uint32_t)VertexType::ClipSpace2D], ClipSpacePassthroughVS, sizeof(ClipSpacePassthroughVS), &InputLayout[(uint32_t)PassType::DirectionalLighting]));
    PSShaderResources[(uint32_t)PassType::DirectionalLighting][0] = DepthStencilSRV.Get();
    PSShaderResources[(uint32_t)PassType::DirectionalLighting][1] = GBufferSRV[(uint32_t)GBufferSlice::Normals].Get();
    PSShaderResources[(uint32_t)PassType::DirectionalLighting][2] = GBufferSRV[(uint32_t)GBufferSlice::SpecularRoughness].Get();
    PSShaderResources[(uint32_t)PassType::DirectionalLighting][3] = GBufferSRV[(uint32_t)GBufferSlice::Color].Get();
    NumShaderResources[(uint32_t)PassType::DirectionalLighting] = 4;
    RenderTargets[(uint32_t)PassType::DirectionalLighting][0] = GBufferRTV[(uint32_t)GBufferSlice::LightAccum].Get();
    NumRenderTargets[(uint32_t)PassType::DirectionalLighting] = 1;
    BlendStates[(uint32_t)PassType::DirectionalLighting] = AdditiveBlendState;

    // Dbg Render Depth Pass
    CheckResult(Device->CreateVertexShader(ClipSpacePassthroughVS, sizeof(ClipSpacePassthroughVS), nullptr, &VertexShader[(uint32_t)PassType::DebugDisplayDepth]));
    CheckResult(Device->CreatePixelShader(DbgRenderDepthPS, sizeof(DbgRenderDepthPS), nullptr, &PixelShader[(uint32_t)PassType::DebugDisplayDepth]));
    CheckResult(Device->CreateInputLayout(VertexElements[(uint32_t)VertexType::ClipSpace2D], VertexElementCount[(uint32_t)VertexType::ClipSpace2D], ClipSpacePassthroughVS, sizeof(ClipSpacePassthroughVS), &InputLayout[(uint32_t)PassType::DebugDisplayDepth]));
    PSShaderResources[(uint32_t)PassType::DebugDisplayDepth][0] = DepthStencilSRV.Get();
    NumShaderResources[(uint32_t)PassType::DebugDisplayDepth] = 1;
    RenderTargets[(uint32_t)PassType::DebugDisplayDepth][0] = GBufferRTV[(uint32_t)GBufferSlice::LightAccum].Get();
    NumRenderTargets[(uint32_t)PassType::DebugDisplayDepth] = 1;

    return true;
}

void DeferredRenderer11::ApplyPass(PassType type, const ComPtr<ID3D11DepthStencilView>& dsv)
{
    uint32_t typeIndex = (uint32_t)type;
    if (typeIndex >= (uint32_t)PassType::Count)
    {
        assert(false);
        return;
    }

    // Clear inputs & outputs
    Context->OMSetRenderTargets(_countof(NullRTVs), NullRTVs, nullptr);
    Context->PSSetShaderResources(0, _countof(NullSRVs), NullSRVs);

    // Bind shaders
    Context->VSSetShader(VertexShader[typeIndex].Get(), nullptr, 0);
    Context->PSSetShader(PixelShader[typeIndex].Get(), nullptr, 0);

    // Setup IA
    Context->IASetInputLayout(InputLayout[typeIndex].Get());
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Bind inputs & outputs
    Context->PSSetShaderResources(0, NumShaderResources[typeIndex], PSShaderResources[typeIndex]);
    Context->OMSetRenderTargets(NumRenderTargets[typeIndex], RenderTargets[typeIndex], dsv.Get());

    //// Bind state objects
    static const float BlendFactor[] = { 1.f, 1.f, 1.f, 1.f };
    Context->OMSetBlendState(BlendStates[typeIndex].Get(), BlendFactor, D3D11_DEFAULT_SAMPLE_MASK);

    Context->RSSetState(RasterizerState.Get());
}

void DeferredRenderer11::BindGeometryPool(const std::shared_ptr<GeometryPool>& pool)
{
    assert(pool != nullptr);

    uint32_t offset = 0;
    Context->IASetVertexBuffers(0, 1, pool->GetVertexBuffer().GetAddressOf(), &VertexStride[(uint32_t)pool->GetType()], &offset);
    Context->IASetIndexBuffer(pool->GetIndexBuffer().Get(), DXGI_FORMAT_R32_UINT, offset);
}

void DeferredRenderer11::DrawMesh(const std::shared_ptr<GeoMesh>& mesh)
{
    Context->DrawIndexed(mesh->NumIndices, mesh->BaseIndex, mesh->BaseVertex);
}
