#include "Precomp.h"
#include "ContentLoader.h"
#include "Geometry.h"

ContentLoader::ContentLoader(const ComPtr<ID3D11Device>& device, const std::wstring& contentRoot)
    : Device(device)
    , ContentRoot(contentRoot)
{
}

bool ContentLoader::LoadObject(const std::wstring& filename, std::shared_ptr<Object>* object)
{
    static_assert(sizeof(ModelVertex) == sizeof(StandardVertex), "Make sure structures (and padding) match so we can read directly!");

    object->reset();

    FileHandle modelFile(CreateFile((ContentRoot + filename).c_str(), GENERIC_READ,
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

    ComPtr<ID3D11DeviceContext> context;
    Device->GetImmediateContext(&context);
    context->UpdateSubresource(pool->GetVertexBuffer().Get(), 0, &box, vertices.get(), header.NumVertices * sizeof(StandardVertex), 0);

    // Free up memory
    vertices.reset();

    std::unique_ptr<uint32_t[]> indices(new uint32_t[header.NumIndices]);
    if (!ReadFile(modelFile.Get(), indices.get(), header.NumIndices * sizeof(uint32_t), &bytesRead, nullptr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

    box.right = header.NumIndices * sizeof(uint32_t);

    context->UpdateSubresource(pool->GetIndexBuffer().Get(), 0, &box, indices.get(), header.NumIndices * sizeof(uint32_t), 0);

    // Free up memory
    indices.reset();

    *object = std::make_shared<Object>();

    // Load objects
    for (int iObj = 0; iObj < (int)header.NumObjects; ++iObj)
    {
        ModelObject obj{};
        if (!ReadFile(modelFile.Get(), &obj, sizeof(obj), &bytesRead, nullptr))
        {
            LogError(L"Failed to read file.");
            return false;
        }

        XMStoreFloat4x4(&(*object)->RootTransform, XMMatrixIdentity());

        for (int iPart = 0; iPart < (int)obj.NumParts; ++iPart)
        {
            ModelPart part{};
            if (!ReadFile(modelFile.Get(), &part, sizeof(part), &bytesRead, nullptr))
            {
                LogError(L"Failed to read file.");
                return false;
            }

            std::shared_ptr<Object::Part> meshPart = std::make_shared<Object::Part>();
            (*object)->Parts.push_back(meshPart);

            XMStoreFloat4x4(&meshPart->RelativeTransform, XMMatrixIdentity());

            meshPart->Mesh = std::make_shared<GeoMesh>();
            meshPart->Mesh->Pool = pool;
            meshPart->Mesh->BaseIndex = part.StartIndex;
            meshPart->Mesh->NumIndices = part.NumIndices;
            meshPart->Mesh->BaseVertex = baseVertex;

            if (part.DiffuseTexture[0] != 0)
            {
                std::wstring path = ContentRoot + part.DiffuseTexture;
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
                std::wstring path = ContentRoot + part.NormalTexture;
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
                std::wstring path = ContentRoot + part.SpecularTexture;
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

bool ContentLoader::LoadTexture(const std::wstring& filename, ComPtr<ID3D11ShaderResourceView>* srv)
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

    hr = Device->CreateShaderResourceView(texture.Get(), nullptr, srv->ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        LogError(L"Failed to create texture SRV.");
        return false;
    }

    return true;
}
