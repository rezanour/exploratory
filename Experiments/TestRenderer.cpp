#include "Precomp.h"
#include "TestRenderer.h"
#include "Debug.h"
#include "Shaders/SimpleTransformVS.h"
#include "Shaders/SimpleTexturePS.h"

//#define USE_SRGB 1

std::unique_ptr<TestRenderer> TestRenderer::Create(HWND window)
{
    std::unique_ptr<TestRenderer> renderer(new TestRenderer(window));
    if (renderer)
    {
        if (renderer->Initialize())
        {
            return renderer;
        }
    }
    return nullptr;
}

TestRenderer::TestRenderer(HWND window)
    : Window(window)
{
}

TestRenderer::~TestRenderer()
{
}

bool TestRenderer::AddMeshes(const std::wstring& contentRoot, const std::wstring& modelFilename)
{
    static_assert(sizeof(ModelVertex) == sizeof(Vertex), "Make sure structures (and padding) match so we can read directly!");

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

    TheScene.reset(new Scene);

    TheScene->VertexCount = header.NumVertices;
    TheScene->IndexCount = header.NumIndices;

    std::unique_ptr<Vertex[]> vertices(new Vertex[header.NumVertices]);
    if (!ReadFile(modelFile.Get(), vertices.get(), header.NumVertices * sizeof(Vertex), &bytesRead, nullptr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

    D3D11_BUFFER_DESC bd {};
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = sizeof(Vertex) * header.NumVertices;
    bd.StructureByteStride = sizeof(Vertex);
    bd.Usage = D3D11_USAGE_DEFAULT;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = vertices.get();
    init.SysMemPitch = bd.ByteWidth;
    init.SysMemSlicePitch = init.SysMemPitch;

    HRESULT hr = Device->CreateBuffer(&bd, &init, &TheScene->VertexBuffer);
    if (FAILED(hr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

    // Free up memory
    vertices.reset();

    std::unique_ptr<uint32_t[]> indices(new uint32_t[header.NumIndices]);
    if (!ReadFile(modelFile.Get(), indices.get(), header.NumIndices * sizeof(uint32_t), &bytesRead, nullptr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.ByteWidth = sizeof(uint32_t) * header.NumIndices;
    bd.StructureByteStride = sizeof(uint32_t);

    init.pSysMem = indices.get();
    init.SysMemPitch = bd.ByteWidth;
    init.SysMemSlicePitch = init.SysMemPitch;

    hr = Device->CreateBuffer(&bd, &init, &TheScene->IndexBuffer);
    if (FAILED(hr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

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
        obj->Name = object.Name;
        XMStoreFloat4x4(&obj->World, XMMatrixIdentity());

        for (int iPart = 0; iPart < (int)object.NumParts; ++iPart)
        {
            ModelPart part{};
            if (!ReadFile(modelFile.Get(), &part, sizeof(part), &bytesRead, nullptr))
            {
                LogError(L"Failed to read file.");
                return false;
            }

            Mesh mesh{};
            mesh.StartIndex = part.StartIndex;
            mesh.NumIndices = part.NumIndices;

            if (part.DiffuseTexture[0] != 0)
            {
                std::wstring path = contentRoot + part.DiffuseTexture;
                auto it = CachedTextureMap.find(path);
                if (it == CachedTextureMap.end())
                {
                    if (!LoadTexture(path, &mesh.AlbedoSRV))
                    {
                        LogError(L"Failed to load texture.");
                        return false;
                    }
                    CachedTextureMap[path] = mesh.AlbedoSRV;
                }
                else
                {
                    mesh.AlbedoSRV = it->second;
                }
            }
            if (part.NormalTexture[0] != 0)
            {
                std::wstring path = contentRoot + part.NormalTexture;
                auto it = CachedTextureMap.find(path);
                if (it == CachedTextureMap.end())
                {
                    if (!LoadTexture(path, &mesh.BumpDerivativeSRV))
                    {
                        LogError(L"Failed to load texture.");
                        return false;
                    }
                    CachedTextureMap[path] = mesh.BumpDerivativeSRV;
                }
                else
                {
                    mesh.BumpDerivativeSRV = it->second;
                }
            }
            if (part.SpecularTexture[0] != 0)
            {
                std::wstring path = contentRoot + part.SpecularTexture;
                auto it = CachedTextureMap.find(path);
                if (it == CachedTextureMap.end())
                {
                    if (!LoadTexture(path, &mesh.SpecularSRV))
                    {
                        LogError(L"Failed to load texture.");
                        return false;
                    }
                    CachedTextureMap[path] = mesh.SpecularSRV;
                }
                else
                {
                    mesh.SpecularSRV = it->second;
                }
            }

            obj->Meshes.push_back(mesh);
        }

        TheScene->Objects.push_back(obj);
    }

    return true;
}

bool TestRenderer::Render(FXMVECTOR cameraPosition, FXMMATRIX view, FXMMATRIX projection, bool vsync)
{
    Clear();

    static const uint32_t stride = sizeof(Vertex);
    static const uint32_t offset = 0;

    D3D11_MAPPED_SUBRESOURCE mapped{};

    Context->IASetVertexBuffers(0, 1, TheScene->VertexBuffer.GetAddressOf(), &stride, &offset);
    Context->IASetIndexBuffer(TheScene->IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

    XMStoreFloat3(&LightData.EyePosition, cameraPosition);
    LightData.NumLights = 0;
    LightData.Lights[0].Direction = XMFLOAT3(1.f, 1.f, 1.f);
    LightData.Lights[0].Color = XMFLOAT3(0.6f, 0.6f, 0.6f);
    LightData.Lights[1].Direction = XMFLOAT3(-1.f, 1.f, -1.f);
    LightData.Lights[1].Color = XMFLOAT3(0.5f, 0.5f, 0.5f);

    LightData.NumPointLights = 3;
    LightData.PointLights[0].Position = XMFLOAT3(0.f, 300.f, 0.f);
    LightData.PointLights[0].Color = XMFLOAT3(0.6f, 0.6f, 0.6f);
    LightData.PointLights[0].Radius = 500.f;
    LightData.PointLights[1].Position = XMFLOAT3(-800.f, 300.f, 0.f);
    LightData.PointLights[1].Color = XMFLOAT3(0.6f, 0.6f, 0.9f);
    LightData.PointLights[1].Radius = 500.f;
    LightData.PointLights[2].Position = XMFLOAT3(800.f, 300.f, 0.f);
    LightData.PointLights[2].Color = XMFLOAT3(0.9f, 0.6f, 0.6f);
    LightData.PointLights[2].Radius = 500.f;

    Context->UpdateSubresource(LightsConstantBuffer.Get(), 0, nullptr, &LightData, sizeof(LightData), 0);
    
    for (auto& object : TheScene->Objects)
    {
        if (FAILED(Context->Map(ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            LogError(L"Failed to map constant buffer.");
        }

        Constants* constants = (Constants*)mapped.pData;
        XMStoreFloat4x4(&constants->ViewProjection, view * projection);
        constants->World = object->World;

        Context->Unmap(ConstantBuffer.Get(), 0);

        for (auto& mesh : object->Meshes)
        {
            ID3D11ShaderResourceView* srvs[] = { mesh.AlbedoSRV.Get(), mesh.BumpDerivativeSRV.Get(), mesh.SpecularSRV.Get() };
            Context->PSSetShaderResources(0, _countof(srvs), srvs);
            Context->DrawIndexed(mesh.NumIndices, mesh.StartIndex, 0);
        }
    }

    return Present(vsync);
}

bool TestRenderer::Initialize()
{
    UINT d3dFlag = 0;
#if defined(_DEBUG)
    d3dFlag |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        d3dFlag, &featureLevel, 1, D3D11_SDK_VERSION, &Device, nullptr, &Context);
    if (FAILED(hr))
    {
        // Did it fail because we're requesting the debug layer and it's not present on this machine?
        if (d3dFlag == D3D11_CREATE_DEVICE_DEBUG && hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
        {
            // Try again without debug layer
            d3dFlag &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                d3dFlag, &featureLevel, 1, D3D11_SDK_VERSION, &Device, nullptr, &Context);
        }

        if (FAILED(hr))
        {
            LogError(L"Failed to create D3D11 device.");
            return false;
        }
    }

    UINT dxgiFlag = 0;
#if defined(_DEBUG)
    dxgiFlag |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    ComPtr<IDXGIFactory> factory;
    hr = CreateDXGIFactory2(dxgiFlag, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        LogError(L"Failed to create DXGI factory.");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
#if USE_SRGB
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
#else
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
#endif
    scd.SampleDesc.Count = 4;
    scd.OutputWindow = Window;
    scd.Windowed = TRUE;

    hr = factory->CreateSwapChain(Device.Get(), &scd, &SwapChain);
    if (FAILED(hr))
    {
        LogError(L"Failed to create swapchain.");
        return false;
    }

    ComPtr<ID3D11Texture2D> texture;
    hr = SwapChain->GetBuffer(0, IID_PPV_ARGS(&texture));
    if (FAILED(hr))
    {
        LogError(L"Failed to get backbuffer.");
        return false;
    }

    hr = Device->CreateRenderTargetView(texture.Get(), nullptr, &RenderTarget);
    if (FAILED(hr))
    {
        LogError(L"Failed to get backbuffer.");
        return false;
    }

    D3D11_TEXTURE2D_DESC td{};
    texture->GetDesc(&td);
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    td.Format = DXGI_FORMAT_D32_FLOAT;
    hr = Device->CreateTexture2D(&td, nullptr, texture.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        LogError(L"Failed to create depth texture.");
        return false;
    }

    hr = Device->CreateDepthStencilView(texture.Get(), nullptr, &DepthBuffer);
    if (FAILED(hr))
    {
        LogError(L"Failed to create depth stencil view.");
        return false;
    }

    hr = Device->CreateVertexShader(SimpleTransformVS, sizeof(SimpleTransformVS), nullptr, &VertexShader);
    if (FAILED(hr))
    {
        LogError(L"Failed to create vertex shader.");
        return false;
    }

    hr = Device->CreatePixelShader(SimpleTexturePS, sizeof(SimpleTexturePS), nullptr, &PixelShader);
    if (FAILED(hr))
    {
        LogError(L"Failed to create pixel shader.");
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC elems[3] {};
    elems[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elems[0].SemanticName = "POSITION";
    elems[1].AlignedByteOffset = sizeof(XMFLOAT3);
    elems[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elems[1].SemanticName = "NORMAL";
    elems[2].AlignedByteOffset = sizeof(XMFLOAT3) * 2;
    elems[2].Format = DXGI_FORMAT_R32G32_FLOAT;
    elems[2].SemanticName = "TEXCOORD";

    hr = Device->CreateInputLayout(elems, _countof(elems), SimpleTransformVS, sizeof(SimpleTransformVS), &InputLayout);
    if (FAILED(hr))
    {
        LogError(L"Failed to create input layout.");
        return false;
    }

    D3D11_BUFFER_DESC bd = {};
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.ByteWidth = sizeof(Constants);
    bd.StructureByteStride = bd.ByteWidth;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = Device->CreateBuffer(&bd, nullptr, &ConstantBuffer);
    if (FAILED(hr))
    {
        LogError(L"Failed to create constant buffer.");
        return false;
    }

    bd.ByteWidth = sizeof(LightConstants);
    bd.StructureByteStride = bd.ByteWidth;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.CPUAccessFlags = 0;

    hr = Device->CreateBuffer(&bd, nullptr, &LightsConstantBuffer);
    if (FAILED(hr))
    {
        LogError(L"Failed to create constant buffer.");
        return false;
    }

    D3D11_SAMPLER_DESC sd = {};
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.MaxLOD = 12;

    hr = Device->CreateSamplerState(&sd, &Sampler);
    if (FAILED(hr))
    {
        LogError(L"Failed to create sampler.");
        return false;
    }

    D3D11_RASTERIZER_DESC rd = {};
    rd.FrontCounterClockwise = TRUE;
    rd.AntialiasedLineEnable = TRUE;
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.DepthClipEnable = TRUE;

    hr = Device->CreateRasterizerState(&rd, &RasterizerState);
    if (FAILED(hr))
    {
        LogError(L"Failed to create rasterizer state.");
        return false;
    }

    RECT rc{};
    GetClientRect(Window, &rc);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)(rc.right - rc.left);
    vp.Height = (float)(rc.bottom - rc.top);
    vp.MaxDepth = 1.f;

    Context->OMSetRenderTargets(1, RenderTarget.GetAddressOf(), DepthBuffer.Get());

    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Context->IASetInputLayout(InputLayout.Get());
    Context->VSSetShader(VertexShader.Get(), nullptr, 0);
    Context->PSSetShader(PixelShader.Get(), nullptr, 0);
    Context->VSSetConstantBuffers(0, 1, ConstantBuffer.GetAddressOf());
    Context->PSSetSamplers(0, 1, Sampler.GetAddressOf());
    Context->PSSetConstantBuffers(0, 1, LightsConstantBuffer.GetAddressOf());
    Context->RSSetState(RasterizerState.Get());
    Context->RSSetViewports(1, &vp);

    return true;
}

bool TestRenderer::LoadTexture(const std::wstring& filename, ID3D11ShaderResourceView** srv)
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

void TestRenderer::Clear()
{
    static const float clearColor[] = { 0.f, 0.f, 0.f, 1.f };
    Context->ClearRenderTargetView(RenderTarget.Get(), clearColor);

    Context->ClearDepthStencilView(DepthBuffer.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
}

bool TestRenderer::Present(bool vsync)
{
    HRESULT hr = SwapChain->Present(vsync ? 1 : 0, 0);
    if (FAILED(hr))
    {
        LogError(L"Present failed.");
        return false;
    }
    return true;
}
