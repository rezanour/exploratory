#include "Precomp.h"
#include "Assets.h"
#include "ObjModel.h"
#include "Debug.h"
#include "StringHelpers.h"
#include "AssetLoader.h"

static std::wstring FindDiffuseTexture(const std::unique_ptr<ObjModel>& model, const std::string& materialName)
{
    std::wstring texture;

    for (int i = 0; i < (int)model->Materials.size(); ++i)
    {
        if (model->Materials[i].Name == materialName)
        {
            auto it = model->Materials[i].TextureMaps.find(ObjMaterial::TextureType::Diffuse);
            if (it != model->Materials[i].TextureMaps.end())
            {
                texture = it->second;
            }
            break;
        }
    }

    return texture;
}

bool SaveModel(const std::unique_ptr<ObjModel>& objModel, const std::wstring& outputFilename)
{
    FileHandle outputFile(CreateFile(outputFilename.c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!outputFile.IsValid())
    {
        LogError(L"Failed to create output file: %s.", outputFilename.c_str());
        return false;
    }

    DWORD bytesWritten{};

    ModelHeader header{};
    header.Signature = ModelHeader::ExpectedSignature;
    header.NumObjects = (uint32_t)objModel->Objects.size();

    if (!WriteFile(outputFile.Get(), &header, sizeof(header), &bytesWritten, nullptr))
    {
        LogError(L"Error writing output file.");
        return false;
    }

    for (int iObj = 0; iObj < (int)objModel->Objects.size(); ++iObj)
    {
        const ObjModelObject& srcObject = objModel->Objects[iObj];

        ModelObject object{};
        strcpy_s(object.Name, srcObject.Name.c_str());
        object.NumParts = (uint32_t)srcObject.Parts.size();
        object.MinBounds = srcObject.MinBounds;
        object.MaxBounds = srcObject.MaxBounds;

        if (!WriteFile(outputFile.Get(), &object, sizeof(object), &bytesWritten, nullptr))
        {
            LogError(L"Error writing output file.");
            return false;
        }

        for (int iPart = 0; iPart < srcObject.Parts.size(); ++iPart)
        {
            const ObjModelPart& srcPart = srcObject.Parts[iPart];

            ModelPart part{};
            std::wstring diffuseTexture = FindDiffuseTexture(objModel, srcPart.Material);

            if (!diffuseTexture.empty())
            {
                if (!BuildAsset(SourceAsset(AssetType::Texture, std::move(diffuseTexture)), diffuseTexture))
                {
                    LogError(L"Error writing output file.");
                    return false;
                }
            }

            wcscpy_s(part.DiffuseTexture, diffuseTexture.c_str());
            part.NumVertices = (uint32_t)srcPart.PositionIndices.size();
            part.MinBounds = srcPart.MinBounds;
            part.MaxBounds = srcPart.MaxBounds;

            if (!WriteFile(outputFile.Get(), &part, sizeof(part), &bytesWritten, nullptr))
            {
                LogError(L"Error writing output file.");
                return false;
            }

            std::vector<ModelVertex> vertices;
            vertices.reserve(part.NumVertices);

            for (int i = 0; i < (int)srcPart.PositionIndices.size(); ++i)
            {
                ModelVertex v{};

                XMFLOAT4& pos = objModel->Positions[srcPart.PositionIndices[i]];
                v.Position.x = pos.x;
                v.Position.y = pos.y;
                v.Position.z = pos.z;

                if (srcPart.NormalIndices.size() > 0 && i < srcPart.NormalIndices.size())
                {
                    XMFLOAT3& norm = objModel->Normals[srcPart.NormalIndices[i]];
                    v.Normal.x = norm.x;
                    v.Normal.y = norm.y;
                    v.Normal.z = norm.z;
                }

                if (srcPart.TextureIndices.size() > 0 && i < srcPart.TextureIndices.size())
                {
                    XMFLOAT3& tex = objModel->TexCoords[srcPart.TextureIndices[i]];
                    v.TexCoord.x = tex.x;
                    v.TexCoord.y = tex.y;
                }

                vertices.push_back(v);
            }

            if (srcPart.NormalIndices.empty())
            {
                // Data didn't provide normals, so generate basic ones from triangles
                for (int i = 0; i < (int)vertices.size() - 3; i += 3)
                {
                    XMVECTOR a = XMLoadFloat3(&vertices[i].Position);
                    XMVECTOR b = XMLoadFloat3(&vertices[i + 1].Position);
                    XMVECTOR c = XMLoadFloat3(&vertices[i + 2].Position);
                    XMVECTOR n = XMVector3Normalize(XMVector3Cross(b - a, c - a));
                    XMStoreFloat3(&vertices[i].Normal, n);
                    XMStoreFloat3(&vertices[i + 1].Normal, n);
                    XMStoreFloat3(&vertices[i + 2].Normal, n);
                }
            }

            if (!WriteFile(outputFile.Get(), vertices.data(), sizeof(ModelVertex) * (uint32_t)vertices.size(), &bytesWritten, nullptr))
            {
                LogError(L"Error writing output file.");
                return false;
            }
        }
    }

    return true;
}
