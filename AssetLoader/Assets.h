#pragma once

enum class AssetType
{
    Model = 0,
    Texture
};

struct SourceAsset
{
    AssetType Type;
    std::wstring Path;

    // Support moving string in during construct to avoid copy
    SourceAsset(AssetType type, std::wstring&& path)
        : Type(type), Path(std::move(path))
    {
        // Normalize all \\ to /
        for (int i = 0; i < (int)Path.size(); ++i)
        {
            if (Path[i] == L'\\') Path[i] = L'/';
        }
    }
};

bool ProcessAssets(
    const std::wstring& sourceRoot,
    const std::wstring& outputRoot,
    const std::vector<SourceAsset>& assets);

bool BuildModel(
    const std::wstring& assetFilename,
    const std::wstring& outputFilename);

struct ObjModel;

bool SaveModel(
    const std::unique_ptr<ObjModel>& objModel,
    const std::wstring& outputFilename);
