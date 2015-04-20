#include "Precomp.h"
#include "DeferredRenderer11.h"
#include "Geometry.h"
#include "Object.h"
#include "Shaders/GeometryPassVS.h"
#include "Shaders/GeometryPassPS.h"
#include "Shaders/PostProjectionPassthroughVS.h"
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
}

DeferredRenderer11::~DeferredRenderer11()
{
}

bool DeferredRenderer11::AddObjects(const std::wstring& contentRoot, const std::wstring& modelFilename)
{
    static_assert(sizeof(ModelVertex) == sizeof(StandardVertex), "Make sure structures (and padding) match so we can read directly!");

    FileHandle modelFile(CreateFile((contentRoot + modelFilename).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!modelFile.IsValid())
    {
        LogError(L"Failed to open asset file.");
        return false;
    }

    DWORD bytesRead{};
    ModelHeader header{};
    if (!ReadFile(modelFile.Get(), &header, sizeof(header), &bytesRead, nullptr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

    if (header.Signature != header.ExpectedSignature)
    {
        LogError(L"Invalid model file.");
        return false;
    }

    uint32_t baseVertex = 0;
    uint32_t baseIndex = 0;

    std::shared_ptr<GeometryPool> pool = GeometryPool::Create(Device, VertexType::Standard, header.NumVertices, header.NumIndices);
    if (!pool->ReserveRange(header.NumVertices, header.NumIndices, &baseVertex, &baseIndex))
    {
        LogError(L"Not enough room in geo pool.");
        return false;
    }

    GeometryPools.push_back(pool);

    std::unique_ptr<StandardVertex[]> vertices(new StandardVertex[header.NumVertices]);
    if (!ReadFile(modelFile.Get(), vertices.get(), header.NumVertices * sizeof(StandardVertex), &bytesRead, nullptr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

    D3D11_BOX box{};
    box.right = header.NumVertices * sizeof(StandardVertex);
    box.bottom = 1;
    box.back = 1;

    Context->UpdateSubresource(pool->GetVertexBuffer().Get(), 0, &box, vertices.get(), header.NumVertices * sizeof(StandardVertex), 0);

    // Free up memory
    vertices.reset();

    std::unique_ptr<uint32_t[]> indices(new uint32_t[header.NumIndices]);
    if (!ReadFile(modelFile.Get(), indices.get(), header.NumIndices * sizeof(uint32_t), &bytesRead, nullptr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

    box.right = header.NumIndices * sizeof(uint32_t);

    Context->UpdateSubresource(pool->GetIndexBuffer().Get(), 0, &box, indices.get(), header.NumIndices * sizeof(uint32_t), 0);

    // Free up memory
    indices.reset();

    // Load objects
    for (int iObj = 0; iObj < (int)header.NumObjects; ++iObj)
    {
        ModelObject object{};
        if (!ReadFile(modelFile.Get(), &object, sizeof(object), &bytesRead, nullptr))
        {
            LogError(L"Failed to read file.");
            return false;
        }

        std::shared_ptr<Object> obj = std::make_shared<Object>();
        Objects.push_back(obj);

        XMStoreFloat4x4(&obj->RootTransform, XMMatrixIdentity());

        for (int iPart = 0; iPart < (int)object.NumParts; ++iPart)
        {
            ModelPart part{};
            if (!ReadFile(modelFile.Get(), &part, sizeof(part), &bytesRead, nullptr))
            {
                LogError(L"Failed to read file.");
                return false;
            }

            std::shared_ptr<Object::Part> meshPart = std::make_shared<Object::Part>();
            obj->Parts.push_back(meshPart);

            XMStoreFloat4x4(&meshPart->RelativeTransform, XMMatrixIdentity());

            meshPart->Mesh = std::make_shared<GeoMesh>();
            meshPart->Mesh->Pool = pool;
            meshPart->Mesh->BaseIndex = part.StartIndex;
            meshPart->Mesh->NumIndices = part.NumIndices;
            meshPart->Mesh->BaseVertex = baseVertex;

            if (part.DiffuseTexture[0] != 0)
            {
                std::wstring path = contentRoot + part.DiffuseTexture;
                auto it = CachedTextureMap.find(path);
                if (it == CachedTextureMap.end())
                {
                    if (!LoadTexture(path, &meshPart->AlbedoSRV))
                    {
                        LogError(L"Failed to load texture.");
                        return false;
                    }
                    CachedTextureMap[path] = meshPart->AlbedoSRV;
                }
                else
                {
                    meshPart->AlbedoSRV = it->second;
                }
            }
            if (part.NormalTexture[0] != 0)
            {
                std::wstring path = contentRoot + part.NormalTexture;
                auto it = CachedTextureMap.find(path);
                if (it == CachedTextureMap.end())
                {
                    if (!LoadTexture(path, &meshPart->NormalSRV))
                    {
                        LogError(L"Failed to load texture.");
                        return false;
                    }
                    CachedTextureMap[path] = meshPart->NormalSRV;
                }
                else
                {
                    meshPart->NormalSRV = it->second;
                }
            }
            if (part.SpecularTexture[0] != 0)
            {
                std::wstring path = contentRoot + part.SpecularTexture;
                auto it = CachedTextureMap.find(path);
                if (it == CachedTextureMap.end())
                {
                    if (!LoadTexture(path, &meshPart->SpecularSRV))
                    {
                        LogError(L"Failed to load texture.");
                        return false;
                    }
                    CachedTextureMap[path] = meshPart->SpecularSRV;
                }
                else
                {
                    meshPart->SpecularSRV = it->second;
                }
            }
        }
    }

    return true;
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

    ////////////////////////////////
    // Geometry pass

    ApplyPass(PassType::Geometry, DepthStencilView);

    Context->VSSetConstantBuffers(0, 1, GeometryCB.GetAddressOf());
    Context->PSSetSamplers(0, 1, LinearWrapSampler.GetAddressOf());
    Context->PSSetSamplers(1, 1, PointClampSampler.GetAddressOf());

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
    // Debug display depth

    ApplyPass(PassType::DebugDisplayDepth, nullptr);

    BindGeometryPool(FullscreenQuad->Pool);
    DrawMesh(FullscreenQuad);

    //Context->CopyResource(GBuffer[(uint32_t)GBufferSlice::LightAccum].Get(), GBuffer[(uint32_t)GBufferSlice::Color].Get());
    Context->CopyResource(GBuffer[(uint32_t)GBufferSlice::LightAccum].Get(), GBuffer[(uint32_t)GBufferSlice::Normals].Get());
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
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    CheckResult(dxgiFactory->CreateSwapChainForHwnd(Device.Get(), window, &scd,
        nullptr, nullptr, &SwapChain));

    D3D11_VIEWPORT vp{};
    vp.Width = (float)scd.Width;
    vp.Height = (float)scd.Height;
    vp.MaxDepth = 1.f;
    Context->RSSetViewports(1, &vp);

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

    // Create fullscreen quad
    PostProjectionVertex verts[] =
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

    GeometryPools.push_back(GeometryPool::Create(Device, VertexType::PostProjection, 1024, 1024));

    FullscreenQuad = std::make_shared<GeoMesh>();
    if (!GeometryPools.front()->ReserveRange(_countof(verts), _countof(indices), &FullscreenQuad->BaseVertex, &FullscreenQuad->BaseIndex))
    {
        assert(false);
        return false;
    }

    FullscreenQuad->Pool = GeometryPools.front();
    FullscreenQuad->NumIndices = _countof(indices);

    D3D11_BOX box{};
    box.right = _countof(verts) * sizeof(PostProjectionVertex);
    box.bottom = 1;
    box.back = 1;
    Context->UpdateSubresource(GeometryPools.front()->GetVertexBuffer().Get(), 0, &box, &verts, sizeof(verts), 0);

    box.right = FullscreenQuad->NumIndices * sizeof(uint32_t);
    Context->UpdateSubresource(GeometryPools.front()->GetIndexBuffer().Get(), 0, &box, &indices, sizeof(indices), 0);

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

    // Dbg Render Depth Pass
    CheckResult(Device->CreateVertexShader(PostProjectionPassthroughVS, sizeof(PostProjectionPassthroughVS), nullptr, &VertexShader[(uint32_t)PassType::DebugDisplayDepth]));
    CheckResult(Device->CreatePixelShader(DbgRenderDepthPS, sizeof(DbgRenderDepthPS), nullptr, &PixelShader[(uint32_t)PassType::DebugDisplayDepth]));
    CheckResult(Device->CreateInputLayout(VertexElements[(uint32_t)VertexType::PostProjection], VertexElementCount[(uint32_t)VertexType::PostProjection], PostProjectionPassthroughVS, sizeof(PostProjectionPassthroughVS), &InputLayout[(uint32_t)PassType::DebugDisplayDepth]));
    PSShaderResources[(uint32_t)PassType::DebugDisplayDepth][0] = DepthStencilSRV.Get();
    NumShaderResources[(uint32_t)PassType::DebugDisplayDepth] = 1;
    RenderTargets[(uint32_t)PassType::DebugDisplayDepth][0] = GBufferRTV[(uint32_t)GBufferSlice::LightAccum].Get();
    NumRenderTargets[(uint32_t)PassType::DebugDisplayDepth] = 1;

    return true;
}

bool DeferredRenderer11::LoadTexture(const std::wstring& filename, ID3D11ShaderResourceView** srv)
{
    FileHandle texFile(CreateFile(filename.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!texFile.IsValid())
    {
        LogError(L"Failed to open texture.");
        return false;
    }

    DWORD bytesRead{};
    uint32_t fileSize = GetFileSize(texFile.Get(), nullptr);

    TextureHeader texHeader{};
    if (!ReadFile(texFile.Get(), &texHeader, sizeof(texHeader), &bytesRead, nullptr))
    {
        LogError(L"Failed to read texture.");
        return false;
    }

    if (texHeader.Signature != TextureHeader::ExpectedSignature)
    {
        LogError(L"Invalid texture file.");
        return false;
    }

    uint32_t pixelDataSize = fileSize - sizeof(TextureHeader);
    std::unique_ptr<uint8_t[]> pixelData(new uint8_t[pixelDataSize]);
    if (!ReadFile(texFile.Get(), pixelData.get(), pixelDataSize, &bytesRead, nullptr))
    {
        LogError(L"Failed to read texture data.");
        return false;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.ArraySize = texHeader.ArrayCount;
    td.Format = texHeader.Format;
#if USE_SRGB
    if (td.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
    {
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
    else if (td.Format == DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    }
#endif
    td.Width = texHeader.Width;
    td.Height = texHeader.Height;
    td.MipLevels = texHeader.MipLevels;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;

    D3D11_SUBRESOURCE_DATA init[20] {};
    uint32_t bpp = (uint32_t)BitsPerPixel(td.Format) / 8;

    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = S_OK;

    // Only try to use mips if width & height are the same size
    if (td.Width == td.Height && td.MipLevels > 1)
    {
        uint32_t width = td.Width;
        uint32_t height = td.Height;
        uint8_t* pPixels = pixelData.get();

        for (int m = 0; m < (int)td.MipLevels; ++m)
        {
            init[m].pSysMem = pPixels;
            init[m].SysMemPitch = width * bpp;
            init[m].SysMemSlicePitch = width * height * bpp;

            width >>= 1;
            height >>= 1;
            pPixels += init[m].SysMemSlicePitch;
        }

        hr = Device->CreateTexture2D(&td, init, &texture);
    }
    else
    {
        td.MipLevels = 1;

        init[0].pSysMem = pixelData.get();
        init[0].SysMemPitch = td.Width * bpp;
        init[0].SysMemSlicePitch = td.Width * td.Height * bpp;

        hr = Device->CreateTexture2D(&td, init, &texture);
    }
    if (FAILED(hr))
    {
        LogError(L"Failed to create texture.");
        return false;
    }

    hr = Device->CreateShaderResourceView(texture.Get(), nullptr, srv);
    if (FAILED(hr))
    {
        LogError(L"Failed to create texture SRV.");
        return false;
    }

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
    //static const float BlendFactor[] = { 1.f, 1.f, 1.f, 1.f };
    //Context->OMSetBlendState(pass.BlendState, BlendFactor, D3D11_DEFAULT_SAMPLE_MASK);

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
