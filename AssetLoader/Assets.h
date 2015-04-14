#pragma once

enum class AssetType
{
    Model = 0,      // Save out as a geometric mesh with model parts
    Texture,        // Save out as standard mipmapped texture
    BumpTexture,    // Process single channel texture into texture-space derivative height map and save as texture
    SpecularTexture,// If single channel texture, expand to RGBA as "standard" spec color map
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

bool DoesAssetNeedBuilt(const SourceAsset& asset, bool* needsBuild);

bool BuildAsset(const SourceAsset& asset, std::wstring& outputRelativePath);

struct ObjModel;

bool SaveModel(const std::unique_ptr<ObjModel>& objModel, const std::wstring& outputFilename);
bool SaveTexture(const std::wstring& assetFilename, const std::wstring& outputFilename, bool saveDerivativeMap = false, bool expandChannels = false);

bool ConvertToBumpMapToNormalMap(const std::wstring& bumpFilename, const std::wstring& outputFilename);
