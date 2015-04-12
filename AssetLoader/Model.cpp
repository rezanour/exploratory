#include "Precomp.h"
#include "Assets.h"
#include "ObjModel.h"
#include "Debug.h"
#include "StringHelpers.h"
#include "AssetLoader.h"

static std::wstring FindTexture(const std::unique_ptr<ObjModel>& model, const std::string& materialName, ObjMaterial::TextureType textureType)
{
    std::wstring texture;

    for (int i = 0; i < (int)model->Materials.size(); ++i)
    {
        if (model->Materials[i].Name == materialName)
        {
            auto it = model->Materials[i].TextureMaps.find(textureType);
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
    header.NumVertices = (uint32_t)objModel->Vertices.size();
    header.NumIndices = (uint32_t)objModel->Indices.size();
    header.NumObjects = (uint32_t)objModel->Objects.size();

    if (!WriteFile(outputFile.Get(), &header, sizeof(header), &bytesWritten, nullptr))
    {
        LogError(L"Error writing output file.");
        return false;
    }

    // Write all vertices next
    if (!WriteFile(outputFile.Get(), objModel->Vertices.data(), header.NumVertices * sizeof(ModelVertex), &bytesWritten, nullptr))
    {
        LogError(L"Error writing output file.");
        return false;
    }

    // Write all indices next
    if (!WriteFile(outputFile.Get(), objModel->Indices.data(), header.NumIndices * sizeof(uint32_t), &bytesWritten, nullptr))
    {
        LogError(L"Error writing output file.");
        return false;
    }

    // Write out objects
    for (int iObj = 0; iObj < (int)objModel->Objects.size(); ++iObj)
    {
        const ObjModelObject& srcObject = objModel->Objects[iObj];

        ModelObject object{};
        strcpy_s(object.Name, srcObject.Name.c_str());
        object.NumParts = (uint32_t)srcObject.Parts.size();

        if (!WriteFile(outputFile.Get(), &object, sizeof(object), &bytesWritten, nullptr))
        {
            LogError(L"Error writing output file.");
            return false;
        }

        // Write out parts
        for (int iPart = 0; iPart < srcObject.Parts.size(); ++iPart)
        {
            const ObjModelPart& srcPart = srcObject.Parts[iPart];

            ModelPart part{};
            std::wstring textureName = FindTexture(objModel, srcPart.Material, ObjMaterial::TextureType::Diffuse);

            if (!textureName.empty())
            {
                if (!BuildAsset(SourceAsset(AssetType::Texture, std::move(textureName)), textureName))
                {
                    LogError(L"Error writing output file.");
                    return false;
                }
            }
            wcscpy_s(part.DiffuseTexture, textureName.c_str());

            textureName = FindTexture(objModel, srcPart.Material, ObjMaterial::TextureType::Bump);

            if (!textureName.empty())
            {
                if (!BuildAsset(SourceAsset(AssetType::BumpTexture, std::move(textureName)), textureName))
                {
                    LogError(L"Error writing output file.");
                    return false;
                }
            }
            wcscpy_s(part.NormalTexture, textureName.c_str());

            textureName = FindTexture(objModel, srcPart.Material, ObjMaterial::TextureType::SpecularColor);

            if (!textureName.empty())
            {
                if (!BuildAsset(SourceAsset(AssetType::Texture, std::move(textureName)), textureName))
                {
                    LogError(L"Error writing output file.");
                    return false;
                }
            }
            wcscpy_s(part.SpecularTexture, textureName.c_str());

            part.StartIndex = srcPart.StartIndex;
            part.NumIndices = srcPart.NumIndices;

            if (!WriteFile(outputFile.Get(), &part, sizeof(part), &bytesWritten, nullptr))
            {
                LogError(L"Error writing output file.");
                return false;
            }
        }
    }

    return true;
}
