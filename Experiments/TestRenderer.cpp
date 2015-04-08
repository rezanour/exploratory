#include "Precomp.h"
#include "TestRenderer.h"
#include "Debug.h"
#include "SimpleTransformVS.h"
#include "SimpleTexturePS.h"

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

bool TestRenderer::AddMeshes(const std::wstring& contentRoot, const std::wstring& modelFilename, const XMFLOAT3& desiredSize)
{
    static_assert(sizeof(ModelVertex) == sizeof(Vertex), "Make sure structures (and padding) match so we can read directly!");

    FileHandle modelFile(CreateFile((contentRoot + modelFilename).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!modelFile.IsValid())
    {
        LogError(L"Failed to open asset file.");
        return false;
    }

    std::vector<Vertex> vertices;

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

    XMFLOAT4X4 worldMatrix{};

    for (int iObj = 0; iObj < (int)header.NumObjects; ++iObj)
    {
        ModelObject object{};
        if (!ReadFile(modelFile.Get(), &object, sizeof(object), &bytesRead, nullptr))
        {
            LogError(L"Failed to read file.");
            return false;
        }

        // Determine scale based on first object
        if (iObj == 0)
        {
            XMFLOAT3 size = XMFLOAT3(
                object.MaxBounds.x - object.MinBounds.x,
                object.MaxBounds.y - object.MinBounds.y,
                object.MaxBounds.z - object.MinBounds.z);

            XMFLOAT3 scales = XMFLOAT3(desiredSize.x / size.x, desiredSize.y / size.y, desiredSize.z / size.z);

            XMStoreFloat4x4(&worldMatrix, XMMatrixScaling(scales.x, scales.y, scales.z));
        }

        for (int iPart = 0; iPart < (int)object.NumParts; ++iPart)
        {
            ModelPart part{};
            if (!ReadFile(modelFile.Get(), &part, sizeof(part), &bytesRead, nullptr))
            {
                LogError(L"Failed to read file.");
                return false;
            }

            if (part.NumVertices > vertices.size())
            {
                vertices.resize(part.NumVertices);
            }
            if (!ReadFile(modelFile.Get(), vertices.data(), sizeof(Vertex) * part.NumVertices, &bytesRead, nullptr))
            {
                LogError(L"Failed to read file.");
                return false;
            }

            std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>();
            mesh->World = worldMatrix;
            mesh->VertexCount = (int)part.NumVertices;

            D3D11_BUFFER_DESC bd = {};
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.ByteWidth = sizeof(Vertex) * mesh->VertexCount;
            bd.StructureByteStride = sizeof(Vertex);
            bd.Usage = D3D11_USAGE_DEFAULT;

            D3D11_SUBRESOURCE_DATA init = {};
            init.pSysMem = vertices.data();
            init.SysMemPitch = bd.ByteWidth;
            init.SysMemSlicePitch = init.SysMemPitch;

            HRESULT hr = Device->CreateBuffer(&bd, &init, &mesh->VertexBuffer);
            if (FAILED(hr))
            {
                LogError(L"Failed to create vertex buffer.");
                return false;
            }

            if (part.DiffuseTexture[0] != 0)
            {
                FileHandle texFile(CreateFile((contentRoot + part.DiffuseTexture).c_str(), GENERIC_READ,
                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
                if (!texFile.IsValid())
                {
                    LogError(L"Failed to open texture.");
                    return false;
                }

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
                td.Width = texHeader.Width;
                td.Height = texHeader.Height;
                td.MipLevels = texHeader.MipLevels;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DEFAULT;

                D3D11_SUBRESOURCE_DATA init[20] {};
                uint32_t width = td.Width;
                uint32_t height = td.Height;
                uint32_t bpp = (uint32_t)BitsPerPixel(td.Format) / 8;
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
                ComPtr<ID3D11Texture2D> texture;
                HRESULT hr = Device->CreateTexture2D(&td, init, &texture);
                if (FAILED(hr))
                {
                    // Try without mips
                    td.MipLevels = 1;

                    init[0].pSysMem = pixelData.get();
                    init[0].SysMemPitch = td.Width * bpp;
                    init[0].SysMemSlicePitch = td.Width * td.Height * bpp;

                    hr = Device->CreateTexture2D(&td, init, &texture);
                    if (FAILED(hr))
                    {
                        LogError(L"Failed to create texture.");
                        return false;
                    }
                }

                hr = Device->CreateShaderResourceView(texture.Get(), nullptr, &mesh->SRV);
                if (FAILED(hr))
                {
                    LogError(L"Failed to create texture SRV.");
                    return false;
                }
            }

            Meshes.push_back(mesh);
        }
    }

    return true;
}

bool TestRenderer::Render(FXMMATRIX view, FXMMATRIX projection, bool vsync)
{
    Clear();

    D3D11_MAPPED_SUBRESOURCE mapped{};

    for (int i = 0; i < (int)Meshes.size(); ++i)
    {
        static const uint32_t stride = sizeof(Vertex);
        static const uint32_t offset = 0;

        if (FAILED(Context->Map(ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            LogError(L"Failed to map constant buffer.");
        }

        Constants* constants = (Constants*)mapped.pData;
        XMStoreFloat4x4(&constants->ViewProjection, view * projection);
        constants->World = Meshes[i]->World;

        Context->Unmap(ConstantBuffer.Get(), 0);

        Context->IASetVertexBuffers(0, 1, Meshes[i]->VertexBuffer.GetAddressOf(), &stride, &offset);
        Context->PSSetShaderResources(0, 1, Meshes[i]->SRV.GetAddressOf());
        Context->Draw(Meshes[i]->VertexCount, 0);
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
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
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
    elems[2].AlignedByteOffset = 2 * sizeof(XMFLOAT3);
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
    Context->RSSetState(RasterizerState.Get());
    Context->RSSetViewports(1, &vp);

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
